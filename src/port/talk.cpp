// Talks: the talk session and its views, and the WORDS script handlers.

#include "game_functions.h"
#include "game_globals.h"
#include "widgets.h"
#include "render.h"
#include "lifetime.h"
#include "sdk.h"
#include "vu0.h"

#include <bit>

namespace mm
{
    using packet::addressOf;

    namespace
    {
        // The place the talk views draw a person at: the matrix's last row (0, 0.4, -3.65, 1).
        void setTalkPlace(Vec4 &place)
        {
            place.x = 0.0f;
            place.y = std::bit_cast<float>(0x3ECCCCCDu); // 0.4
            place.z = std::bit_cast<float>(0xC069999Au); // -3.65
            place.w = 1.0f;
        }

        // A model put at a place: x, y and z of the matrix's last row (w is left).
        void copyPlace(Vec4 &to, const Vec4 &from)
        {
            to.x = from.x;
            to.y = from.y;
            to.z = from.z;
        }

        // The view's animation state and time of person slot `slot`, handed to a class's draw.
        uint32_t animSlots(GuestPtr<TalkView> view, int slot) { return (GuestPtr<int32_t>::of(view->anim[0]) + slot).address; }
        uint32_t animTimes(GuestPtr<TalkView> view, int slot) { return (GuestPtr<ps2f>::of(view->animTime[0]) + slot).address; }

