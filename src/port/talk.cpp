// Talks: the talk session and its views, and the WORDS script handlers.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"
#include "vu0.h"

namespace mm
{
    // The pose stored for a family member in the session's 32-entry table; 0 when there is none.
    int Talk_LookupPose(GuestPtr<TalkSession> talk, uint32_t family, uint32_t member, GuestPtr<uint32_t> pose)
    {
        for (const TalkPose &entry : talk->poses)
            if (entry.family == family && entry.member == member)
            {
                *pose = entry.pose;
                return 1;
            }
        return 0;
    }

    GuestPtr<TalkSession> Talk_ctor(GuestPtr<TalkSession> talk)
    {
        Widget_ctor(GuestPtr<Widget>::of(talk->base));
        talk->base.vtable = GuestPtr<void>{0x001BD770u};
        talk->city = GuestPtr<CityScene>{0};
        talk->view = GuestPtr<TalkView>{0};
        talk->conversation = GuestPtr<Widget>{0};
        talk->endChoosing = 0;
        return talk;
    }

    // A talk view: starts from the city camera's eye and target, with the talk models (0x1F3B2C, 0x1F3B7C),
    // a grey colour and its packet, no windows yet and every person slot idle (animation 7).
    GuestPtr<TalkView> TalkView_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city)
    {
        Widget_ctor(GuestPtr<Widget>::of(view->base));
        view->base.vtable = GuestPtr<void>{0x001BD450u};
        view->forcedChoice = -1;
        view->forceLit = 0;
        view->city = city;
        const GuestPtr<CityCamera> camera = view->city->cityCamera;
        for (uint32_t i = 0; i < 16; i += 4)
        {
            port::store<uint32_t>(view.address + 0x10u + i, port::load<uint32_t>(camera.address + 0x130u + i)); // eye
            port::store<uint32_t>(view.address + 0x20u + i, port::load<uint32_t>(camera.address + 0x140u + i)); // target
        }
        view->buildingModel = GuestPtr<void>{port::load<uint32_t>(0x001F3B2Cu)};
        view->personModel = GuestPtr<void>{port::load<uint32_t>(0x001F3B7Cu)};
        view->color[0] = 0.5f;
        view->color[1] = 0.5f;
        view->color[2] = 0.5f;
        view->color[3] = 1.0f;
        // DMA packet sending the colour to VU1: REF tag to it, then STCYCL and UNPACK V4-32 to VU address 0x0F,
        // then a RET tag.
        const GuestPtr<uint32_t> packet{view.address + 0x50u};
        packet[0] = 0x30000001u;
        packet[1] = (view.address + 0x40u) & 0x7FFFFFFFu;
        packet[2] = 0x01000101u;
        packet[3] = 0x6C01000Fu;
        packet[4] = 0x60000000u;
        packet[5] = 0;
        packet[6] = 0;
        packet[7] = 0;
        for (int i = 0; i < 4; ++i)
        {
            view->frames[i] = GuestPtr<void>{0};
            view->nameBoxes[i] = GuestPtr<void>{0};
        }
        view->cursorFrame = GuestPtr<void>{0};
        for (int slot = 0; slot < 32; ++slot)
        {
            view->anim[slot] = 7;
            view->animFlip[slot] = 0;
            view->animTime[slot] = 0.0f;
        }
        view->peopleCount = 0;
        view->chosenSlot = -1;
        view->choosing = 0;
        return view;
    }

    // The choice cursor: a small frame at the cursor column, drawn at once.
    void TalkView_DrawCursor(GuestPtr<TalkView> view)
    {
        Frame_SetRect(GuestPtr<Frame>{view->cursorFrame.address}, view->cursorX, 0x26, 0x18, 0xC);
        Dma_AppendCall(static_cast<int>(view->cursorFrame.address));
    }

    // The far view of a building (allocated 0x2A0 bytes) keeps the camera to return to: the eye in the base
    // view's last 16 bytes (+0x260, over targetObject and targetPerson) and the target at +0x270. The
    // building is at +0x280.
    namespace
    {
        GuestPtr<Vec4> farReturnEye(GuestPtr<TalkView> view) { return GuestPtr<Vec4>{view.address + 0x260u}; }
        GuestPtr<Vec4> farReturnTarget(GuestPtr<TalkView> view) { return GuestPtr<Vec4>{view.address + 0x270u}; }
        int farBuilding(GuestPtr<TalkView> view) { return port::load<int32_t>(view.address + 0x280u); }
        GuestPtr<Camera> cityCamera(GuestPtr<TalkView> view) { return GuestPtr<Camera>{view->city->cityCamera.address}; }

        void cameraOnObject(GuestPtr<TalkView> view, int object)
        {
            port::GuestLocal<Vec4> target; // declared first: the same stack slots as the original's frame
            port::GuestLocal<Vec4> eye;
            TalkView_BuildingCamera(view, object, eye, target);
            Camera_SetEyeTarget(cityCamera(view), eye, target);
            Camera_UpdateMatrices(cityCamera(view));
        }

        void cameraOnBuilding(GuestPtr<TalkView> view) { cameraOnObject(view, farBuilding(view)); }

        void cameraAt(GuestPtr<TalkView> view, GuestPtr<Vec4> eye, GuestPtr<Vec4> target)
        {
            Camera_SetEyeTarget(cityCamera(view), eye, target);
            Camera_UpdateMatrices(cityCamera(view));
        }

        void cameraBack(GuestPtr<TalkView> view) { cameraAt(view, farReturnEye(view), farReturnTarget(view)); }
    }

    // Widget states of the far view: 1 opening (fade out), 2 opened (camera moves to the building, fade in),
    // 3 active, 4 closing, 5 closed (camera back, HUD element 1 shown again).
    void TalkBldgFar_Opening(GuestPtr<TalkView> view)
    {
        if (CityScene_GetFade(view->city) == 0)
        {
            CityScene_StartFade(view->city, 8);
            view->base.state = 2;
        }
        cameraBack(view);
    }

    void TalkBldgFar_Opened(GuestPtr<TalkView> view)
    {
        if (CityScene_GetFade(view->city) == 0)
            view->base.state = 3;
        cameraOnBuilding(view);
    }

    // Picking a person at the building: left and right (with key repeat, or towards a forced choice) move the
    // cursor over the three people in view, 16 pixels a frame for 10 frames, or past the edge zoom the view
    // by 0.03 a frame for 10 frames while the people shift by one. The idle timer runs on throughout.
    void TalkView_HandleInput(GuestPtr<TalkView> view)
    {
        const int mode = view->scrollMode;
        if (mode == 2)
        {
            view->cursorX += view->scrollDir == 1 ? 16 : -16;
            if (++view->scrollStep >= 10)
                view->scrollMode = 0;
        }
        else if (mode == 1)
        {
            if (view->scrollDir == 1)
                view->zoom = view->zoom + ps2f(0.03f);
            else
                view->zoom = view->zoom - ps2f(0.03f);
            if (++view->scrollStep >= 10)
                view->scrollMode = 0;
        }
        else if (mode == 0)
        {
            const bool left = view->forcedChoice < 0 ? (Pad_GetRepeat() & 0x8000) != 0 : view->forcedChoice < view->cursorSlot;
            if (left)
            {
                if (view->choiceSlot > 0)
                {
                    view->scrollMode = 2;
                    --view->choiceSlot;
                    view->scrollDir = -1;
                    view->scrollStep = 0;
                    --view->cursorSlot;
                }
                else if (view->firstShown > 0)
                {
                    view->scrollMode = 1;
                    view->scrollStep = 0;
                    view->scrollDir = 1;
                    --view->firstShown;
                    --view->cursorSlot;
                }
            }
            else if (view->forcedChoice < 0 ? (Pad_GetRepeat() & 0x2000) != 0 : view->cursorSlot < view->forcedChoice)
            {
                if (view->choiceSlot < 2 && view->choiceSlot < view->peopleCount - 1)
                {
                    view->scrollMode = 2;
                    ++view->choiceSlot;
                    view->scrollDir = 1;
                    view->scrollStep = 0;
                    ++view->cursorSlot;
                }
                else if (view->firstShown < view->peopleCount - 3)
                {
                    view->scrollMode = 1;
                    view->scrollStep = 0;
                    view->scrollDir = -1;
                    ++view->firstShown;
                    ++view->cursorSlot;
                }
            }
        }
        view->idleTime = view->idleTime + ps2f(1.0f);
        if (!(view->idleTime < ps2f(180.0f)))
            view->idleTime = 0.0f;
    }

    // Talk session state 5: once the view reports idle, the scene mode is popped, the conversation and the view
    // are deleted, the HUD and the applicants panel come back, and if the talk took the pad focus (city flag
    // 0x40) it goes back to the city.
    void Talk_Closed(GuestPtr<TalkSession> talk)
    {
        const auto slot = [](uint32_t widget, uint32_t offset) { return port::load<uint32_t>(port::load<uint32_t>(widget + 8u) + offset); };
        const uint32_t view = talk->view.address;
        if (port::callGuest<int>(slot(view, 0x1Cu), view) != 0)
            return;
        CityScene_PopMode(talk->city);
        talk->base.state = 0;
        if (const uint32_t conversation = talk->conversation.address)
        {
            port::callGuest<void>(slot(conversation, 8u), conversation, 1);
            talk->conversation = GuestPtr<Widget>{0};
        }
        if (const uint32_t remaining = talk->view.address)
        {
            port::callGuest<void>(slot(remaining, 8u), remaining, 1);
            talk->view = GuestPtr<TalkView>{0};
        }
        talk->city->hudVisible[1] = 1;
        talk->city->hudVisible[2] = 1;
        talk->city->hudVisible[3] = 1;
        talk->city->applicants->enabled = 1;
        if ((talk->city->flags & 0x40u) != 0)
        {
            Pad_TakeFocus(4, 0x1F0);
            talk->city->flags &= ~0x40u;
        }
    }

    void TalkBldgFar_Active(GuestPtr<TalkView> view)
    {
        cameraOnBuilding(view);
        TalkView_HandleInput(view);
    }

    void TalkBldgFar_Closing(GuestPtr<TalkView> view)
    {
        if (CityScene_GetFade(view->city) == 0)
        {
            CityScene_StartFade(view->city, 8);
            view->base.state = 5;
        }
        cameraOnBuilding(view);
    }

    void TalkBldgFar_Closed(GuestPtr<TalkView> view)
    {
        if (CityScene_GetFade(view->city) == 0)
        {
            view->base.state = 0;
            view->city->hudVisible[1] = 1;
        }
        cameraBack(view);
    }

    // The far view of a building fades the city (speed 8) as it opens and closes.
    void TalkBldgFar_Open(GuestPtr<TalkView> view)
    {
        CityScene_StartFade(view->city, 8);
        Widget_Open(GuestPtr<Widget>::of(view->base));
    }

    void TalkBldgFar_Close(GuestPtr<TalkView> view)
    {
        CityScene_StartFade(view->city, 8);
        Widget_Close(GuestPtr<Widget>::of(view->base));
    }

    // The camera for a talk at building `object`: the target at the middle of its footprint, half a unit below
    // the object's height (2.5 lower for type flag 0x10); the eye at the target plus 0.4 back along the view's
    // direction (its open target minus eye, flattened and normalized). The original's frame is reserved whole
    // and its vectors and matrix kept at their offsets.
    void TalkView_PersonCamera(GuestPtr<TalkView> view, int object, GuestPtr<Vec4> target, GuestPtr<Vec4> eye)
    {
        using namespace vu0;
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> direction{sp + 0x80u}, back{sp + 0x90u}, turn{sp + 0xA0u};
        const GuestPtr<MapObject> obj = Map_GetRecord(view->city->map, object);
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
        const ps2f size = fromInt(btype->size);
        const ps2f half = size / ps2f(2.0f), centre = 64.0f;
        target->x = (fromInt(obj->tile % 128) - centre) + half;
        target->y = MapObj_GetHeight(view->city->map, static_cast<uint32_t>(object)) - ps2f(0.5f);
        target->z = (fromInt(128 - obj->tile / 128) - centre) - size / ps2f(2.0f);
        target->w = 1.0f;
        if ((btype->flags & 0x10u) != 0)
            target->y = target->y - ps2f(2.5f);
        *eye = *target;
        back->x = 0.0f;
        back->y = 0.0f;
        back->z = 0.4f;
        back->w = 0.0f;
        vf(4) = load(GuestPtr<Vec4>{view.address + 0x20u});
        vf(5) = load(GuestPtr<Vec4>{view.address + 0x10u});
        set(vf(4), sub(vf(4), vf(5)), XYZW);
        store(direction, vf(4));
        direction->y = 0.0f;
        Vec3_Normalize(direction, direction);
        Mat4_Copy(turn, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const ps2f dz = -direction->z;
        turn[0].x = dz;
        turn[0].z = direction->x;
        turn[2].x = -direction->x;
        turn[2].z = dz;
        Math_MulMatVecVu0(back, turn, back);
        vf(4) = load(eye);
        vf(5) = load(back);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(eye, vf(4));
    }

    // The camera for a talk at building `object`, eye first. The target is the middle of the footprint at the
    // object's height, the eye an offset turned by the tile's facing (the building matrix of its tile kind,
    // translation row reset): the shared one (2.5 lower for type flag 0x10) for an object without the
    // entrance flag (0x20), the type's own for an entrance of a type with flag 0x4000. The target then takes
    // the eye's height. Other entrances use the person camera, which takes the target first.
    void TalkView_BuildingCamera(GuestPtr<TalkView> view, int object, GuestPtr<Vec4> eye, GuestPtr<Vec4> target)
    {
        using namespace vu0;
        const GuestPtr<MapObject> obj = Map_GetRecord(view->city->map, object);
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
        const bool entrance = (obj->flags & 0x20u) != 0;
        if (entrance && (btype->flags & 0x4000u) == 0)
        {
            TalkView_PersonCamera(view, object, target, eye);
            return;
        }
        const ps2f half = fromInt(btype->size) / ps2f(2.0f), centre = 64.0f;
        target->x = (fromInt(obj->tile % 128) - centre) + half;
        target->y = MapObj_GetHeight(view->city->map, static_cast<uint32_t>(object));
        target->z = (fromInt(128 - obj->tile / 128) - centre) - half;
        target->w = 1.0f;
        const auto kind = static_cast<uint32_t>(Map_GetTileKind(view->city->map, obj->tile));
        const GuestPtr<Vec4> turn{globals::kBuildingMatrices + 0x40u * kind};
        turn[3] = *GuestPtr<Vec4>{globals::kOriginPoint};
        if (!entrance)
        {
            Math_MulMatVecVu0(eye, turn, GuestPtr<Vec4>{globals::kTalkEyeOffset});
            if ((btype->flags & 0x10u) != 0)
                eye->y = eye->y - ps2f(2.5f);
        }
        else
        {
            Math_MulMatVecVu0(eye, turn, GuestPtr<Vec4>::of(btype->talkEye));
        }
        vf(4) = load(target);
        vf(5) = load(eye);
        set(vf(4), add(vf(4), vf(5)), XYZW);
        store(eye, vf(4));
        target->y = eye->y;
        eye->w = 1.0f;
    }

    // For a talk at a building (+0x70; 0x3F80, a talk with a family, has nothing to do): the building's workers
    // among the people listed at it, outside or already in it and of a class without flag 1, are sorted by
    // their +0x2E byte, highest first, then each is unlinked from the building's tile and, when it was linked
    // there, added to the building. The sorted list sits in the original's frame (sp + 0x60, its cursor at
    // + 0x7E), and as in the original an insertion only shifts the entries up to the 6th, however long the list.
    void TalkView_CollectOccupants(GuestPtr<TalkView> view)
    {
        if (view->building == kNoObject)
            return;
        port::GuestFrame<0x80> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<uint16_t> list{sp + 0x60u};
        const GuestPtr<uint16_t> cursor{sp + 0x7Eu};
        const GuestPtr<MapObject> obj = Map_GetRecord(view->city->map, view->building);
        *cursor = obj->firstPerson;
        int count = 0;
        while (*cursor != kNoPerson)
        {
            const GuestPtr<MapPerson> person = People_Next(view->city->map, cursor);
            if (!person)
                break;
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) != 0)
                continue;
            if (person->inside != kNoObject && person->inside != view->building)
                continue;
            if (person->work != obj->id)
                continue;
            int at = 0;
            while (at < count && !(view->city->map->people[list[at]].member < person->member))
                ++at;
            if (at < count && at < 6)
                for (int k = 5; k >= at; --k)
                    list[k + 1] = list[k];
            list[at] = person->index;
            ++count;
        }
        for (int i = 0; i < count; ++i)
            if (Map_UnlinkPersonFromTile(view->city->map, obj->tile, list[i]) != kNoObject)
                MapObj_AddPerson(view->city->map, obj->id, list[i]);
    }

    // ---- the other talk views (batch 38)

    // Occupant, person and phone views keep a camera of their own after the base view, eye at +0x270 and target
    // at +0x280: the far views return to it, the near views look through it. Person views draw the person
    // (+0x260) turned by the angle at +0x290.
    namespace
    {
        GuestPtr<Vec4> ownEye(GuestPtr<TalkView> view) { return GuestPtr<Vec4>{view.address + 0x270u}; }
        GuestPtr<Vec4> ownTarget(GuestPtr<TalkView> view) { return GuestPtr<Vec4>{view.address + 0x280u}; }
        float personAngle(GuestPtr<TalkView> view) { return port::load<float>(view.address + 0x290u); }

        bool fadeDone(GuestPtr<TalkView> view) { return CityScene_GetFade(view->city) == 0; }

        // Starts the next fade (speed 8) and moves on to `state`.
        void fadeTo(GuestPtr<TalkView> view, int state)
        {
            CityScene_StartFade(view->city, 8);
            view->base.state = state;
        }

        // The view is done: idle, and HUD element 1 shown again.
        void closed(GuestPtr<TalkView> view)
        {
            view->base.state = 0;
            view->city->hudVisible[1] = 1;
        }

        void fadeOpen(GuestPtr<TalkView> view)
        {
            CityScene_StartFade(view->city, 8);
            Widget_Open(GuestPtr<Widget>::of(view->base));
        }

        void fadeClose(GuestPtr<TalkView> view)
        {
            CityScene_StartFade(view->city, 8);
            Widget_Close(GuestPtr<Widget>::of(view->base));
        }

        // The next person of a talk at a building, in list order: outside or inside it, of a class without
        // flag 1. 0 at the end of the list.
        GuestPtr<MapPerson> nextTalkPerson(GuestPtr<TalkView> view, GuestPtr<uint16_t> cursor)
        {
            for (;;)
            {
                const GuestPtr<MapPerson> person = People_Next(view->city->map, cursor);
                if (!person)
                    return person;
                const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
                if ((cls->flags & 1u) != 0)
                    continue;
                if (person->inside != kNoObject && person->inside != view->building)
                    continue;
                return person;
            }
        }
    }

    // The person under the cursor in a talk at a building, 0x3F80 when there is none.
    uint16_t TalkView_CursorPerson(GuestPtr<TalkView> view)
    {
        port::GuestLocal<uint16_t> cursor;
        *cursor = Map_GetRecord(view->city->map, view->building)->firstPerson;
        for (int slot = 0;; ++slot)
        {
            const GuestPtr<MapPerson> person = nextTalkPerson(view, cursor);
            if (!person)
                return kNoObject;
            if (slot == view->cursorSlot)
                return person->index;
        }
    }

    // The slot of a family member in the talk: at a building their place among its people (0 when not
    // there), in a talk with a family the member number itself. 0 while nobody is in the talk.
    uint32_t TalkView_FindPersonSlot(GuestPtr<TalkView> view, uint32_t family, uint32_t member)
    {
        if (view->peopleCount == 0)
            return 0;
        if (view->building == kNoObject)
            return member;
        port::GuestLocal<uint16_t> cursor;
        *cursor = Map_GetRecord(view->city->map, view->building)->firstPerson;
        for (uint32_t slot = 0;; ++slot)
        {
            const GuestPtr<MapPerson> person = nextTalkPerson(view, cursor);
            if (!person)
                return 0;
            if (person->family == family && person->member == member)
                return slot;
        }
    }

    // The choice made, if any: none while the cursor moves, while it is off a forced choice or with nobody in
    // the talk. Otherwise the person (in a talk with a family, the cursor slot) and the column, and the cursor
    // slot becomes the chosen one.
    int TalkView_GetChoice(GuestPtr<TalkView> view, GuestPtr<uint32_t> person, GuestPtr<uint32_t> slot)
    {
        if (view->scrollMode != 0)
            return 0;
        if (view->forcedChoice >= 0 && view->cursorSlot != view->forcedChoice)
            return 0;
        if (view->peopleCount == 0)
            return 0;
        if (view->building == kNoObject)
            *person = static_cast<uint32_t>(view->cursorSlot);
        else
            *person = TalkView_CursorPerson(view);
        *slot = static_cast<uint32_t>(view->choiceSlot);
        view->chosenSlot = view->cursorSlot;
        return 1;
    }

    // The chosen person (slot 0 when none), when idle, plays animation 2 or 3 by turns from its start.
    void TalkView_ToggleAnim(GuestPtr<TalkView> view)
    {
        const int slot = view->chosenSlot == -1 ? 0 : view->chosenSlot;
        if (view->anim[slot] != 7)
            return;
        view->anim[slot] = view->animFlip[slot] == 0 ? 2 : 3;
        view->animTime[slot] = 0.0f;
        view->animFlip[slot] = view->animFlip[slot] ^ 1;
    }

    // The near views look through their camera and clear the city's word +0x28 every frame; the near view of a
    // building also takes the cursor input.
    void TalkBldgNear_Active(GuestPtr<TalkView> view)
    {
        cameraOnObject(view, view->targetObject);
        view->city->unk_28 = 0;
        TalkView_HandleInput(view);
    }

    void TalkOccupantNear_Active(GuestPtr<TalkView> view)
    {
        cameraOnObject(view, view->targetObject);
        view->city->unk_28 = 0;
    }

    void TalkPersonNear_Active(GuestPtr<TalkView> view)
    {
        cameraAt(view, ownEye(view), ownTarget(view));
        view->city->unk_28 = 0;
    }

    void TalkPersonNear_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (pass == 1)
            TalkView_DrawPersonTurned(view, view->targetObject, personAngle(view));
    }

    // The near view of a building's occupant (vtable 0x1BD630) sets its camera on the building once, as for a
    // person there.
    GuestPtr<TalkView> TalkOccupantNear_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int object, int person)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<void>{0x001BD630u};
        view->targetObject = object;
        view->targetPerson = person;
        TalkView_PersonCamera(view, object, ownTarget(view), ownEye(view));
        return view;
    }

    // The far view of a building's occupant (vtable 0x1BD5E0) keeps the city camera to return to and looks at
    // the building like the far view of a building, without cursor input.
    GuestPtr<TalkView> TalkOccupantFar_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int object, int person)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<void>{0x001BD5E0u};
        view->targetObject = object;
        view->targetPerson = person;
        const uint32_t camera = city->cityCamera.address;
        *ownEye(view) = *GuestPtr<Vec4>{camera + 0x130u};
        *ownTarget(view) = *GuestPtr<Vec4>{camera + 0x140u};
        return view;
    }

    void TalkOccupantFar_Open(GuestPtr<TalkView> view) { fadeOpen(view); }
    void TalkOccupantFar_Close(GuestPtr<TalkView> view) { fadeClose(view); }

    void TalkOccupantFar_Opening(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 2);
        cameraAt(view, ownEye(view), ownTarget(view));
    }

    void TalkOccupantFar_Opened(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            view->base.state = 3;
        cameraOnObject(view, view->targetObject);
    }

    void TalkOccupantFar_Active(GuestPtr<TalkView> view) { cameraOnObject(view, view->targetObject); }

    void TalkOccupantFar_Closing(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 5);
        cameraOnObject(view, view->targetObject);
    }

    void TalkOccupantFar_Closed(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            closed(view);
        cameraAt(view, ownEye(view), ownTarget(view));
    }

    // The far view of a person changes the camera only under the fades: to its own camera as it opens, back to
    // the city camera it opened from as it closes. The person is drawn while it is open (states 2 to 4).
    void TalkPersonFar_Open(GuestPtr<TalkView> view) { fadeOpen(view); }
    void TalkPersonFar_Close(GuestPtr<TalkView> view) { fadeClose(view); }

    void TalkPersonFar_Opening(GuestPtr<TalkView> view)
    {
        if (!fadeDone(view))
            return;
        cameraAt(view, ownEye(view), ownTarget(view));
        fadeTo(view, 2);
    }

    void TalkPersonFar_Opened(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            view->base.state = 3;
    }

    void TalkPersonFar_Active(GuestPtr<TalkView>) {}

    void TalkPersonFar_Closing(GuestPtr<TalkView> view)
    {
        if (!fadeDone(view))
            return;
        cameraAt(view, GuestPtr<Vec4>{view.address + 0x10u}, GuestPtr<Vec4>{view.address + 0x20u});
        fadeTo(view, 5);
    }

    void TalkPersonFar_Closed(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            closed(view);
    }

    void TalkPersonFar_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (pass == 1 && view->base.state >= 2 && view->base.state < 5)
            TalkView_DrawPersonTurned(view, view->targetObject, personAngle(view));
    }

    // The phone views (a whole family, or one member: vtable 0x1BD4F0, the family and member in the object and
    // person slots) only fade in and out; picking in the family one takes the cursor input.
    void TalkPhoneFamily_Open(GuestPtr<TalkView> view) { fadeOpen(view); }
    void TalkPhoneFamily_Close(GuestPtr<TalkView> view) { fadeClose(view); }

    void TalkPhoneFamily_Opening(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 2);
    }

    void TalkPhoneFamily_Opened(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            view->base.state = 3;
    }

    void TalkPhoneFamily_Active(GuestPtr<TalkView> view) { TalkView_HandleInput(view); }

    void TalkPhoneFamily_Closing(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 5);
    }

    void TalkPhoneFamily_Closed(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            closed(view);
    }

    GuestPtr<TalkView> TalkPhoneMember_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int family, int member)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<void>{0x001BD4F0u};
        view->targetObject = family;
        view->targetPerson = member;
        return view;
    }

    void TalkPhoneMember_Open(GuestPtr<TalkView> view) { fadeOpen(view); }
    void TalkPhoneMember_Close(GuestPtr<TalkView> view) { fadeClose(view); }

    void TalkPhoneMember_Opening(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 2);
    }

    void TalkPhoneMember_Opened(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            view->base.state = 3;
    }

    void TalkPhoneMember_Active(GuestPtr<TalkView>) {}

    void TalkPhoneMember_Closing(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            fadeTo(view, 5);
    }

    void TalkPhoneMember_Closed(GuestPtr<TalkView> view)
    {
        if (fadeDone(view))
            closed(view);
    }

    // The money panel's amount counts 100 a frame towards the real one, never past it.
    void TalkMoney_Active(GuestPtr<TalkMoney> money)
    {
        if (money->amount < money->shown)
        {
            money->shown = money->shown - 100;
            if (money->shown < money->amount)
                money->shown = money->amount;
        }
        else if (money->shown < money->amount)
        {
            money->shown = money->shown + 100;
            if (money->amount < money->shown)
                money->shown = money->amount;
        }
    }

    // Pass 2: over a cleared depth buffer, node 7 of the talk building model (nodes of 0xD0 bytes; its matrix
    // set to the identity moved 4.4 back) and the shown amount as money, 15 wide.
    void TalkMoney_Draw(GuestPtr<TalkMoney> money, int pass)
    {
        if (pass != 2)
            return;
        port::GuestFrame<0x40> frame;
        const GuestPtr<char> text{frame.ptr().address + 0x30u};
        DmaList_QueueClearZ();
        const GuestPtr<Vec4> matrix{port::load<uint32_t>(money->model->model.address + 8u) + 0x5B0u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        matrix[3].z = -4.4f;
        A3DObject_DrawNode(money->model, 7);
        Text_FormatMoney(text, money->shown, 15, 0);
        TextBox_SetText(money->text, text, 1);
        TextBox_Draw(money->text);
    }

    PORT_FN(TalkView_CursorPerson);
    PORT_FN(TalkView_FindPersonSlot);
    PORT_FN(TalkView_GetChoice);
    PORT_FN(TalkView_ToggleAnim);
    PORT_FN(TalkBldgNear_Active);
    PORT_FN(TalkOccupantNear_Active);
    PORT_FN(TalkPersonNear_Active);
    PORT_FN(TalkPersonNear_Draw);
    PORT_FN(TalkOccupantNear_ctor);
    PORT_FN(TalkOccupantFar_ctor);
    PORT_FN(TalkOccupantFar_Open);
    PORT_FN(TalkOccupantFar_Close);
    PORT_FN(TalkOccupantFar_Opening);
    PORT_FN(TalkOccupantFar_Opened);
    PORT_FN(TalkOccupantFar_Active);
    PORT_FN(TalkOccupantFar_Closing);
    PORT_FN(TalkOccupantFar_Closed);
    PORT_FN(TalkPersonFar_Open);
    PORT_FN(TalkPersonFar_Close);
    PORT_FN(TalkPersonFar_Opening);
    PORT_FN(TalkPersonFar_Opened);
    PORT_FN(TalkPersonFar_Active);
    PORT_FN(TalkPersonFar_Closing);
    PORT_FN(TalkPersonFar_Closed);
    PORT_FN(TalkPersonFar_Draw);
    PORT_FN(TalkPhoneFamily_Open);
    PORT_FN(TalkPhoneFamily_Close);
    PORT_FN(TalkPhoneFamily_Opening);
    PORT_FN(TalkPhoneFamily_Opened);
    PORT_FN(TalkPhoneFamily_Active);
    PORT_FN(TalkPhoneFamily_Closing);
    PORT_FN(TalkPhoneFamily_Closed);
    PORT_FN(TalkPhoneMember_ctor);
    PORT_FN(TalkPhoneMember_Open);
    PORT_FN(TalkPhoneMember_Close);
    PORT_FN(TalkPhoneMember_Opening);
    PORT_FN(TalkPhoneMember_Opened);
    PORT_FN(TalkPhoneMember_Active);
    PORT_FN(TalkPhoneMember_Closing);
    PORT_FN(TalkPhoneMember_Closed);
    PORT_FN(TalkMoney_Active);
    PORT_FN(TalkMoney_Draw);
    PORT_FN(Talk_LookupPose);
    PORT_FN(TalkView_CollectOccupants);
    PORT_FN(TalkView_BuildingCamera);
    PORT_FN(TalkView_PersonCamera);
    PORT_FN(Talk_ctor);
    PORT_FN(TalkView_ctor);
    PORT_FN(TalkView_DrawCursor);
    PORT_FN(TalkBldgFar_Open);
    PORT_FN(TalkBldgFar_Close);
    PORT_FN(TalkBldgFar_Opening);
    PORT_FN(TalkBldgFar_Opened);
    PORT_FN(Talk_Closed);
    PORT_FN(TalkView_HandleInput);
    PORT_FN(TalkBldgFar_Active);
    PORT_FN(TalkBldgFar_Closing);
    PORT_FN(TalkBldgFar_Closed);

    // ---- the person card (batch 39)

    // Opens the card for a person (6000: the family member `family`, `member`) at a place on the screen: the two
    // texts and the face frame move to the column from the table at 0x1B1B28; the friendship is kept as a float,
    // and for a person on the map the mood face shows how well the member is known: none when the member is not a
    // friend yet or their level is above 13, else 4 or 3 by the level when they are at stage 0 of both low and middle
    // status, 2 with a middle status, 1 with a low one.
    void TalkCard_Show(GuestPtr<TalkCard> card, int person, int family, int member, int slot)
    {
        Widget_Open(GuestPtr<Widget>::of(card->base));
        card->person = person;
        card->family = family;
        card->member = member;
        card->slot = slot;
        const int column = port::load<int32_t>(0x001B1B28u + 4u * static_cast<uint32_t>(card->slot));
        TextBox_SetPos(card->upperText, (column + 0x6C0) * 0x10, 0x7C00, -0x100000);
        TextBox_SetPos(card->lowerText, (column + 0x6C0) * 0x10, 0x7D60, -0x100000);
        Frame_SetRect(card->frame, column + 0xB0, 0x45, 0x18, 0xC);
        if (card->person == 6000)
        {
            card->friendship = fromInt(static_cast<int8_t>(Family_GetFriendship(card->family, card->member)));
            return;
        }
        const GuestPtr<MapPerson> record = card->city->map->people + card->person;
        card->friendship = fromInt(static_cast<int8_t>(Family_GetFriendship(record->family, record->member)));
        const GuestPtr<uint8_t> status{record.address + 0x2Fu};
        int level = StatusByte_LevelAbove13(status);
        if (Family_GetFriendship(record->family, record->member) == 0)
            level = 2;
        const uint32_t middle = StatusByte_Mid2(status);
        const uint8_t low = StatusByte_Low2(status);
        if (level == 2)
            card->mood = 0;
        else if (low != 0)
            card->mood = 1;
        else if (middle != 0)
            card->mood = 2;
        else
            card->mood = level == 0 ? 3 : 4;
    }

    PORT_FN(TalkCard_Show);

    // ---- widget state callbacks (batch 40)

    namespace
    {
        uint32_t widgetSlot(GuestPtr<Widget> widget, uint32_t offset) { return port::load<uint32_t>(widget->vtable.address + offset); }

        // The widget step (vtable +0x18) moves the timer by dt and says (1) when the state is over.
        int64_t widgetStep(GuestPtr<Widget> widget, float dt) { return port::callGuest<int64_t>(widgetSlot(widget, 0x18u), widget, dt); }

        // The conversation's status (its vtable +0x1C) as the session reads it.
        int64_t conversationStatus(GuestPtr<TalkSession> talk) { return port::callGuest<int64_t>(widgetSlot(talk->conversation, 0x1Cu), talk->conversation); }
    }

    // The four timed states of a plain widget just run the step by a whole second at once; the result is the step's.
    void Widget_Closed(GuestPtr<Widget> widget) { widgetStep(widget, 1.0f); }
    void Widget_Closing(GuestPtr<Widget> widget) { widgetStep(widget, 1.0f); }
    void Widget_Opened(GuestPtr<Widget> widget) { widgetStep(widget, 1.0f); }
    void Widget_Opening(GuestPtr<Widget> widget) { widgetStep(widget, 1.0f); }

    // The windows fade in and out by a tenth a frame: when the step ends they are open (state 3) or gone (0).
    void MsgWin_Closing(GuestPtr<MsgWin> msgwin)
    {
        if (widgetStep(GuestPtr<Widget>::of(msgwin->base), 0.1f) != 0)
            msgwin->base.state = 0;
    }

    void MsgWin_Opening(GuestPtr<MsgWin> msgwin)
    {
        if (widgetStep(GuestPtr<Widget>::of(msgwin->base), 0.1f) != 0)
            msgwin->base.state = 3;
    }

    void MenuWin_Opening(GuestPtr<MenuWin> menuwin)
    {
        if (widgetStep(GuestPtr<Widget>::of(menuwin->base), 0.1f) != 0)
            menuwin->base.state = 3;
    }

    void TalkMoney_Closing(GuestPtr<TalkMoney> money)
    {
        if (widgetStep(GuestPtr<Widget>::of(money->base), 0.1f) != 0)
            money->base.state = 0;
    }

    void TalkMoney_Opening(GuestPtr<TalkMoney> money)
    {
        if (widgetStep(GuestPtr<Widget>::of(money->base), 0.1f) != 0)
            money->base.state = 3;
    }

    // The menu window's update: a pressed choice runs the close (vtable +0x10).
    void MenuWin_Update(GuestPtr<MenuWin> menuwin)
    {
        if (ListMenu_HandleInput(menuwin->list, GuestPtr<int32_t>{menuwin.address + 0x18u}) != 0)
            port::callGuest<void>(widgetSlot(GuestPtr<Widget>::of(menuwin->base), 0x10u), menuwin);
    }

    // The talk session follows its conversation: while the conversation shows its message (state 6) the people
    // keep their idle animation going; status 4 asks the session to close (vtable +0x10), 3 or 6 mean the session
    // is open, 0 that the close can finish (a single target closes the view too, else the view's choice resets).
    void Talk_Active(GuestPtr<TalkSession> talk)
    {
        if (talk->conversation->state == 6)
            TalkView_ToggleAnim(talk->view);
        if (conversationStatus(talk) == 4)
            port::callGuest<void>(widgetSlot(GuestPtr<Widget>::of(talk->base), 0x10u), talk);
    }

    void Talk_Opened(GuestPtr<TalkSession> talk)
    {
        if (conversationStatus(talk) == 3 || conversationStatus(talk) == 6)
            talk->base.state = 3;
    }

    void Talk_Closing(GuestPtr<TalkSession> talk)
    {
        if (conversationStatus(talk) != 0)
            return;
        if (talk->singleTarget == 0)
        {
            talk->base.state = 6;
            talk->view->chosenSlot = -1;
        }
        else
        {
            port::callGuest<void>(widgetSlot(GuestPtr<Widget>::of(talk->view->base), 0x10u), talk->view);
            talk->base.state = 5;
        }
    }

    // ---- the near views opening and closing (batch 40)

    namespace
    {
        // The camera moves between the one the view opened with (eye +0x10, target +0x20) and the near one stored
        // after the base view (eye +0x270, target +0x280), blended by the widget timer t as (1 - t) * a + t * b:
        // opening from the first to the second, closing back (a and b swapped). The vectors are built in the
        // caller's 0x40-byte frame (eye at +0x20, target at +0x30), w 1.0; then the camera is set and updated and
        // the city's word +0x28 raised.
        void blendNearCamera(GuestPtr<TalkView> view, bool opening, uint32_t eyeAt, uint32_t targetAt)
        {
            const auto blend = [&](uint32_t out, uint32_t current, uint32_t dest) {
                for (uint32_t lane = 0; lane < 12; lane += 4)
                {
                    const ps2f t = view->base.timer;
                    const ps2f u = ps2f(1.0f) - t;
                    const ps2f from = port::load<float>((opening ? current : dest) + lane);
                    const ps2f to = port::load<float>((opening ? dest : current) + lane);
                    port::store<float>(out + lane, static_cast<float>(u * from + t * to));
                }
                port::store<uint32_t>(out + 12u, 0x3F800000u);
            };
            blend(eyeAt, view.address + 0x10u, view.address + 0x270u);
            blend(targetAt, view.address + 0x20u, view.address + 0x280u);
            cameraAt(view, GuestPtr<Vec4>{eyeAt}, GuestPtr<Vec4>{targetAt});
            view->city->unk_28 = 1;
        }

        int64_t stepNearView(GuestPtr<TalkView> view) { return widgetStep(GuestPtr<Widget>::of(view->base), 0.1f); }

        // A near view that closes hands the camera back and goes idle.
        void closeNearView(GuestPtr<TalkView> view)
        {
            port::GuestFrame<0x40> frame;
            blendNearCamera(view, false, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
            if (stepNearView(view) != 0)
            {
                view->city->unk_28 = 0;
                view->base.state = 0;
            }
        }

        void openNearView(GuestPtr<TalkView> view)
        {
            port::GuestFrame<0x40> frame;
            blendNearCamera(view, true, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
            stepNearView(view);
        }
    }

    void TalkBldgNear_Opening(GuestPtr<TalkView> view) { openNearView(view); }
    void TalkOccupantNear_Opening(GuestPtr<TalkView> view) { openNearView(view); }
    void TalkBldgNear_Closing(GuestPtr<TalkView> view) { closeNearView(view); }
    void TalkOccupantNear_Closing(GuestPtr<TalkView> view) { closeNearView(view); }
    void TalkPersonNear_Closing(GuestPtr<TalkView> view) { closeNearView(view); }

    // The person's near view is active as soon as the step ends.
    void TalkPersonNear_Opening(GuestPtr<TalkView> view)
    {
        port::GuestFrame<0x40> frame;
        blendNearCamera(view, true, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
        if (stepNearView(view) != 0)
            view->base.state = 3;
    }

    // My Home: opening marks the entrance once the step ends (if the home has its entrance flag); closing also
    // shows the HUD again.
    void MyHome_Opening(GuestPtr<TalkView> view)
    {
        port::GuestFrame<0x40> frame;
        blendNearCamera(view, true, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
        if (stepNearView(view) != 0 && (Map_GetRecord(view->city->map, view->targetObject)->flags & 0x20u) != 0)
            MyHome_MarkEntrance(view, 1);
    }

    void MyHome_Closing(GuestPtr<TalkView> view)
    {
        port::GuestFrame<0x40> frame;
        blendNearCamera(view, false, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
        view->city->hudVisible[0] = 1;
        if (stepNearView(view) != 0)
        {
            view->city->unk_28 = 0;
            view->base.state = 0;
        }
    }

    PORT_FN(Widget_Closed);
    PORT_FN(Widget_Closing);
    PORT_FN(Widget_Opened);
    PORT_FN(Widget_Opening);
    PORT_FN(MsgWin_Closing);
    PORT_FN(MsgWin_Opening);
    PORT_FN(MenuWin_Opening);
    PORT_FN(MenuWin_Update);
    PORT_FN(TalkMoney_Closing);
    PORT_FN(TalkMoney_Opening);
    PORT_FN(Talk_Active);
    PORT_FN(Talk_Opened);
    PORT_FN(Talk_Closing);
    PORT_FN(TalkBldgNear_Opening);
    PORT_FN(TalkOccupantNear_Opening);
    PORT_FN(TalkBldgNear_Closing);
    PORT_FN(TalkOccupantNear_Closing);
    PORT_FN(TalkPersonNear_Opening);
    PORT_FN(TalkPersonNear_Closing);
    PORT_FN(MyHome_Opening);
    PORT_FN(MyHome_Closing);

    // ---- the person shown in a talk view (batch 40)

    namespace
    {
        // After the person's draw: its first animation slot, when it has run out its time, returns to the idle animation.
        void idleAfterDraw(GuestPtr<TalkView> view)
        {
            if (ps2f(0.0f) == view->animTime[0] && view->anim[0] != 7)
                view->anim[0] = 7;
        }
    }

    // The person at record `person` is drawn in front of the camera at (0, 0.4, -3.65): the character class's draw slot
    // (vtable +0x14) with an identity matrix moved there, the default colour and the view's animation slots. A class with
    // flag 2 also gets the view's person model placed there first (and the person is then 0.15 lower).
    void TalkView_DrawPerson(GuestPtr<TalkView> view, int person)
    {
        port::GuestFrame<0xD0> frame; // the matrix at +0x90, its position row at +0xC0
        const uint32_t base = frame.ptr().address;
        const GuestPtr<MapPerson> who = view->city->map->people + person;
        if (!who)
            return;
        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * who->charType)};
        Dma_AppendCall(static_cast<int>(view.address + 0x50u));
        const GuestPtr<Vec4> matrix{base + 0x90u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        port::store<uint32_t>(base + 0xC0u, 0);
        port::store<uint32_t>(base + 0xC4u, 0x3ECCCCCDu);
        port::store<uint32_t>(base + 0xC8u, 0xC069999Au);
        port::store<uint32_t>(base + 0xCCu, 0x3F800000u);
        if ((cls->flags & 2u) != 0)
        {
            const uint32_t model = view->personModel.address;
            Mat4_Copy(GuestPtr<Vec4>{model + 0x10u}, GuestPtr<Vec4>{globals::kIdentityMatrix});
            port::store<uint32_t>(model + 0x40u, port::load<uint32_t>(base + 0xC0u)); // the position row of the matrix at +0x10
            port::store<uint32_t>(model + 0x44u, port::load<uint32_t>(base + 0xC4u));
            port::store<uint32_t>(model + 0x48u, port::load<uint32_t>(base + 0xC8u));
            Instance_DrawModel(GuestPtr<A3DObject>{view->personModel.address}, GuestPtr<Vec4>{globals::kDefaultColour}, 0, 0);
            port::store<float>(base + 0xC4u, static_cast<float>(ps2f(port::load<float>(base + 0xC4u)) - ps2f(0.15f)));
        }
        port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(cls.address + 0x20u) + 0x14u), cls.address, view->city.address, who.address, 0, 0,
                              matrix.address, globals::kDefaultColour, view.address + 0xD0u, view.address + 0x1D0u);
        idleAfterDraw(view);
    }

    // The same for a person turned by `angle` about y, at its own position.
    void TalkView_DrawPersonTurned(GuestPtr<TalkView> view, int person, ps2f angle)
    {
        port::GuestFrame<0xA0> frame; // the matrix at +0x60, its position row at +0x90
        const uint32_t base = frame.ptr().address;
        const GuestPtr<MapPerson> who = view->city->map->people + person;
        if (!who)
            return;
        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * who->charType)};
        Dma_AppendCall(static_cast<int>(view.address + 0x50u));
        sdk::sceVu0RotMatrixY(base + 0x60u, globals::kIdentityMatrix, angle);
        port::store<float>(base + 0x90u, static_cast<float>(who->x));
        port::store<float>(base + 0x94u, static_cast<float>(who->y));
        port::store<float>(base + 0x98u, static_cast<float>(who->z));
        port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(cls.address + 0x20u) + 0x14u), cls.address, view->city.address, who.address, 0, 0,
                              base + 0x60u, globals::kDefaultColour, view.address + 0xD0u, view.address + 0x1D0u);
        idleAfterDraw(view);
    }

    PORT_FN(TalkView_DrawPerson);
    PORT_FN(TalkView_DrawPersonTurned);

    // ---- the friendship gauge on the talk card (batch 40)

    // The card's friendship meter follows the real friendship of the person (or the family member by phone when
    // the person is 6000) towards it: up by 0.02 a frame below 1, else 0.1 (and down by 0.1), snapping on arrival. The
    // face frame takes the friendship band (style 0, or the band + 9). The gauge node of the building model (node 9) is then
    // laid out as a bar whose length is the meter within its band (below 1: nothing; 1..24 over 23, 24..48 over 24, 48..100
    // over 52, above 100 over 20), at the card's slot, 1.5 long and 1.3 high.
    void TalkCard_DrawGauges(GuestPtr<TalkCard> card)
    {
        int friendship, band;
        if (card->person == 6000)
        {
            friendship = static_cast<int8_t>(Family_GetFriendship(card->family, card->member));
            band = Family_GetFriendshipBand(card->family, card->member);
        }
        else
        {
            const GuestPtr<MapPerson> who = card->city->map->people + card->person;
            if (!who)
                return;
            friendship = static_cast<int8_t>(Family_GetFriendship(who->family, who->member));
            band = Family_GetFriendshipBand(who->family, who->member);
        }
        Frame_SetStyle(card->frame, band == 0 ? 0 : band + 9);
        const ps2f target = fromInt(friendship);
        const ps2f shown = card->friendship;
        if (!(target <= shown))
        {
            const ps2f step = shown < ps2f(1.0f) ? ps2f(0.02f) : ps2f(0.1f);
            card->friendship = target - shown <= step ? target : shown + step;
        }
        else
            card->friendship = shown - target <= ps2f(0.1f) ? target : shown - ps2f(0.1f);
        ps2f along = card->friendship;
        ps2f span = 1.0f;
        if (!(along < ps2f(1.0f)))
        {
            if (along < ps2f(24.0f))
            {
                along = along - span;
                span = 23.0f;
            }
            else if (along < ps2f(48.0f))
            {
                along = along - ps2f(24.0f);
                span = 24.0f;
            }
            else if (along < ps2f(100.0f))
            {
                along = along - ps2f(48.0f);
                span = 52.0f;
            }
            else
            {
                along = along - ps2f(100.0f);
                span = 20.0f;
            }
        }
        if (along < ps2f(0.01f))
            along = 0.0001f;
        const uint32_t node = port::load<uint32_t>(port::load<uint32_t>(card->model.address) + 8u) + 0x730u;
        Mat4_Copy(GuestPtr<Vec4>{node}, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const ps2f length = along / span;
        port::store<float>(node + 0x30u, port::load<float>(0x001B1B38u + 4u * static_cast<uint32_t>(card->slot)));
        port::store<uint32_t>(node + 0x34u, 0xBD79DB23u);
        port::store<uint32_t>(node + 0x38u, 0xC08CCCCDu);
        port::store<float>(node, static_cast<float>(length));
        port::store<float>(node, static_cast<float>(ps2f(port::load<float>(node)) * ps2f(1.5f)));
        port::store<float>(node + 0x14u, static_cast<float>(ps2f(port::load<float>(node + 0x14u)) * ps2f(1.3f)));
        A3DObject_DrawNode(GuestPtr<A3DObject>{card->model.address}, 9);
    }

    PORT_FN(TalkCard_DrawGauges);
}
