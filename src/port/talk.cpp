// Talks: the talk session and its views, and the WORDS script handlers.

#include "game_functions.h"
#include "game_globals.h"

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

        void cameraOnBuilding(GuestPtr<TalkView> view)
        {
            port::GuestLocal<Vec4> target; // declared first: the same stack slots as the original's frame
            port::GuestLocal<Vec4> eye;
            TalkView_BuildingCamera(view, farBuilding(view), eye, target);
            Camera_SetEyeTarget(cityCamera(view), eye, target);
            Camera_UpdateMatrices(cityCamera(view));
        }

        void cameraBack(GuestPtr<TalkView> view)
        {
            Camera_SetEyeTarget(cityCamera(view), farReturnEye(view), farReturnTarget(view));
            Camera_UpdateMatrices(cityCamera(view));
        }
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

    PORT_FN(Talk_LookupPose);
    PORT_FN(Talk_ctor);
    PORT_FN(TalkView_ctor);
    PORT_FN(TalkView_DrawCursor);
    PORT_FN(TalkBldgFar_Open);
    PORT_FN(TalkBldgFar_Close);
    PORT_FN(TalkBldgFar_Opening);
    PORT_FN(TalkBldgFar_Opened);
    PORT_FN(TalkBldgFar_Active);
    PORT_FN(TalkBldgFar_Closing);
    PORT_FN(TalkBldgFar_Closed);
}