        // The business card's x by its slot (the table at 0x1B1B38).
        float cardX(int32_t slot) { return GuestPtr<float>{0x001B1B38u}[slot]; }
    }

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
        talk->base.vtable = GuestPtr<WidgetVtable>{0x001BD770u};
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
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD450u};
        view->forcedChoice = -1;
        view->forceLit = 0;
        view->city = city;
        const GuestPtr<Camera> camera{view->city->cityCamera.address};
        view->eye = camera->eye;
        view->target = camera->target;
        view->buildingModel = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B2Cu)};
        view->personModel = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B7Cu)};
        view->color[0] = 0.5f;
        view->color[1] = 0.5f;
        view->color[2] = 0.5f;
        view->color[3] = 1.0f;
        // DMA packet sending the colour to VU1: REF tag to it, then STCYCL and UNPACK V4-32 to VU address 0x0F,
        // then a RET tag.
        const GuestPtr<uint32_t> packet = view.field(&TalkView::packet).as<uint32_t>();
        packet[0] = 0x30000001u;
        packet[1] = view.field(&TalkView::color).address & 0x7FFFFFFFu;
        packet[2] = 0x01000101u;
        packet[3] = 0x6C01000Fu;
        packet[4] = 0x60000000u;
        packet[5] = 0;
        packet[6] = 0;
        packet[7] = 0;
        for (int i = 0; i < 4; ++i)
        {
            view->frames[i] = GuestPtr<Frame>{0};
            view->nameBoxes[i] = GuestPtr<TextBox>{0};
        }
        view->cursorFrame = GuestPtr<Frame>{0};
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
        Frame_SetRect(view->cursorFrame, view->cursorX, 0x26, 0x18, 0xC);
        Dma_AppendCall(static_cast<int>(view->cursorFrame.address));
    }

    // The far view of a building (allocated 0x2A0 bytes) keeps the camera to return to: the eye in the base
    // view's last 16 bytes (farReturnEye, over targetObject and targetPerson) and the target in returnTarget. The
    // building is `building` (TalkFarView).
    namespace
    {
        GuestPtr<Vec4> farReturnEye(GuestPtr<TalkView> view) { return GuestPtr<Vec4>::of(view->farReturnEye); }
        GuestPtr<Vec4> farReturnTarget(GuestPtr<TalkView> view) { return GuestPtr<Vec4>::of(view.as<TalkFarView>()->returnTarget); }
        int farBuilding(GuestPtr<TalkView> view) { return view.as<TalkFarView>()->building; }
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
    // cursor over the three people in view, 16 pixels a frame for 10 frames, or past the edge scroll the view
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
                view->rowX = view->rowX + ps2f(0.03f);
            else
                view->rowX = view->rowX - ps2f(0.03f);
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
        const auto vtableOf = [](uint32_t widget) { return GuestPtr<Widget>{widget}->vtable; };
        const uint32_t view = talk->view.address;
        if (port::callGuest<int>(vtableOf(view)->getState, view) != 0)
            return;
        CityScene_PopMode(talk->city);
        talk->base.state = 0;
        if (const uint32_t conversation = talk->conversation.address)
        {
            port::callGuest<void>(vtableOf(conversation)->dtor, conversation, 1);
            talk->conversation = GuestPtr<Widget>{0};
        }
        if (const uint32_t remaining = talk->view.address)
        {
            port::callGuest<void>(vtableOf(remaining)->dtor, remaining, 1);
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

    // The camera for a talk at building `object`: the target at the middle of its footprint, half a unit above the
    // object's height (2.5 higher for type flag 0x10, a port: heights grow downwards); the eye at the target plus 0.4
    // back along the view's direction (its open target minus eye, flattened and normalized). The original's frame is
    // reserved whole and its vectors and matrix kept at their offsets.
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
        vf(4) = load(GuestPtr<Vec4>::of(view->target));
        vf(5) = load(GuestPtr<Vec4>::of(view->eye));
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
    // translation row reset): the shared one (2.5 higher for type flag 0x10) for an object still being built
    // (no flag 0x20), the type's own for a built one of a type with flag 0x4000. The target then takes the
    // eye's height. Other built ones use the person camera, which takes the target first.
    void TalkView_BuildingCamera(GuestPtr<TalkView> view, int object, GuestPtr<Vec4> eye, GuestPtr<Vec4> target)
    {
        using namespace vu0;
        const GuestPtr<MapObject> obj = Map_GetRecord(view->city->map, object);
        const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(obj->type)};
        const bool built = (obj->flags & 0x20u) != 0;
        if (built && (btype->flags & 0x4000u) == 0)
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
        if (!built)
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

    // For a talk at a building (building; 0x3F80, a talk with a family, has nothing to do): the building's residents
    // among the people listed at it, outside or already in it and of a class without flag 1, are sorted by
    // their member number, highest first, then each is unlinked from the building's tile and, when it was linked
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
            if (person->home != obj->id)
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

    // Occupant, person and phone views keep a camera of their own after the base view (TalkPersonView eye and target):
    // the far views return to it, the near views look through it. Person views draw the person (targetObject) turned by
    // the angle in facing.
    namespace
    {
        GuestPtr<Vec4> ownEye(GuestPtr<TalkView> view) { return GuestPtr<Vec4>::of(view.as<TalkPersonView>()->eye); }
        GuestPtr<Vec4> ownTarget(GuestPtr<TalkView> view) { return GuestPtr<Vec4>::of(view.as<TalkPersonView>()->target); }
        ps2f personAngle(GuestPtr<TalkView> view) { return view.as<TalkPersonView>()->facing; }

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

    // The near views look through their camera and clear the city's talkOverlay every frame; the near view of a
    // building also takes the cursor input.
    void TalkBldgNear_Active(GuestPtr<TalkView> view)
    {
        cameraOnObject(view, view->targetObject);
        view->city->talkOverlay = 0;
        TalkView_HandleInput(view);
    }

    void TalkOccupantNear_Active(GuestPtr<TalkView> view)
    {
        cameraOnObject(view, view->targetObject);
        view->city->talkOverlay = 0;
    }

    void TalkPersonNear_Active(GuestPtr<TalkView> view)
    {
        cameraAt(view, ownEye(view), ownTarget(view));
        view->city->talkOverlay = 0;
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
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD630u};
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
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD5E0u};
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
        cameraAt(view, view.field(&TalkView::eye), view.field(&TalkView::target));
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
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD4F0u};
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
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(money->model, 7));
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
        const int column = GuestPtr<int32_t>{0x001B1B28u}[card->slot]; // the card's screen column by slot
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
        const GuestPtr<uint8_t> status = record.field(&MapPerson::status);
        int level = TalkStatus_LongApart(status);
        if (Family_GetFriendship(record->family, record->member) == 0)
            level = 2;
        const uint32_t middle = TalkStatus_TalksToday(status);
        const uint8_t low = TalkStatus_RecentTalks(status);
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
        // The widget step moves the timer by dt and says (1) when the state is over.
        int64_t widgetStep(GuestPtr<Widget> widget, float dt) { return port::callGuest<int64_t>(widget->vtable->step, widget, dt); }

        // The conversation's state as the session reads it.
        int64_t conversationStatus(GuestPtr<TalkSession> talk) { return port::callGuest<int64_t>(talk->conversation->vtable->getState, talk->conversation); }
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

    // The menu window's update: a pressed choice runs the close slot.
    void MenuWin_Update(GuestPtr<MenuWin> menuwin)
    {
        if (ListMenu_HandleInput(menuwin->list, GuestPtr<int32_t>::of(menuwin->choice)) != 0)
            port::callGuest<void>(menuwin->base.vtable->close, menuwin);
    }

    // The talk session follows its conversation: while the conversation shows its message (state 6) the people
    // keep their idle animation going; status 4 asks the session to close (close slot), 3 or 6 mean the session
    // is open, 0 that the close can finish (a single target closes the view too, else the view's choice resets).
    void Talk_Active(GuestPtr<TalkSession> talk)
    {
        if (talk->conversation->state == 6)
            TalkView_ToggleAnim(talk->view);
        if (conversationStatus(talk) == 4)
            port::callGuest<void>(talk->base.vtable->close, talk);
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
            port::callGuest<void>(talk->view->base.vtable->close, talk->view);
            talk->base.state = 5;
        }
    }

    // ---- the near views opening and closing (batch 40)

    namespace
    {
        // The camera moves between the one the view opened with (eye, target) and the near one stored after the
        // base view (TalkPersonView eye and target), blended by the widget timer t as (1 - t) * a + t * b:
        // opening from the first to the second, closing back (a and b swapped). The vectors are built in the
        // caller's 0x40-byte frame (eye at sp + 0x20, target at sp + 0x30), w 1.0; then the camera is set and updated
        // and the city's talkOverlay raised.
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
                GuestPtr<Vec4>{out}->w = 1.0f;
            };
            blend(eyeAt, GuestPtr<Vec4>::of(view->eye).address, ownEye(view).address);
            blend(targetAt, GuestPtr<Vec4>::of(view->target).address, ownTarget(view).address);
            cameraAt(view, GuestPtr<Vec4>{eyeAt}, GuestPtr<Vec4>{targetAt});
            view->city->talkOverlay = 1;
        }

        int64_t stepNearView(GuestPtr<TalkView> view) { return widgetStep(GuestPtr<Widget>::of(view->base), 0.1f); }

        // A near view that closes hands the camera back and goes idle.
        void closeNearView(GuestPtr<TalkView> view)
        {
            port::GuestFrame<0x40> frame;
            blendNearCamera(view, false, frame.ptr().address + 0x20u, frame.ptr().address + 0x30u);
            if (stepNearView(view) != 0)
            {
                view->city->talkOverlay = 0;
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

    // My Home: opening marks the entrance once the step ends (if the home is built, flag 0x20); closing also
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
            view->city->talkOverlay = 0;
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
    // (drawAt slot) with an identity matrix moved there, the default colour and the view's animation slots. A class with
    // flag 2 also gets the view's person model placed there first (and the person is then 0.15 higher).
    void TalkView_DrawPerson(GuestPtr<TalkView> view, int person)
    {
        port::GuestFrame<0xD0> frame;
        const GuestPtr<MapPerson> who = view->city->map->people + person;
        if (!who)
            return;
        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * who->charType)};
        Dma_AppendCall(static_cast<int>(GuestPtr<uint8_t>::of(view->packet[0]).address));
        const GuestPtr<Vec4> matrix{frame.ptr().address + 0x90u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        setTalkPlace(matrix[3]);
        if ((cls->flags & 2u) != 0)
        {
            const GuestPtr<A3DObject> model = view->personModel;
            Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{globals::kIdentityMatrix});
            copyPlace(model->matrix[3], matrix[3]);
            Instance_DrawModel(view->personModel, GuestPtr<Vec4>{globals::kDefaultColour}, 0, 0);
            matrix[3].y = static_cast<float>(ps2f(matrix[3].y) - ps2f(0.15f));
        }
        port::callGuest<void>(cls->vtable->drawAt, cls.address, view->city.address, who.address, 0, 0,
                              matrix.address, globals::kDefaultColour, animSlots(view, 0), animTimes(view, 0));
        idleAfterDraw(view);
    }

    // The same for a person turned by `angle` about y, at its own position.
    void TalkView_DrawPersonTurned(GuestPtr<TalkView> view, int person, ps2f angle)
    {
        port::GuestFrame<0xA0> frame;
        const GuestPtr<MapPerson> who = view->city->map->people + person;
        if (!who)
            return;
        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * who->charType)};
        Dma_AppendCall(static_cast<int>(GuestPtr<uint8_t>::of(view->packet[0]).address));
        const GuestPtr<Vec4> matrix{frame.ptr().address + 0x60u};
        sdk::sceVu0RotMatrixY(matrix.address, globals::kIdentityMatrix, angle);
        matrix[3].x = static_cast<float>(who->x);
        matrix[3].y = static_cast<float>(who->y);
        matrix[3].z = static_cast<float>(who->z);
        port::callGuest<void>(cls->vtable->drawAt, cls.address, view->city.address, who.address, 0, 0,
                              matrix.address, globals::kDefaultColour, animSlots(view, 0), animTimes(view, 0));
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
        const GuestPtr<Vec4> matrix = worldMatrix(modelNode(GuestPtr<A3DObject>{card->model.address}, 9)); // the gauge
        Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
        const ps2f length = along / span;
        matrix[3].x = cardX(card->slot);
        port::store<uint32_t>(addressOf(matrix[3].y), 0xBD79DB23u);
        port::store<uint32_t>(addressOf(matrix[3].z), 0xC08CCCCDu);
        matrix[0].x = length; // the gauge's length as its x scale
        matrix[0].x = matrix[0].x * ps2f(1.5f);
        matrix[1].y = matrix[1].y * ps2f(1.3f);
        A3DObject_DrawNode(GuestPtr<A3DObject>{card->model.address}, 9);
    }

    PORT_FN(TalkCard_DrawGauges);

    // ---- Destructors (plan P6 batch 2): the widget vtable goes back to the base class's, the owned text boxes and
    // frames are deleted, then the widget base destructor and the delete ----

    GuestPtr<TalkMoney> TalkMoney_dtor(GuestPtr<TalkMoney> money, int16_t deleteFlag)
    {
        if (money)
        {
            money->base.vtable = GuestPtr<WidgetVtable>{0x001BD7C0u};
            lifetime::deleteTextBox(money.field(&TalkMoney::text).address);
            Widget_dtor(GuestPtr<Widget>{money.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(money.address);
        }
        return money;
    }

    GuestPtr<TalkCard> TalkCard_dtor(GuestPtr<TalkCard> card, int16_t deleteFlag)
    {
        if (card)
        {
            card->base.vtable = GuestPtr<WidgetVtable>{0x001BD720u};
            lifetime::deleteTextBox(card.field(&TalkCard::lowerText).address);
            lifetime::deleteTextBox(card.field(&TalkCard::upperText).address);
            lifetime::deleteFrame(card.field(&TalkCard::frame).address);
            Widget_dtor(GuestPtr<Widget>{card.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(card.address);
        }
        return card;
    }

    // The conversation and the view delete themselves through their vtables (the destructor slot).
    GuestPtr<TalkSession> Talk_dtor(GuestPtr<TalkSession> talk, int16_t deleteFlag)
    {
        if (talk)
        {
            talk->base.vtable = GuestPtr<WidgetVtable>{0x001BD770u};
            lifetime::deleteVirtual(talk.field(&TalkSession::conversation).address, 8);
            lifetime::deleteVirtual(talk.field(&TalkSession::view).address, 8);
            Widget_dtor(GuestPtr<Widget>{talk.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(talk.address);
        }
        return talk;
    }

    // Four name boxes (nameBoxes) and four frames (frames), then the cursor frame (cursorFrame).
    GuestPtr<TalkView> TalkView_dtor(GuestPtr<TalkView> view, int16_t deleteFlag)
    {
        if (view)
        {
            view->base.vtable = GuestPtr<WidgetVtable>{0x001BD450u};
            for (uint32_t i = 0; i < 4u; ++i)
            {
                lifetime::deleteTextBox((view.field(&TalkView::nameBoxes) + i).address);
                lifetime::deleteFrame((view.field(&TalkView::frames) + i).address);
            }
            lifetime::deleteFrame(view.field(&TalkView::cursorFrame).address);
            Widget_dtor(GuestPtr<Widget>{view.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(view.address);
        }
        return view;
    }

    PORT_FN(TalkMoney_dtor);
    PORT_FN(TalkCard_dtor);
    PORT_FN(Talk_dtor);
    PORT_FN(TalkView_dtor);

    // ---- Talk view people count (plan P6 batch 2) ----

    // How many people the view lists: a family's member count, or the people of the building (those outside it or on its
    // way in or out count when they are not "keep clear" characters, the ones inside only when inside this building).
    void TalkView_CountPeople(GuestPtr<TalkView> view)
    {
        if (view->building == 0x3F80)
        {
            view->peopleCount = Family_Get(view->family, 0)->memberCount;
            return;
        }
        const GuestPtr<MapObject> object = Map_GetRecord(view->city->map, view->building);
        port::GuestLocal<uint16_t> cursor;
        *cursor = object->firstPerson;
        view->peopleCount = 0;
        while (const GuestPtr<MapPerson> person = People_Next(view->city->map, cursor))
        {
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) != 0)
                continue;
            if (person->inside == 0x3F80 || static_cast<int32_t>(person->inside) == view->building)
                view->peopleCount = view->peopleCount + 1;
        }
    }

    PORT_FN(TalkView_CountPeople);

    // ---- The talk views' destructors (plan P6 batch 2) ----

    namespace
    {
        // Each view class sets its own vtable (at +8), then runs the base view's destructor without deleting.
        GuestPtr<TalkView> talkViewDtor(GuestPtr<TalkView> view, int16_t deleteFlag, uint32_t vtable)
        {
            if (view)
            {
                view->base.vtable = GuestPtr<WidgetVtable>{vtable};
                TalkView_dtor(view, 0);
                if (0 < deleteFlag)
                    sdk::operator_delete(view.address);
            }
            return view;
        }
    }

    GuestPtr<TalkView> TalkPhoneFamily_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD4A0u); }
    GuestPtr<TalkView> TalkPhoneMember_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD4F0u); }
    GuestPtr<TalkView> TalkBldgFar_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD540u); }
    GuestPtr<TalkView> TalkBldgNear_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD590u); }
    GuestPtr<TalkView> TalkOccupantFar_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD5E0u); }
    GuestPtr<TalkView> TalkOccupantNear_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD630u); }
    GuestPtr<TalkView> TalkPersonFar_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD680u); }
    GuestPtr<TalkView> TalkPersonNear_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD6D0u); }
    GuestPtr<TalkView> MyHome_dtor(GuestPtr<TalkView> view, int16_t deleteFlag) { return talkViewDtor(view, deleteFlag, 0x001BD810u); }

    // The conversation window: its business card, menu window and money panel (card, menu, panel) and the name entry
    // (nameEntry) delete themselves through their vtables, then the message window below it.
    GuestPtr<TalkConv> TalkConv_dtor(GuestPtr<TalkConv> conv, int16_t deleteFlag)
    {
        if (conv)
        {
            conv->base.base.vtable = GuestPtr<WidgetVtable>{0x001BD860u};
            lifetime::deleteVirtual(conv.field(&TalkConv::card).address, 8);
            lifetime::deleteVirtual(conv.field(&TalkConv::menu).address, 8);
            lifetime::deleteVirtual(conv.field(&TalkConv::panel).address, 8);
            lifetime::deleteVirtual(conv.field(&TalkConv::nameEntry).address, 0x120);
            MsgWin_dtor(GuestPtr<MsgWin>{conv.address}, 0);
            if (0 < deleteFlag)
                sdk::operator_delete(conv.address);
        }
        return conv;
    }

    PORT_FN(TalkPhoneFamily_dtor);
    PORT_FN(TalkPhoneMember_dtor);
    PORT_FN(TalkBldgFar_dtor);
    PORT_FN(TalkBldgNear_dtor);
    PORT_FN(TalkOccupantFar_dtor);
    PORT_FN(TalkOccupantNear_dtor);
    PORT_FN(TalkPersonFar_dtor);
    PORT_FN(TalkPersonNear_dtor);
    PORT_FN(MyHome_dtor);
    PORT_FN(TalkConv_dtor);

    // The money window: the amount is shown (shown) and kept (amount), the talk building model is shared (model, from
    // 0x1F3B2C) and a text box of 8 columns, 1 row and style -2 (text) is made, placed at (0x8040, 0x85A0, -0x100000) with the
    // character size 0, 0, 0xB0, 0.
    GuestPtr<TalkMoney> TalkMoney_ctor(GuestPtr<TalkMoney> money, int amount)
    {
        Widget_ctor(GuestPtr<Widget>{money.address});
        money->base.vtable = GuestPtr<WidgetVtable>{0x001BD7C0u};
        money->amount = amount;
        money->shown = amount;
        money->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B2Cu)};
        const uint32_t box = sdk::operator_new(0x40u);
        money->text = box != 0u ? TextBox_Init(GuestPtr<TextBox>{box}, 8, 1, -2) : GuestPtr<TextBox>{0u};
        TextBox_SetPos(money->text, 0x8040, 0x85A0, -0x100000);
        TextBox_SetCharSize(money->text, 0, 0, 0xB0, 0);
        return money;
    }

    PORT_FN(TalkMoney_ctor);

    // The business card window: the city, the shared building model (model, from 0x1F3B2C), a text box of 9 by 6 (lowerText),
    // one of 14 by 2 (upperText) and a frame.
    GuestPtr<TalkCard> TalkCard_ctor(GuestPtr<TalkCard> card, GuestPtr<CityScene> city)
    {
        const uint32_t c = card.address;
        const auto newBox = [](int columns, int rows) {
            const uint32_t memory = sdk::operator_new(0x40u);
            return memory != 0u ? TextBox_Init(GuestPtr<TextBox>{memory}, columns, rows, -1).address : 0u;
        };
        Widget_ctor(GuestPtr<Widget>{c});
        card->base.vtable = GuestPtr<WidgetVtable>{0x001BD720u};
        card->city = city;
        card->model = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B2Cu)};
        card->lowerText = GuestPtr<TextBox>{newBox(9, 6)};
        card->upperText = GuestPtr<TextBox>{newBox(0xE, 2)};
        const uint32_t memory = sdk::operator_new(0xC0u);
        uint32_t frame = 0u;
        if (memory != 0u)
            frame = Frame_ctor(GuestPtr<Frame>{memory}, 0, 0, 0, 0x18, 0xC, 0x10000000u).address;
        card->frame = GuestPtr<Frame>{frame};
        return card;
    }

    PORT_FN(TalkCard_ctor);

    // Sets what the talk view shows (building `building`, 0x3F80 for a family, and `family`): four name boxes (12 by 1) each with
    // a frame, the cursor frame (style 0x33 at 100, 100), the shared person model (awayModel, from 0x1F3B40), the row's x
    // (rowX, -0.3) and the scroll and idle state cleared, the cursor's x (cursorX) at 0x78; then the people are
    // counted.
    void TalkView_SetTarget(GuestPtr<TalkView> view, int building, int family)
    {
        view->building = building;
        view->family = family;
        for (uint32_t i = 0; i < 4u; ++i)
        {
            const uint32_t box = sdk::operator_new(0x40u);
            view->nameBoxes[i] = box != 0u ? TextBox_Init(GuestPtr<TextBox>{box}, 0xC, 1, -1) : GuestPtr<TextBox>{0u};
            const uint32_t frame = sdk::operator_new(0xC0u);
            view->frames[i] = frame != 0u ? Frame_ctor(GuestPtr<Frame>{frame}, 0, 0, 0, 0x18, 0xC, 0x10000000u) : GuestPtr<Frame>{0u};
        }
        const uint32_t cursor = sdk::operator_new(0xC0u);
        view->cursorFrame = cursor != 0u ? Frame_ctor(GuestPtr<Frame>{cursor}, 0x33, 0x64, 0x64, 0x18, 0xC, 0x10000000u) : GuestPtr<Frame>{0u};
        view->awayModel = GuestPtr<A3DObject>{port::load<uint32_t>(0x001F3B40u)};
        view->rowX = std::bit_cast<float>(0xBE99999Au);
        view->idleTime = 0.0f;
        view->scrollMode = 0;
        view->scrollDir = 0;
        view->scrollStep = 0;
        view->cursorSlot = 0;
        view->choiceSlot = 0;
        view->cursorX = 0x78;
        view->peopleCount = 0;
        view->firstShown = 0;
        TalkView_CountPeople(view);
    }

    PORT_FN(TalkView_SetTarget);

    // ---- The talk views' constructors (plan P6 batch 2) ----

    // By family: the family is kept as the target and shown from the start (the building is 0x3F80, meaning none).
    GuestPtr<TalkView> TalkPhoneFamily_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int family)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD4A0u};
        view->targetObject = family;
        TalkView_SetTarget(view, 0x3F80, family);
        return view;
    }

    // A building seen close up: the camera for the building's person, the building shown and its occupants collected.
    GuestPtr<TalkView> TalkBldgNear_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int object)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD590u};
        view->targetObject = object;
        TalkView_PersonCamera(view, object, ownTarget(view), ownEye(view));
        TalkView_SetTarget(view, view->targetObject, 0);
        TalkView_CollectOccupants(view);
        return view;
    }

    // A building seen from far away: the building (TalkFarView building) shown, its occupants collected, and the city camera's eye and target
    // kept to return to.
    GuestPtr<TalkView> TalkBldgFar_ctor(GuestPtr<TalkView> view, GuestPtr<CityScene> city, int object)
    {
        TalkView_ctor(view, city);
        view->base.vtable = GuestPtr<WidgetVtable>{0x001BD540u};
        view.as<TalkFarView>()->building = object;
        TalkView_SetTarget(view, farBuilding(view), 0);
        TalkView_CollectOccupants(view);
        const GuestPtr<Camera> camera{city->cityCamera.address};
        view->farReturnEye = camera->eye;
        view.as<TalkFarView>()->returnTarget = camera->target;
        return view;
    }

    // The conversation window: the message window below it, the city, the menu window (menu) and the business card
    // window (card), with no money panel (panel) or name entry (nameEntry) yet.
    GuestPtr<TalkConv> TalkConv_ctor(GuestPtr<TalkConv> conv, GuestPtr<CityScene> city)
    {
        const uint32_t c = conv.address;
        MsgWin_ctor(GuestPtr<MsgWin>{c});
        conv->base.base.vtable = GuestPtr<WidgetVtable>{0x001BD860u};
        conv->city = city;
        uint32_t memory = sdk::operator_new(0x20u);
        if (memory != 0u)
            memory = MenuWin_ctor(GuestPtr<MenuWin>{memory}).address;
        conv->menu = GuestPtr<MenuWin>{memory};
        memory = sdk::operator_new(0x38u);
        if (memory != 0u)
            memory = TalkCard_ctor(GuestPtr<TalkCard>{memory}, city).address;
        conv->card = GuestPtr<TalkCard>{memory};
        conv->panel = GuestPtr<Widget>{0u};
        conv->nameEntry = GuestPtr<NameEntry>{0u};
        return conv;
    }

    PORT_FN(TalkPhoneFamily_ctor);
    PORT_FN(TalkBldgNear_ctor);
    PORT_FN(TalkBldgFar_ctor);
    PORT_FN(TalkConv_ctor);

    // The texts of the business card: the upper box gets the member's names and the two-digit number of the member record; the
    // lower box the word for the friendship tier (the meter, as an integer: up to 0, 23, 47 and 99 are tiers 0 to 3, above that 4),
    // the member's line in the table at 0x1F3CE8 (by character type and variant) and, for the phone card, a closing text, or
    // for the others the one of the card's mood (0 to 4).
    void TalkCard_FillText(GuestPtr<TalkCard> card)
    {
        port::GuestFrame<0xD0> frame;
        const uint32_t line = frame.ptr().address + 0x60u, number = frame.ptr().address + 0xC8u;
        uint32_t charType, variant;
        GuestPtr<FamilyMember> member;
        if (card->person == 6000)
        {
            const GuestPtr<FamilyMember> who = GuestPtr<FamilyMember>::of(Family_Get(card->family, 0)->members[0]) + card->member;
            charType = who->charType;
            variant = who->look;
            member = who;
        }
        else
        {
            const GuestPtr<MapPerson> person = card->city->map->people + card->person;
            Map_GetRecord(card->city->map, person->home);
            const GuestPtr<Family> family = Family_Get(person->family, 0);
            charType = person->charType;
            member = GuestPtr<FamilyMember>::of(family->members[0]) + person->member;
            variant = person->flags & 3u;
        }
        const int friendship = sdk::fptosi(card->friendship);
        const uint32_t tier = friendship <= 0 ? 0u : (friendship < 0x18 ? 1u : (friendship < 0x30 ? 2u : (friendship < 100 ? 3u : 4u)));
        Text_FormatPadded(GuestPtr<char>{number}, member->age, 2);
        sdk::sprintf(line, 0x001B9048u, GuestPtr<char>::of(member->name2[0]).address, GuestPtr<char>::of(member->name[0]).address, number);
        TextBox_SetText(card->upperText, GuestPtr<char>{line}, 0);
        sdk::sprintf(line, 0x001B9058u, port::load<uint32_t>(0x001B1B10u + 4u * tier));
        const uint32_t table = port::load<uint32_t>(0x001F3CE8u);
        sdk::sprintf(line + static_cast<uint32_t>(sdk::strlen(line)), 0x001B9058u, table + port::load<uint16_t>(table + 2u * (variant + charType * 3u)));
        if (card->person == 6000)
            sdk::strcat(line, 0x001B9060u);
        else
        {
            constexpr uint32_t kMoodText[] = {0x001B9080u, 0x001B90A0u, 0x001B90B8u, 0x001B90D0u, 0x001B90E8u};
            if (card->mood >= 0 && card->mood <= 4)
                sdk::strcat(line, kMoodText[card->mood]);
        }
        TextBox_SetText(card->lowerText, GuestPtr<char>{line}, 0);
    }

    PORT_FN(TalkCard_FillText);

    // ---- The talk views' draw functions (plan P6 batch 2) ----
    //
    // Each draws in pass 2 only: the near views while the view is active (state 3), the far and phone views in the states 2 to 4.
    // The building (or the family, by the second argument) is drawn first, then what the view shows: the people and the cursor,
    // or the one person or member the view is about.

    namespace
    {
        bool drawsNow(GuestPtr<TalkView> view, int pass, bool nearView)
        {
            if (pass != 2)
                return false;
            const int32_t state = view->base.state;
            return nearView ? state == 3 : (state >= 2 && state < 5);
        }
    }

    void TalkOccupantNear_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, true))
        {
            TalkView_DrawBuilding(view, 0, view->targetObject);
            TalkView_DrawPerson(view, view->targetPerson);
        }
    }

    void TalkBldgNear_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, true))
        {
            TalkView_DrawBuilding(view, 0, view->targetObject);
            TalkView_DrawPeople(view);
            TalkView_DrawCursor(view);
        }
    }

    void TalkOccupantFar_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, false))
        {
            TalkView_DrawBuilding(view, 0, view->targetObject);
            TalkView_DrawPerson(view, view->targetPerson);
        }
    }

    void TalkPhoneFamily_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, false))
        {
            TalkView_DrawBuilding(view, 1, view->targetObject);
            TalkView_DrawPeople(view);
            TalkView_DrawCursor(view);
        }
    }

    void TalkPhoneMember_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, false))
        {
            TalkView_DrawBuilding(view, 1, view->targetObject);
            TalkView_DrawMember(view, view->targetObject, view->targetPerson);
        }
    }

    void TalkBldgFar_Draw(GuestPtr<TalkView> view, int pass)
    {
        if (drawsNow(view, pass, false))
        {
            TalkView_DrawBuilding(view, 0, farBuilding(view));
            TalkView_DrawPeople(view);
            TalkView_DrawCursor(view);
        }
    }

    PORT_FN(TalkOccupantNear_Draw);
    PORT_FN(TalkBldgNear_Draw);
    PORT_FN(TalkOccupantFar_Draw);
    PORT_FN(TalkPhoneFamily_Draw);
    PORT_FN(TalkPhoneMember_Draw);
    PORT_FN(TalkBldgFar_Draw);

    // ---- The talk session and the card's drawing (plan P6 batch 2) ----

    // Updates the view and the conversation window through their vtables, lets the view's scroll mode 3 (scrollMode) end when
    // the conversation says it is idle, then updates the session itself; in its state 6 the session takes the input
    // (mm_Talk_ChooseInput) and the view stops choosing (choosing 0), in any other state the view is choosing (1). The state is
    // returned.
    int Talk_Update(GuestPtr<TalkSession> talk)
    {
        const uint32_t t = talk.address;
        const auto callSlot = [](uint32_t object, uint32_t WidgetVtable::*slot) {
            return port::callGuest<int64_t>(widgetMethod(object, slot), object);
        };
        if (talk->view.address != 0u)
            callSlot(talk->view.address, &WidgetVtable::update);
        const uint32_t conv = talk->conversation.address;
        if (conv != 0u)
        {
            callSlot(conv, &WidgetVtable::update);
            if (talk->view->scrollMode == 3 && callSlot(talk->conversation.address, &WidgetVtable::getState) == 0)
                talk->view->scrollMode = 0;
        }
        const int state = Widget_Update(GuestPtr<Widget>{t});
        if (state == 6)
            Talk_ChooseInput(talk);
        if (talk->view.address != 0u)
            talk->view->choosing = state == 6 ? 0 : 1;
        return state;
    }

    // The business card, drawn in pass 2 while it is showing (state 3): its model's matrix (the node at +0x670 of the model's
    // data) is the standard one put at the card's place (x from the table at 0x1B1B38 by the card's slot, -0.0608 and -4.4),
    // stretched 1.5 across and 1.3 up, the model's node 8 is drawn, then the texts, the gauges and the frame.
    void TalkCard_Draw(GuestPtr<TalkCard> card, int pass)
    {
        if (pass != 2 || card->base.state != 3)
            return;
        const uint32_t node = worldMatrix(modelNode(card->model, 8)).address;
        Mat4_Copy(GuestPtr<Vec4>{node}, GuestPtr<Vec4>{0x001B2550u});
        const GuestPtr<Vec4> place = GuestPtr<Vec4>{node} + 3;
        place->x = cardX(card->slot);
        place->y = std::bit_cast<float>(0xBD79DB23u); // -0.0608
        place->z = std::bit_cast<float>(0xC08CCCCDu); // -4.4
        const auto scale = [&](uint32_t offset, ps2f factor) {
            const ps2f value = ps2f(std::bit_cast<float>(port::load<uint32_t>(node + offset))) * factor;
            port::store<uint32_t>(node + offset, std::bit_cast<uint32_t>(value.v));
        };
        scale(0, ps2f(std::bit_cast<float>(0x3FC00000u)));
        scale(0x14, ps2f(std::bit_cast<float>(0x3FA66666u)));
        A3DObject_DrawNode(GuestPtr<A3DObject>{card->model.address}, 8);
        TalkCard_FillText(card);
        TextBox_Draw(card->upperText);
        TextBox_Draw(card->lowerText);
        DmaList_QueueClearZ();
        TalkCard_DrawGauges(card);
        Dma_AppendCall_14C200(static_cast<int>(card->frame.address));
    }

    PORT_FN(Talk_Update);
    PORT_FN(TalkCard_Draw);

    // Whether everyone is asleep (schedule activity 5, the night hours): for a family (`byFamily` 1, `target` the family) every
    // member; for a building the people on its tile that count (not in a vehicle class, outside or in this view's building).
    // 1 when all are.
    int TalkView_AllAsleep(GuestPtr<TalkView> view, int byFamily, int target)
    {
        port::GuestFrame<0x60> frame;
        if (byFamily == 1)
        {
            const GuestPtr<Family> family = Family_Get(target, 0);
            for (int i = 0; i < family->memberCount; ++i)
                if (Schedule_MemberActivity(view->city, static_cast<uint32_t>(target), i) != 5)
                    return 0;
            return 1;
        }
        const GuestPtr<CityMap> map = view->city->map;
        const GuestPtr<uint16_t> cursor{frame.ptr().address + 0x5Eu};
        *cursor = Map_GetRecord(map, target)->firstPerson;
        while (const GuestPtr<MapPerson> person = People_Next(map, cursor))
        {
            const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * person->charType)};
            if ((cls->flags & 1u) != 0)
                continue;
            if (person->inside != 0x3F80 && static_cast<int32_t>(person->inside) != view->building)
                continue;
            if (person->activity != 5)
                return 0;
        }
        return 1;
    }

    PORT_FN(TalkView_AllAsleep);

    // The input of the choosing state: the Triangle button (0x10; unless a choice is already forced in the view, forcedChoice) or a talk
    // that must end at once (endChoosing) closes the talk and its view and ends it (state 5). The Cross button (0x40) or a forced choice
    // takes the choice the view reports: for a building it is a person (whose family and member are noted) and the conversation
    // starts about them, for a family it is a member. The view goes to mode 3 and the talk to state 2.
    void Talk_ChooseInput(GuestPtr<TalkSession> talk)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t choiceAt = frame.ptr().address + 0x2Cu;
        const auto closeSlot = [](uint32_t object) {
            port::callGuest<int64_t>(widgetMethod(object, &WidgetVtable::close), object);
        };
        const bool forced = talk->view->forcedChoice >= 0;
        if (talk->endChoosing != 0 || (!forced && (Pad_Pressed(0) & 0x10u) != 0))
        {
            closeSlot(talk.address);
            closeSlot(talk->view.address);
            talk->base.state = 5;
            return;
        }
        if (!forced && (Pad_Pressed(0) & 0x40u) == 0)
            return;
        if (TalkView_GetChoice(talk->view, GuestPtr<uint32_t>{choiceAt}, talk.field(&TalkSession::slot).as<uint32_t>()) == 0)
            return;
        if (talk->building == 0x3F80)
        {
            talk->member = port::load<int32_t>(choiceAt);
            TalkConv_Start(GuestPtr<TalkConv>{talk->conversation.address}, 1, talk->family, talk->member, talk->slot, talk->arg5);
        }
        else
        {
            talk->person = port::load<int32_t>(choiceAt);
            const GuestPtr<MapPerson> person = talk->city->map->people + talk->person;
            talk->family = person->family;
            talk->member = person->member;
            TalkConv_Start(GuestPtr<TalkConv>{talk->conversation.address}, 0, talk->person, talk->building, talk->slot, talk->arg5);
        }
        talk->view->scrollMode = 3;
        talk->base.state = 2;
    }

    PORT_FN(Talk_ChooseInput);

    // Starts the conversation window on a person (`byFamily` 0: `target` is the person, `buildingOrMember` the building they are at)
    // or on a family member (`byFamily` 1: the family and the member). The script text is loaded (mm_Words_LoadForPerson or
    // mm_Words_LoadForFamily) and kept in script, the business card is shown for the speaker, the window's own record of the
    // speaker is kept (person, with peopleInside set when the building has people inside; family and member for a member, with
    // peopleInside 1), and the message `slot` is set with the speaker's character class sound.
    void TalkConv_Start(GuestPtr<TalkConv> conv, int byFamily, int target, int buildingOrMember, int slot, int arg5)
    {
        const uint32_t c = conv.address;
        conv->ended = 0;
        conv->typedAnswer = 0;
        conv->byFamily = byFamily;
        conv->arg5 = arg5;
        const GuestPtr<CityScene> city{conv->city.address};
        uint32_t charType;
        if (byFamily == 0)
        {
            const GuestPtr<MapPerson> person = city->map->people + target;
            conv->script = Words_LoadForPerson(person, arg5, city);
            conv->person = target;
            const GuestPtr<MapObject> where = Map_GetRecord(conv->city->map, buildingOrMember);
            conv->peopleInside = (globals::buildingType(where->type)->flags & 0x40u) != 0 ? 1 : 0;
            TalkCard_Show(conv->card, conv->person, 0, 0, slot);
            charType = person->charType;
        }
        else
        {
            conv->script = Words_LoadForFamily(target, buildingOrMember, arg5, conv->city);
            TalkCard_Show(conv->card, 6000, target, buildingOrMember, slot);
            charType = (GuestPtr<FamilyMember>::of(Family_Get(target, 0)->members[0]) + buildingOrMember)->charType;
            conv->family = target;
            conv->member = buildingOrMember;
            conv->peopleInside = 1;
        }
        MsgWin_SetMessage(GuestPtr<MsgWin>{c}, slot, GuestPtr<CharClass>{port::load<uint32_t>(globals::charClassTable.get() + 4u * charType)}->voice);
    }

    PORT_FN(TalkConv_Start);

    // Updates the conversation: the money panel (panel), the name entry (nameEntry; in state 8 a finished entry, result 2, is stored at the
    // table 0x1C5380 for the codes below -0x7A8 or checked as the typed answer of the script, then closed, back to state 3 and the
    // next line), the business card (card) and the message window below. When the window reports 7 (a menu asked for) and the
    // menu (menu) has closed, its choice (the last entry when none) is applied to the script (mm_Words_ApplyChoice), the state
    // goes back to 3 with the next line, and the speaker is given the player's money. The window's result is returned.
    int TalkConv_Update(GuestPtr<TalkConv> conv)
    {
        const uint32_t c = conv.address;
        const auto callSlot = [](uint32_t object) { return port::callGuest<int64_t>(widgetMethod(object, &WidgetVtable::update), object); };
        if (conv->panel.address != 0u)
            callSlot(conv->panel.address);
        if (conv->nameEntry.address != 0u && conv->base.base.state == 8)
        {
            if (NameEntry_Update(conv->nameEntry) == 2)
            {
                const int32_t code = conv->code;
                // Codes below -0x7A8 take the typed name into the stage state's names[code + 0x7D0].
                if (code < -0x7A8)
                    sdk::strcpy(globals::kStageState + (static_cast<uint32_t>(code + 0x7D0) << 4),
                                conv->nameEntry.field(&NameEntry::name).address);
                else
                    Words_MatchTypedAnswer(conv->script, conv->nameEntry.field(&NameEntry::name));
                NameEntry_Close(conv->nameEntry);
                conv->base.base.state = 3;
                TalkConv_NextLine(conv);
            }
        }
        callSlot(conv->card.address);
        // MsgWin_Update in its typing state hands back the s0 it was called with, which in the original is this conversation.
        const int32_t callerS0 = port::guestS0();
        port::setGuestS0(static_cast<int32_t>(c));
        const int result = MsgWin_Update(GuestPtr<MsgWin>{c});
        port::setGuestS0(callerS0);
        if (result != 7)
            return result;
        const uint32_t menu = conv->menu.address;
        if (callSlot(menu) != 0)
            return result;
        int32_t choice = GuestPtr<MenuWin>{menu}->choice;
        if (choice == -1)
            choice = Words_CountChoices(conv->script, conv->city, conv->code) - 1;
        Words_ApplyChoice(conv->script, conv->city, conv->code, choice);
        conv->code = 0;
        conv->base.base.state = 3;
        TalkConv_NextLine(conv);
        if (conv->panel.address != 0u)
            conv->panel.as<TalkMoney>()->amount = globals::stageState()->money;
        return result;
    }

    PORT_FN(TalkConv_Update);

    // Reads the next piece of the script into the message window's buffer (text) and acts on what the script returned (code): at
    // the end of a conversation (-0x1777) the speaker's friendship goes up once (by the speaker's class bits 21 to 23 plus 4,
    // and not when the speaker is asleep, activity 5) and the end flag (ended) is set; a menu with no choice (-0x7D5) sets
    // the end flag too and -0x7D1 sets the typed answer flag (typedAnswer). The text is then shown (mm_MsgWin_NextLine).
    void TalkConv_NextLine(GuestPtr<TalkConv> conv)
    {
        const uint32_t c = conv.address;
        if (conv->base.base.state != 3)
            return;
        const int code = Words_Step(GuestPtr<int32_t>{static_cast<uint32_t>(conv->script)}, GuestPtr<char>{c + 0x2E8u});
        conv->code = code;
        if (code == -0x1777)
        {
            if (conv->typedAnswer == 0)
            {
                const GuestPtr<CityScene> city{conv->city.address};
                const int32_t arg5 = conv->arg5;
                const auto points = [](uint32_t charType) {
                    return static_cast<int>((port::load<uint32_t>(port::load<uint32_t>(globals::charClassTable.get() + 4u * charType) + 0x1Cu) & 0xE00000u) >> 21) + 4;
                };
                if (conv->byFamily == 0)
                {
                    const GuestPtr<MapPerson> person = city->map->people + conv->person;
                    const bool asleep = arg5 != 2 && arg5 != 3 && conv->peopleInside != 0 && person->activity == 5;
                    if (!asleep)
                        TalkStatus_AddFriendshipAfterBreak(person->family, person->member, GuestPtr<uint8_t>::of(person->status), points(person->charType),
                                             GuestPtr<uint32_t>{port::guest(&city->approval)});
                }
                else
                {
                    const int family = conv->family, member = conv->member;
                    if (arg5 == 3 || Schedule_MemberActivity(city, static_cast<uint32_t>(family), member) != 5)
                    {
                        const uint32_t record = Family_Get(family, 0).address;
                        MemberStatus_AddFriendshipAfterBreak(family, member, points((GuestPtr<FamilyMember>{record + 0x24u} + member)->charType),
                                               static_cast<int>(port::guest(&city->approval)));
                    }
                }
                conv->ended = 1;
            }
        }
        else if (code == -0x7D5)
            conv->ended = 1;
        else if (code == -0x7D1)
            conv->typedAnswer = 1;
        MsgWin_NextLine(GuestPtr<MsgWin>{c}, GuestPtr<char>{c + 0x2E8u});
    }

    PORT_FN(TalkConv_NextLine);

    // Draws a family member in the phone view: the draw packet of the view is appended, the local matrix is the standard one with
    // its translation at (0, 0.4, -3.6); for a character class with flag 2 the view's person model gets the same place and is drawn
    // with the default colour, and the local matrix rises by 0.15 (y less 0.15); the class's own drawAt slot then
    // draws the member with the matrix, the animation state (anim) and times (animTime) of the view; a first animation time of 0
    // puts the first slot to the idle animation 7.
    void TalkView_DrawMember(GuestPtr<TalkView> view, int family, int member)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Family> record = Family_Get(family, 0);
        if (!record)
            return;
        const GuestPtr<CharClass> cls{port::load<uint32_t>(globals::charClassTable.get() + 4u * record->members[member].charType)};
        Dma_AppendCall(static_cast<int>(GuestPtr<uint8_t>::of(view->packet[0]).address));
        const GuestPtr<Vec4> matrix{sp + 0xA0u};
        Mat4_Copy(matrix, GuestPtr<Vec4>{0x001B2550u});
        setTalkPlace(matrix[3]);
        if ((cls->flags & 2u) != 0)
        {
            const GuestPtr<A3DObject> model = view->personModel;
            Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{0x001B2550u});
            copyPlace(model->matrix[3], matrix[3]);
            Instance_DrawModel(view->personModel, GuestPtr<Vec4>{0x001B26C0u}, 0, 0);
            const ps2f dropped = ps2f(matrix[3].y) - ps2f(std::bit_cast<float>(0x3E19999Au));
            matrix[3].y = dropped.v;
        }
        port::callGuest<void>(cls->vtable->drawAt, cls.address, view->city, 0, family, member, matrix.address, 0x001B26C0u,
                              animSlots(view, 0), animTimes(view, 0));
        if (view->animTime[0] == ps2f(0.0f) && view->anim[0] != 7)
            view->anim[0] = 7;
    }

    PORT_FN(TalkView_DrawMember);

    // The conversation window closing: once its close call (the step slot, by 0.1) is done the money panel (panel)
    // is deleted and the state goes back to 0. The speaker's friendship status then moves on, unless the speaker was asleep (activity 5
    // for a person; the schedule says 5 for a member of a family): when the conversation ended well (the end flag, ended, or a
    // change the script made) the status byte advances (mm_TalkStatus_RecordTalk or mm_MemberStatus_RecordTalk), otherwise a status level
    // of 14 or more drops back to 13.
    void TalkConv_Closing(GuestPtr<TalkConv> conv)
    {
        const uint32_t c = conv.address;
        if (port::callGuest<int64_t>(conv->base.base.vtable->step, conv, ps2f(0.1f)) == 0)
            return;
        lifetime::deleteVirtual(c + 0x2D8u, 8);
        conv->base.base.state = 0;
        const int32_t arg5 = conv->arg5;
        const auto lowerLevel = [](GuestPtr<uint8_t> status) {
            if (((*status >> 4) & 0xFu) >= 0xE)
                *status = static_cast<uint8_t>((*status & 0x0Fu) | 0xD0u);
        };
        const GuestPtr<CityScene> city{conv->city.address};
        if (conv->byFamily == 0)
        {
            const GuestPtr<MapPerson> person = city->map->people + conv->person;
            if (arg5 != 2 && arg5 != 3 && conv->peopleInside != 0 && person->activity == 5)
                return;
            if (conv->ended != 0 || Words_FriendshipChanged() != 0)
                TalkStatus_RecordTalk(GuestPtr<uint8_t>::of(person->status));
            else
                lowerLevel(GuestPtr<uint8_t>::of(person->status));
            return;
        }
        if (arg5 != 3 && Schedule_MemberActivity(city, static_cast<uint32_t>(conv->family), conv->member) == 5)
            return;
        if (conv->ended != 0 || Words_FriendshipChanged() != 0)
            MemberStatus_RecordTalk(conv->family, conv->member);
        else
            lowerLevel(MemberStatus_Lookup(conv->family, conv->member));
    }

    PORT_FN(TalkConv_Closing);

    // Starts a talk (once: a session already running answers 0) and makes the view for it. `byFamily` 0 talks to a building
    // (target) and the person (member, 6000 for none), 1 to a family (target) and its member (6 = the whole family): the family
    // and the member of a building talk are those of the person; a family talk looks up the family's home and the member's entry on
    // the map when the family is in the city. The view depends on whether the building is within 20 of the camera (the distance
    // in the ground plane, from the camera's eye to the building's centre; a talk with no building counts as near) and what is
    // talked to: a My Home type gets the home view, a building the building view, a person the person view (a person inside
    // a building the occupant view) in their near or far versions, a whole family the family phone view, a member the member
    // view. The conversation window is made too, the HUD flags of the city (hudVisible[1] to [3]) go off and the poses are cleared.
    // The result is 1.
    int Talk_Start(GuestPtr<TalkSession> talk, int byFamily, int target, int member, int arg5, GuestPtr<CityScene> city)
    {
        using namespace vu0;
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        if (talk->base.state != 0)
            return 0;
        talk->city = city;
        talk->arg5 = arg5;
        talk->slot = 1;
        talk->endChoosing = 0;
        int isMyHome = 0;
        if (byFamily == 0)
        {
            talk->building = target;
            talk->person = member;
            if (talk->person == 0x1770)
            {
                talk->family = 0;
                talk->member = 6;
            }
            else
            {
                const GuestPtr<MapPerson> person = city->map->people + talk->person;
                talk->family = person->family;
                talk->member = person->member;
            }
            const GuestPtr<MapObject> object = Map_GetRecord(city->map, talk->building);
            const GuestPtr<BuildingType> btype{globals::buildingTypeAddress(object->type)};
            talk->peopleInside = (btype->flags & 0x40u) != 0 ? 1 : 0;
            isMyHome = BldgType_IsMyHome(btype);
        }
        else
        {
            talk->building = 0x3F80;
            talk->person = 0x1770;
            talk->family = target;
            talk->member = member;
            talk->peopleInside = 1;
            if (Family_InCity(static_cast<uint32_t>(talk->family)) != 0)
            {
                const GuestPtr<CityMap> map = talk->city->map;
                talk->building = Map_FindFamilyHome(map, static_cast<uint16_t>(static_cast<uint32_t>(talk->family)))->id;
                if (talk->member != 6)
                {
                    const GuestPtr<CityMap> inCity = talk->city->map;
                    const GuestPtr<MapPerson> entry = Map_FindPersonEntry(inCity, static_cast<int16_t>(static_cast<uint32_t>(talk->family)),
                                                                          static_cast<uint16_t>(talk->member));
                    talk->person = entry->index;
                }
            }
        }
        bool isNear = true;
        if (talk->building != 0x3F80)
        {
            const GuestPtr<CityScene> inside{talk->city.address};
            const uint32_t camera = inside->cityCamera.field(&CityCamera::base).field(&Camera::eye).address;
            const GuestPtr<MapObject> object = Map_GetRecord(inside->map, talk->building);
            const ps2f size = fromInt(globals::buildingType(object->type)->size);
            const uint32_t tile = object->tile;
            const ps2f centreX = ((fromInt(static_cast<int32_t>(tile & 0x7Fu)) - ps2f(64.0f)) + size / ps2f(2.0f));
            const ps2f centreZ = ((fromInt(0x80 - static_cast<int32_t>(tile >> 7)) - ps2f(64.0f)) - size / ps2f(2.0f));
            port::store<uint32_t>(sp + 0x70u, port::load<uint32_t>(camera));
            port::store<uint32_t>(sp + 0x80u, std::bit_cast<uint32_t>(centreX.v));
            port::store<uint32_t>(sp + 0x78u, port::load<uint32_t>(camera + 8u));
            port::store<uint32_t>(sp + 0x88u, std::bit_cast<uint32_t>(centreZ.v));
            vf(4) = load(GuestPtr<Vec4>{sp + 0x80u});
            vf(5) = load(GuestPtr<Vec4>{sp + 0x70u});
            set(vf(6), sub(vf(5), vf(4)), XYZW);
            set(vf(7), mul(vf(6), vf(6)), XYZ);
            set(vf(8), add(vf(7), lane(vf(7), 2)), X);
            q() = sqrtQ(vu0::x(vf(8)));
            set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
            store(GuestPtr<Vec4>{sp + 0x90u}, vf(9));
            isNear = ps2f(std::bit_cast<float>(port::load<uint32_t>(sp + 0x90u))) <= ps2f(20.0f);
        }
        // The view: made, built by the constructor from the session's fields, and kept in view.
        const auto makeView = [&](uint32_t size, auto construct) {
            uint32_t view = sdk::operator_new(size);
            if (view != 0u)
                view = construct(view);
            talk->view = GuestPtr<TalkView>{view};
        };
        const auto cityNow = [&]() { return talk->city; };
        if (byFamily == 0)
        {
            if (talk->peopleInside != 0)
            {
                if (talk->person == 0x1770)
                {
                    if (isMyHome != 0)
                        makeView(0x2A0u, [&](uint32_t view) { return MyHome_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->building).address; });
                    else if (isNear)
                        makeView(0x290u, [&](uint32_t view) { return TalkBldgNear_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->building).address; });
                    else
                        makeView(0x290u, [&](uint32_t view) { return TalkBldgFar_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->building).address; });
                    talk->singleTarget = 0;
                }
                else
                {
                    if (isNear)
                        makeView(0x2A0u, [&](uint32_t view) { return TalkOccupantNear_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->building, talk->person).address; });
                    else
                        makeView(0x2A0u, [&](uint32_t view) { return TalkOccupantFar_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->building, talk->person).address; });
                    talk->singleTarget = 1;
                }
            }
            else
            {
                if (isNear)
                    makeView(0x2A0u, [&](uint32_t view) { return TalkPersonNear_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->person).address; });
                else
                    makeView(0x2A0u, [&](uint32_t view) { return TalkPersonFar_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->person).address; });
                talk->singleTarget = 1;
            }
        }
        else if (talk->member == 6)
        {
            makeView(0x270u, [&](uint32_t view) { return TalkPhoneFamily_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->family).address; });
            talk->singleTarget = 0;
        }
        else
        {
            makeView(0x2A0u, [&](uint32_t view) { return TalkPhoneMember_ctor(GuestPtr<TalkView>{view}, cityNow(), talk->family, talk->member).address; });
            talk->singleTarget = 1;
        }
        const uint32_t view = talk->view.address;
        port::callGuest<void>(widgetMethod(view, &WidgetVtable::open), view);
        uint32_t conversation = sdk::operator_new(0x508u);
        if (conversation != 0u)
            conversation = TalkConv_ctor(GuestPtr<TalkConv>{conversation}, cityNow()).address;
        talk->conversation = GuestPtr<Widget>{conversation};
        Widget_Open(talk.as<Widget>());
        city->hudVisible[1] = 0;
        city->hudVisible[2] = 0;
        city->hudVisible[3] = 0;
        for (uint32_t pose = 0; pose < 0x20; ++pose)
            talk->poses[pose].family = 0;
        return 1;
    }

    PORT_FN(Talk_Start);

    // The conversation's state for what the script asked for (the code in code). A code from -6000 up to -2001 is a menu of choices
    // (mm_Words_CountChoices of them, listed in the menu window, menu, state 7); -2001 also opens the money panel (panel, made when
    // missing). A code from -2000 up is a name entry (state 8): the entry window (nameEntry, made when missing) is set up with a slot of
    // the answer table at 0x1C5380 for codes below -1960, else with the script's default answer copied to 0x1F4E90 without its
    // trailing blanks (the longest answer being that of the default). Below -6000 the line is shown and waits for a button: Triangle
    // (when the script allows cancelling) or, at the end of the conversation (-0x1777 in state 3), Cross closes the panel, the card
    // and the conversation; any other Cross goes on to the next line.
    void TalkConv_Active(GuestPtr<TalkConv> conv)
    {
        const uint32_t c = conv.address;
        const auto callSlot = [](uint32_t object, uint32_t WidgetVtable::*slot) { port::callGuest<int64_t>(widgetMethod(object, slot), object); };
        const auto closeAll = [&]() {
            if (conv->panel.address != 0u)
                callSlot(conv->panel.address, &WidgetVtable::close);
            callSlot(conv->card.address, &WidgetVtable::close);
            callSlot(c, &WidgetVtable::close);
        };
        const int32_t code = conv->code;
        const int32_t script = conv->script;
        const GuestPtr<CityScene> city{conv->city.address};
        if (code < -6000)
        {
            if (Words_CancelAllowed() != 0 && (Pad_Pressed(0) & 0x10u) != 0)
            {
                closeAll();
                return;
            }
            if ((Pad_Pressed(0) & 0x40u) == 0)
                return;
            if (conv->base.base.state == 3 && conv->code == -0x1777)
                closeAll();
            else
                TalkConv_NextLine(conv);
            return;
        }
        if (code >= -2000)
        {
            if (conv->nameEntry.address == 0u)
            {
                uint32_t entry = sdk::operator_new(0x124u);
                if (entry != 0u)
                    entry = NameEntry_ctor(GuestPtr<NameEntry>{entry}).address;
                conv->nameEntry = GuestPtr<NameEntry>{entry};
            }
            const GuestPtr<NameEntry> entry{conv->nameEntry.address};
            if (conv->code < -0x7A8)
                NameEntry_Setup(entry, 0, 0xF, 0, 0, GuestPtr<char>{0x001C5380u + (static_cast<uint32_t>(conv->code + 0x7D0) << 4)});
            else
            {
                constexpr uint32_t kBuffer = 0x001F4E90u, kBlank = 0x001B90F8u;
                const GuestPtr<char> text = Words_GetEntryDefault();
                sdk::strcpy(kBuffer, text.address);
                for (uint32_t last = kBuffer + static_cast<uint32_t>(sdk::strlen(kBuffer)) - 1u; last >= kBuffer; --last)
                {
                    if (sdk::strncmp(last, kBlank, 1) != 0)
                        break;
                    port::store<uint8_t>(last, 0);
                }
                const uint32_t mode = static_cast<uint32_t>(Words_GetEntryMode()) & 0xFFu;
                NameEntry_Setup(entry, mode, static_cast<uint8_t>(sdk::strlen(text.address)), 0, 0, GuestPtr<char>{kBuffer});
            }
            conv->base.base.state = 8;
            return;
        }
        const int count = Words_CountChoices(script, city, code);
        conv->base.base.state = 7;
        const GuestPtr<MenuWin> menu{conv->menu.address};
        MenuWin_Open(menu, conv->code, count);
        for (int index = 0; index < count; ++index)
            MenuWin_FillItems(menu, Words_GetChoiceText(script, city, conv->code, index));
        if (conv->code != -0x7D1)
            return;
        if (conv->panel.address == 0u)
        {
            uint32_t panel = sdk::operator_new(0x1Cu);
            if (panel != 0u)
                panel = TalkMoney_ctor(GuestPtr<TalkMoney>{panel}, port::load<int32_t>(0x001C5540u)).address;
            conv->panel = GuestPtr<Widget>{panel};
        }
        callSlot(conv->panel.address, &WidgetVtable::open);
    }

    PORT_FN(TalkConv_Active);

    // The building shown in a talk view. `byFamily` 0: `target` is a building (nothing is drawn when its record lacks flag 0x20),
    // 1: a family, whose home type is used. The type's draw check (its isOpen slot: the type, the city and the
    // record, 0 for a family) says whether to draw; with no yes and the view not forced lit (forceLit) the type's node 0x150 is
    // shown dimmed and nobody-at-home aware. Otherwise: type flag 0x8000 draws the type's built mesh (business model) with its
    // own rotation and position between the two light packets; 0x4000 (by family only) shows node 0x150 and else node 0x14F, each
    // dimmed (0x1B1B00) when nobody is home and untinted (0x1B26C0) otherwise, its matrix reset to the camera spot (0, -0.16, -3.84).
    // The screen's Z is then cleared (mm_DmaList_QueueClearZ).
    void TalkView_DrawBuilding(GuestPtr<TalkView> view, int byFamily, int target)
    {
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<CityScene> city = view->city;
        const auto slot = [&](uint32_t type, uint32_t record) {
            return port::callGuest<int>(GuestPtr<BuildingType>{type}->vtable->isOpen, type, city.address, record);
        };
        uint32_t type;
        int shown;
        if (byFamily == 0)
        {
            const GuestPtr<MapObject> record = Map_GetRecord(city->map, target);
            type = globals::buildingTypeAddress(record->type);
            shown = slot(type, record.address);
            if ((record->flags & 0x20u) == 0)
                return;
        }
        else
        {
            type = globals::buildingTypeAddress(Family_Get(target, 0)->homeType);
            shown = slot(type, 0);
        }
        const auto drawNode = [&](uint32_t node) {
            const GuestPtr<A3DObject> model{view->buildingModel.address};
            const bool allAsleep = TalkView_AllAsleep(view, byFamily, target) != 0;
            A3DObject_ChangeLuminosity(model, GuestPtr<uint32_t>{allAsleep ? 0x001B1B00u : 0x001B26C0u}, static_cast<int>(node));
            const GuestPtr<Vec4> matrix = worldMatrix(modelNode(model, static_cast<int>(node)));
            Mat4_Copy(matrix, GuestPtr<Vec4>{globals::kIdentityMatrix});
            matrix[3].x = 0.0f;
            matrix[3].y = std::bit_cast<float>(0xBE23D70Au); // -0.16
            matrix[3].z = std::bit_cast<float>(0xC075C28Fu); // -3.84
            A3DObject_DrawNode(model, static_cast<int>(node));
        };
        const GuestPtr<BuildingType> btype{type};
        const uint32_t flags = btype->flags;
        if (shown == 0 && view->forceLit == 0)
            drawNode(btype->modelNode);
        else if ((flags & 0x8000u) != 0)
        {
            // The finished building's mesh at the type's talk eye offset, turned by its w.
            sdk::sceVu0RotMatrixY(sp + 0x60u, globals::kIdentityMatrix, btype->talkEye.w);
            const GuestPtr<Vec4> place{sp + 0x90u};
            place->x = btype->talkEye.x;
            place->y = btype->talkEye.y;
            place->z = btype->talkEye.z;
            Packet2_Draw(GuestPtr<uint32_t>{city->lights.address}, 1);
            Instance_DrawMesh(globals::businessModel.get(), GuestPtr<Vec4>{sp + 0x60u}, GuestPtr<Vec4>{globals::kDefaultColour},
                              btype->builtMesh, 1);
            Packet2_Draw(GuestPtr<uint32_t>{city->lights.address}, 0);
        }
        else if ((flags & 0x4000u) != 0)
        {
            if (byFamily == 1)
                drawNode(btype->modelNode);
        }
        else
            drawNode(btype->openNode);
        DmaList_QueueClearZ();
    }

    PORT_FN(TalkView_DrawBuilding);

    // The people of a talk view, in a row from the view's scroll position (rowX) at 0.3 apart, at height 0.4 and depth
    // -3.65: the members of the family when the view is by family (building is 0x3F80), else the people of the building
    // that count (a class without flag 1, outside or in this building). Those left of -0.8 are only counted past;
    // drawing goes on while the position is at most 0.8. Someone who is away (activity or schedule 5) is the view's
    // second model (awayModel) swaying with a phase of its own (dimmed when nobody is home); anyone else is drawn by
    // the character class's drawAt slot turned about y by the position, in the default colour for the highlighted slot
    // (the chosen one, else the cursor's) and a grey for the rest, with the view's person model first for a class with
    // flag 2 (and the person then 0.15 higher). Up to four of them (and none left of -1) are counted for the name boxes
    // (nameBoxes) and frames (frames); unless the view is choosing, each gets its name (the member's two name fields,
    // name2 then name, with a separator) on a row that alternates, and a friendship frame of the member's band + 9 when
    // it has one. The original's frame is reserved whole.
    void TalkView_DrawPeople(GuestPtr<TalkView> view)
    {
        using namespace vu0;
        port::GuestFrame<0x140> frame;
        const uint32_t sp = frame.ptr().address;
        const GuestPtr<Vec4> matrix{sp + 0xB0u}, pos{sp + 0xF0u}, colour{sp + 0x100u};
        Vec4 &row3 = matrix[3];
        constexpr uint32_t kDimAway = 0x001B1AD0u, kDimPerson = 0x001B1AE0u, kSeparator = 0x001B8FF8u;
        const ps2f step = std::bit_cast<float>(0x3E99999Au); // 0.3
        const ps2f leftEdge = std::bit_cast<float>(0xBF4CCCCDu), rightEdge = std::bit_cast<float>(0x3F4CCCCDu);
        const ps2f cullEdge = -1.0f;

        pos->x = view->rowX;
        pos->y = std::bit_cast<float>(0x3ECCCCCDu); // 0.4
        pos->z = std::bit_cast<float>(0xC069999Au); // -3.65
        pos->w = 1.0f;
        int highlighted = view->chosenSlot;
        if (highlighted == -1)
            highlighted = view->cursorSlot;
        int slot = 0, drawn = 0;
        bool allAsleep = false;

        const auto posX = [&] { return ps2f(pos->x); };
        const auto advance = [&] {
            ++slot;
            pos->x = (posX() + step).v;
        };
        // The screen x for a label at the position: 9000 * (x + 0.3) rounded towards zero to a multiple of 16, plus `bias`.
        const auto screenX = [&](int bias) {
            const int scaled = sdk::fptosi(ps2f(9000.0f) * (step + posX()));
            return scaled / 16 * 16 + bias;
        };
        const auto classOf = [&](uint32_t charType) { return GuestPtr<CharClass>{port::load<uint32_t>(globals::charClassTable.get() + 4u * charType)}; };

        // Someone away: the second model at the position, swaying with a phase of the view's idle time plus a number of frames from the
        // seed (the square root of its float through VU0, whose bits modulo 90 give the frames).
        const auto drawAway = [&](int seed, uint32_t slotAt) {
            const GuestPtr<A3DObject> model = view->awayModel;
            Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{globals::kIdentityMatrix});
            copyPlace(model->matrix[3], *pos);
            port::store<int32_t>(slotAt, seed);
            vf(4) = load(GuestPtr<Vec4>{slotAt});
            set(vf(4), itof0(vf(4)), XYZW);
            q() = sqrtQ(x(vf(4)));
            set(vf(9), add(vf(0), _mm_set1_ps(q())), XYZW);
            store(GuestPtr<Vec4>{slotAt}, vf(9));
            ps2f phase = view->idleTime + fromInt(port::load<int32_t>(slotAt) % 90);
            if (!(phase < ps2f(180.0f)))
                phase = phase - ps2f(180.0f);
            A3D_AnimateIfRoom(view->awayModel, 0, phase);
            Instance_DrawModel(view->awayModel, GuestPtr<Vec4>{allAsleep ? kDimAway : globals::kDefaultColour}, 0, 0);
        };
        // Someone there: the class draws it (for a family member the person is 0 and the family and member are given, for a person on
        // the map it is the record).
        const auto drawNormal = [&](GuestPtr<CharClass> cls, uint32_t person, int family, int member) {
            sdk::sceVu0RotMatrixY(matrix.address, globals::kIdentityMatrix, posX());
            row3 = *pos;
            if (slot == highlighted)
                *colour = *GuestPtr<Vec4>{globals::kDefaultColour};
            else
            {
                colour->x = std::bit_cast<float>(0x3F4CCCCDu); // 0.8
                colour->y = std::bit_cast<float>(0x3F4CCCCDu);
                colour->z = std::bit_cast<float>(0x3F4CCCCDu);
                colour->w = 0.0f;
            }
            if ((cls->flags & 2u) != 0)
            {
                const GuestPtr<A3DObject> model = view->personModel;
                Mat4_Copy(GuestPtr<Vec4>::of(model->matrix[0]), GuestPtr<Vec4>{globals::kIdentityMatrix});
                copyPlace(model->matrix[3], *pos);
                Instance_DrawModel(model, GuestPtr<Vec4>{allAsleep ? kDimPerson : globals::kDefaultColour}, 0, 0);
                const ps2f lowered = ps2f(row3.y) - ps2f(std::bit_cast<float>(0x3E19999Au));
                row3.y = lowered.v;
            }
            port::callGuest<void>(cls->vtable->drawAt, cls.address, view->city.address, person, family, member,
                                  matrix.address, colour.address, animSlots(view, slot), animTimes(view, slot));
            if (view->animTime[slot] == ps2f(0.0f) && view->anim[slot] != 7)
                view->anim[slot] = 7;
        };
        // The name of the member at `record` in the next name box, and its friendship frame when the band is not 0.
        const auto drawLabel = [&](uint32_t record, int row, int depth, int family, int member) {
            const GuestPtr<TextBox> box = view->nameBoxes[drawn];
            const GuestPtr<FamilyMember> who{record};
            TextBox_SetPos(box, screenX(0x7060), (row + 0x790) * 16, depth);
            TextBox_SetText(box, GuestPtr<char>::of(who->name2[0]), 0);
            TextBox_AppendText(box, GuestPtr<char>{kSeparator}, 0);
            TextBox_AppendText(box, GuestPtr<char>::of(who->name[0]), 0);
            TextBox_Draw(box);
            const int band = Family_GetFriendshipBand(family, member);
            if (band != 0)
            {
                const GuestPtr<Frame> frameObject = view->frames[drawn];
                const int left = screenX(0x6EE0), top = (row + 0x790) * 16;
                Frame_SetCorners(frameObject, left, top, left + 0x180, top + 0xC0);
                Frame_SetStyle(frameObject, band + 9);
                Dma_AppendCall_14C200(static_cast<int>(frameObject.address));
            }
        };

        if (view->building == 0x3F80)
        {
            allAsleep = TalkView_AllAsleep(view, 1, view->family) != 0;
            const GuestPtr<Family> family = Family_Get(view->family, 0);
            int member = 0;
            while (posX() < leftEdge)
            {
                if (!(member < family->memberCount))
                    return;
                ++member;
                advance();
            }
            while (posX() <= rightEdge)
            {
                if (!(member < family->memberCount))
                    return;
                const uint32_t record = (family.field(&Family::members) + static_cast<int32_t>(member)).address;
                const GuestPtr<CharClass> cls = classOf(port::load<uint16_t>(record));
                if (Schedule_MemberActivity(view->city, static_cast<uint32_t>(view->family), member) == 5)
                    drawAway(family->memberCount + 6 * view->family, sp + 0x110u);
                else
                    drawNormal(cls, 0, view->family, member);
                if (!(posX() < cullEdge) && drawn < 4)
                {
                    if (view->choosing == 0)
                        drawLabel(record, member % 2 == 0 ? 0xAA : 0xBA, -0x100000, view->family, member);
                    ++drawn;
                    ++member;
                }
                advance();
            }
            return;
        }

        allAsleep = TalkView_AllAsleep(view, 0, view->building) != 0;
        const GuestPtr<CityMap> map = view->city->map;
        const GuestPtr<uint16_t> cursor{sp + 0x13Eu};
        *cursor = Map_GetRecord(map, view->building)->firstPerson;
        int seen = 0;
        const auto counts = [&](GuestPtr<MapPerson> person, GuestPtr<CharClass> cls) {
            return (cls->flags & 1u) == 0 && (person->inside == kNoObject || static_cast<int32_t>(person->inside) == view->building);
        };
        while (posX() < leftEdge)
        {
            const GuestPtr<MapPerson> person = People_Next(map, cursor);
            if (!person)
                return;
            if (counts(person, classOf(person->charType)))
            {
                ++seen;
                advance();
            }
        }
        while (posX() <= rightEdge)
        {
            const GuestPtr<MapPerson> person = People_Next(map, cursor);
            if (!person)
                return;
            const GuestPtr<CharClass> cls = classOf(person->charType);
            if (!counts(person, cls))
                continue;
            ++seen;
            if (person->activity == 5)
                drawAway(person->index, sp + 0x120u);
            else
                drawNormal(cls, person.address, 0, 0);
            if (!(posX() < cullEdge) && drawn < 4)
            {
                const GuestPtr<Family> family = Family_Get(person->family, 0);
                const uint32_t record = (family.field(&Family::members) + person->member).address;
                if (view->choosing == 0)
                    drawLabel(record, seen % 2 == 0 ? 0xBA : 0xAA, 0x200000, person->family, person->member);
                ++drawn;
            }
            advance();
        }
    }

    PORT_FN(TalkView_DrawPeople);
}
