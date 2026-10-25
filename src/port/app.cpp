// The application roots: mm_Main builds the App on its stack and hands it to mm_MainLoop, which runs one frame after
// another (the pad, the queues, the display lists, the game update through the App's vtable, the vsync); mm_GameUpdate
// is the game's frame, a state machine that makes and runs one scene at a time; mm_App_ctor brings up the IOP, the
// modules, the display and the queues. None of them can be replayed by the difftest (the loop never returns, the
// constructor restarts the IOP), so they stay "ported" and are checked by live runs.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kAppVtable = 0x001BD8D0u;
        constexpr uint32_t kGameVtable = 0x001BCE60u;
        constexpr uint32_t kTimer0Count = 0x10000000u;
        constexpr uint32_t kTimer0Mode = 0x10000010u;
        // The App's virtual functions: slot +8 starts it, slot +0xC runs a frame.
        constexpr uint32_t kSlotStart = 0x8u;
        constexpr uint32_t kSlotFrame = 0xCu;

        // The vtable slot of an object whose vtable pointer is its first word.
        uint32_t slotOf(GuestPtr<App> app, uint32_t offset) { return port::load<uint32_t>(port::load<uint32_t>(app.address) + offset); }

        // The count of a timer as a float the way the compiler converts an unsigned word: a word with the top bit set is halved
        // (keeping the low bit so it rounds right), converted and doubled.
        ps2f floatOfUnsigned(uint32_t count)
        {
            if (static_cast<int32_t>(count) >= 0)
                return fromInt(static_cast<int32_t>(count));
            const ps2f half = fromInt(static_cast<int32_t>((count >> 1) | (count & 1u)));
            return half + half;
        }

        // Loads an IOP module from the disc, and again after a message until it has loaded.
        void loadModule(uint32_t path, uint32_t failureMessage)
        {
            while (sdk::sceSifLoadModule(path, 0, 0) < 0)
            {
                sdk::printf(failureMessage);
                port::checkpoint();
            }
        }
    }

    // The whole program: the heap is probed (all of it is allocated and freed once), the static constructors run, the App is
    // built in this function's stack frame (0x30 bytes, the App at +0x10), switched to the Game's vtable, run until the loop
    // ends, switched to the Game's vtable again for the destructor, and the global destructors run. Returns 0.
    int Main()
    {
        port::GuestFrame<0x30> frame;
        const GuestPtr<App> app{frame.ptr().address + 0x10u};
        Heap_Probe();
        sdk::crt0_InitCpp();
        App_ctor(app);
        app->vtable = GuestPtr<void>{kGameVtable};
        MainLoop(app);
        app->vtable = GuestPtr<void>{kGameVtable};
        App_dtor(app, 0);
        sdk::destroy_global_chain();
        return 0;
    }

    PORT_FN(Main);

    // The frame loop, which never ends. The App is started through slot +8 and the display set to 640 x 448. Each frame: timer 0
    // starts from 0, the frame counter (0x1BDCAC) goes up, then the pad, the file queue, the memory card queue and the sound
    // commands are serviced, the GS path is synced, the display list that was written is sent and the lists flip, the instance
    // buffer toggles and its count clears, and the App runs its frame through slot +0xC. After the cache flush and a GS path sync the
    // timer is sampled twice (0x1BDCB8, 0x1BDCBC) and the second sample gives the frame's load in percent against 9765.625
    // (0x1BD9B8). The vertical sync is waited for (it returns whether the field flag is clear), the two display environments take the
    // half-pixel offset (the pair picked by the low bit of the display frame count at 0x1BDCD0) and the double buffer is swapped to
    // that field; then the display frame count goes up.
    void MainLoop(GuestPtr<App> app)
    {
        port::GuestFrame<0x30> frame;
        port::callGuest<void>(slotOf(app, kSlotStart), app);
        Gs_InitDisplay(0x280, 0x1C0);
        port::store<uint32_t>(globals::kFrameParity, 0u);
        sdk::sceGsSyncV(0);
        for (;;)
        {
            port::writeHardware32(kTimer0Count, 0u);
            globals::frameCounter.set(globals::frameCounter.get() + 1);
            Pad_Update();
            FileQueue_Pump();
            McQueue_Pump();
            SoundCmd_Flush();
            sdk::sceGsSyncPath(0, 0);
            DmaList_KickFrame();
            DmaList_Flip();
            const int32_t buffer = globals::instanceBuffer.get();
            globals::instanceCount.set(0);
            globals::instanceBuffer.set(buffer ^ 1);
            port::callGuest<void>(slotOf(app, kSlotFrame), app);
            sdk::FlushCache(0);
            globals::timerAfterUpdate.set(port::readHardware32(kTimer0Count));
            sdk::sceGsSyncPath(0, 0);
            globals::timerAfterSync.set(port::readHardware32(kTimer0Count));
            globals::frameLoad.set(static_cast<float>(ps2f(100.0f) * floatOfUnsigned(globals::timerAfterSync.get()) / ps2f(9765.625f)));
            const int field = sdk::sceGsSyncV(0) == 0 ? 1 : 0;
            const auto parity = [] { return port::load<uint32_t>(globals::kFrameParity) & 1u; };
            sdk::sceGsSetHalfOffset(parity() == 0u ? 0x001F57F0u : 0x001F5960u, 0x800, 0x800, field);
            sdk::sceGsSetHalfOffset2(parity() == 0u ? 0x001F5870u : 0x001F59E0u, 0x800, 0x800, field);
            sdk::sceGsSwapDBuffDc(0x001F5790u, static_cast<int>(parity()));
            port::store<uint32_t>(globals::kFrameParity, port::load<uint32_t>(globals::kFrameParity) + 1u);
            port::checkpoint();
        }
    }

    PORT_FN(MainLoop);

    // The game's frame. The sound is updated, then the state (+4) picks one step: 0 and 1 make the title scene (a 0x104 byte
    // object, the argument 0 or 1 for the constructor) and go to 2; 2 runs the title until it answers (1 goes on to the briefing
    // with state 3, anything else to state 7, the title scene is deleted); 3 and 7 make the briefing scene (0x104 bytes, loading
    // stage 1 from state 3, 0 from state 7) and go to 4; 4 runs it until it answers, deletes it through its vtable (at +0x100,
    // slot +0x4C) and goes to 5; 5 makes the city scene (0xD8 bytes) and goes to 6; 6 runs the city, which answers 2 (back to the
    // title, state 1) or 1 (the next briefing, state 3) when it ends, and deletes it. The display list is rewound whenever a
    // scene is deleted. Other states do nothing.
    void GameUpdate(GuestPtr<Game> game)
    {
        port::GuestFrame<0x30> frame; // the original's frame: what the scenes' callees read from unwritten stack depends on it
        Sound_Update();
        switch (static_cast<uint32_t>(game->state))
        {
        case 0:
        case 1:
        {
            const uint32_t memory = sdk::operator_new(0x104u);
            game->title = memory != 0u ? SceneA_ctor(GuestPtr<SceneA>{memory}, game->state == 1 ? 1 : 0) : GuestPtr<SceneA>{};
            game->state = 2;
            break;
        }
        case 2:
        {
            const int result = SceneA_Update(game->title);
            if (result == 0)
                break;
            DmaList_Rewind();
            if (game->title)
            {
                SceneA_dtor(game->title, 1);
                game->title = GuestPtr<SceneA>{};
            }
            game->state = result == 1 ? 3 : 7;
            break;
        }
        case 3:
        case 7:
        {
            const uint32_t memory = sdk::operator_new(0x104u);
            game->briefing = memory != 0u ? SceneB_ctor(GuestPtr<SceneB>{memory}, game->state == 3 ? 1 : 0) : GuestPtr<SceneB>{};
            game->state = 4;
            break;
        }
        case 4:
        {
            if (SceneB_Update(game->briefing) == 0)
                break;
            DmaList_Rewind();
            const uint32_t briefing = game->briefing.address;
            if (briefing != 0u)
            {
                port::callGuest<void>(port::load<uint32_t>(port::load<uint32_t>(briefing + 0x100u) + 0x4Cu), briefing, 1);
                game->briefing = GuestPtr<SceneB>{};
            }
            game->state = 5;
            break;
        }
        case 5:
        {
            DmaList_BeginScene();
            const uint32_t memory = sdk::operator_new(0xD8u);
            game->city = memory != 0u ? CityScene_ctor(GuestPtr<CityScene>{memory}) : GuestPtr<CityScene>{};
            game->state = 6;
            break;
        }
        case 6:
        {
            const uint32_t result = CityScene_Update(game->city);
            if (result != 1u && result != 2u)
                break;
            DmaList_Rewind();
            if (game->city)
            {
                CityScene_dtor(game->city, 1);
                game->city = GuestPtr<CityScene>{};
            }
            game->state = result == 2u ? 1 : 3;
            break;
        }
        default:
            break;
        }
    }

    PORT_FN(GameUpdate);

    // Brings the machine up. The App's vtable is set, the disc library started, and the IOP restarted with
    // cdrom0:\SYS\IOPRP243.IMG (the request is repeated until it is accepted, then the restart is waited for). Then the SIF RPC
    // layer, the disc library (mode 1) and the file system library are started again, the IOP heap server is bound, the random
    // generator is seeded with 12345 and the file queue is started. The IRX modules SIO2MAN, MCMAN, MCSERV and PADMAN are loaded
    // from cdrom0:\SYS (a failure prints "Can't load module ..." and tries again). Then the memory card queue and the sound
    // driver start, the DMA and GS hardware are reset (the GS to interlaced NTSC, frame mode), the display environment at
    // 0x1F5790 is set up (640 x 224 a field, depth test 3, depth format 0x30), two words at 0x1F5910 and 0x1F5A80 get 1.0 in their
    // high half, the caches are flushed, timer 0 is set to mode 0x82, the pad is initialised, the pad focus and repeat words are
    // cleared, and the font and the display lists are loaded. Returns the App.
    GuestPtr<App> App_ctor(GuestPtr<App> app)
    {
        port::GuestFrame<0x20> frame;
        app->vtable = GuestPtr<void>{kAppVtable};
        sdk::sceCdInit(0);
        sdk::sceCdMmode(1);
        while (sdk::sceSifRebootIop(0x001B9C20u) == 0)
            port::checkpoint();
        while (sdk::sceSifSyncIop() == 0)
            port::checkpoint();
        sdk::sceSifInitRpc(0);
        sdk::sceCdInit(0);
        sdk::sceCdMmode(1);
        sdk::sceFsReset();
        sdk::sceSifInitRpc(0);
        sdk::sceSifInitIopHeap();
        Random_Seed(0x3039);
        FileQueue_Init();
        loadModule(0x001B9B20u, 0x001B9B40u); // SIO2MAN.IRX
        loadModule(0x001B9B60u, 0x001B9B80u); // MCMAN.IRX
        loadModule(0x001B9BA0u, 0x001B9BC0u); // MCSERV.IRX
        loadModule(0x001B9BE0u, 0x001B9C00u); // PADMAN.IRX
        McQueue_Init();
        Sound_Init();
        sdk::sceGsResetPath();
        sdk::sceDmaReset(1);
        sdk::sceGsResetGraph(0, 1, 2, 1);
        sdk::sceGsSetDefDBuffDc(0x001F5790u, 0, 0x280, 0xE0, 3, 0x30, 1);
        port::store<uint64_t>(0x001F5910u, 0x3F80000000000000ull);
        port::store<uint64_t>(0x001F5A80u, 0x3F80000000000000ull);
        sdk::FlushCache(0);
        port::writeHardware32(kTimer0Mode, 0x82u);
        PadInit();
        globals::padFocusOwner.set(0);
        globals::padFocusMask.set(0u);
        globals::padRepeatTimer.set(0);
        Font_Load();
        DmaList_Init();
        return app;
    }

    PORT_FN(App_ctor);
}
