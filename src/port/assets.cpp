// Assets built after loading: the character part table.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    // The 81 character part models (the table at 0x1F3B90, filled from CHAR.A3D) each hold 13 groups of `per` node
    // pointers (+0x60 of the model; the node table is at +0xC of its data). A group whose first node has a type below 4
    // is overwritten with another group of the model: the second group (group 1 takes the first). The entry after the
    // last group takes the last group's first node when its own node has a type below 4. The model's mesh slots
    // (+0x50 of the model; their number is a halfword at +0x1C of its data) then lose their records 2, 3, 7, 10, 13 and
    // 16, where a slot whose first word is 1 is first pointed at slot 7 instead.
    void CityAssets_BuildCharParts()
    {
        for (uint32_t slot = 0; slot < 0x51; ++slot)
        {
            const uint32_t model = port::load<uint32_t>(globals::kCharParts + 4u * slot);
            const int32_t per = port::load<int32_t>(model + 0x60u);
            const uint32_t data = port::load<uint32_t>(model);
            int32_t first = 0;
            for (int group = 0; group < 13; ++group, first += per)
            {
                const uint32_t node = port::load<uint32_t>(port::load<uint32_t>(data + 0xCu) + 4u * static_cast<uint32_t>(first));
                if (!(port::load<int32_t>(node + 8u) < 4))
                    continue;
                for (int32_t k = 0; k < per; ++k)
                {
                    const uint32_t nodes = port::load<uint32_t>(data + 0xCu);
                    const uint32_t from = group == 1 ? static_cast<uint32_t>(k) : static_cast<uint32_t>(per + k);
                    port::store<uint32_t>(nodes + 4u * static_cast<uint32_t>(first + k), port::load<uint32_t>(nodes + 4u * from));
                }
            }
            const uint32_t end = port::load<uint32_t>(data + 0xCu) + 52u * static_cast<uint32_t>(per);
            if (port::load<int32_t>(port::load<uint32_t>(end + 4u) + 8u) < 4)
                port::store<uint32_t>(end + 4u, port::load<uint32_t>(end));

            const uint32_t meshes = port::load<uint32_t>(model + 0x50u);
            const int32_t meshCount = port::load<int16_t>(port::load<uint32_t>(model) + 0x1Cu);
            for (int32_t i = 0; i < meshCount; ++i)
            {
                const uint32_t meshSlot = meshes + 4u * static_cast<uint32_t>(i);
                uint32_t records = port::load<uint32_t>(meshSlot);
                if (port::load<int32_t>(records) == 1)
                    port::store<uint32_t>(meshSlot, port::load<uint32_t>(meshes + 0x1Cu));
                const int32_t recordCount = port::load<int32_t>(model + 8u);
                for (int32_t r = 0; r < recordCount; ++r, records += 0x10u)
                    if (r == 2 || r == 3 || r == 7 || r == 10 || r == 13 || r == 16)
                        port::store<uint32_t>(records, 0);
            }
        }
    }

    PORT_FN(CityAssets_BuildCharParts);

    // Reads the three MYHOME model files (DATA\BIN\MYHOME_*.A3L, sizes at 0x1B0B80) one per round of three steps, a call
    // each: 0 allocates the block, opens, queues the read and the close; 1 waits for the close's request to be done; 2 moves
    // on to the next file, and after the third the whole load counts as done.
    void CityAssets_LoadMyHome()
    {
        const int32_t state = globals::myHomeLoadState.get();
        if (state == 2)
        {
            globals::myHomeLoadIndex.set(globals::myHomeLoadIndex.get() + 1);
            if (globals::myHomeLoadIndex.get() < 3)
            {
                globals::myHomeLoadState.set(0);
            }
            else
            {
                globals::myHomeLoadState.set(0);
                globals::myHomeLoadIndex.set(0);
                globals::myHomeLoadsDone.set(globals::myHomeLoadsDone.get() + 1);
            }
        }
        else if (state == 1)
        {
            if (FileQueue_IsDone(static_cast<int>(globals::myHomeFile.get()), globals::myHomeRequest.get()) != 0)
                globals::myHomeLoadState.set(globals::myHomeLoadState.get() + 1);
        }
        else if (state == 0)
        {
            const uint32_t index = static_cast<uint32_t>(globals::myHomeLoadIndex.get());
            const uint32_t size = port::load<uint32_t>(globals::kMyHomeSizes + index * 4u);
            const uint32_t block = sdk::malloc(size);
            port::store<uint32_t>(globals::kMyHomeBlocks + static_cast<uint32_t>(globals::myHomeLoadIndex.get()) * 4u, block);
            const GuestPtr<GameFile> file{globals::myHomeFile.get()};
            File_Open(file, GuestPtr<char>{globals::kMyHomePaths + index * 0x20u});
            const uint32_t current = static_cast<uint32_t>(globals::myHomeLoadIndex.get());
            File_Read(file, port::load<int32_t>(globals::kMyHomeBlocks + current * 4u), 1,
                      port::load<int32_t>(globals::kMyHomeSizes + current * 4u), 0);
            globals::myHomeRequest.set(File_Close(file));
            globals::myHomeLoadState.set(globals::myHomeLoadState.get() + 1);
            globals::myHomeReads.set(globals::myHomeReads.get() + 1);
        }
    }

    PORT_FN(CityAssets_LoadMyHome);

    namespace
    {
        void assetStepOn()
        {
            globals::assetLoadStep.set(globals::assetLoadStep.get() + 1);
        }

        // The first step of a list load: the shared file opens `path` and the request is kept.
        void assetOpenFile(uint32_t path)
        {
            const int request = File_Open(GuestPtr<GameFile>{globals::assetLoadFile.get()}, GuestPtr<char>{path});
            globals::assetLoadRequest.set(request);
            assetStepOn();
        }

        // Whether the request queued last (the open or the close) is finished.
        bool assetRequestDone()
        {
            return FileQueue_IsDone(static_cast<int>(globals::assetLoadFile.get()), globals::assetLoadRequest.get()) != 0;
        }

        // The step that closes the file; the request is kept.
        void assetCloseFile()
        {
            globals::assetLoadRequest.set(File_Close(GuestPtr<GameFile>{globals::assetLoadFile.get()}));
            assetStepOn();
        }

        // The last step: back to the first step and file for the next load, and this load counts as done.
        void assetLoadDone()
        {
            const int32_t done = globals::assetLoadsDone.get();
            globals::assetLoadStep.set(0);
            globals::assetLoadIndex.set(0);
            globals::assetLoadsDone.set(done + 1);
        }

        // OBJECT.A3D and CHAROBJ.A3D load the same way: a model is made for each entry of the table (by `construct`, 0 when there
        // is no memory) and filled from the file one per call. A result of 1 means it is complete and the next model is made, -1
        // that the file has no more, and the load goes on to closing the file.
        template <typename Construct>
        void loadModelList(uint32_t path, uint32_t table, Construct construct)
        {
            switch (globals::assetLoadStep.get())
            {
            case 0:
                port::store<uint32_t>(table + 4u * static_cast<uint32_t>(globals::assetLoadIndex.get()), construct());
                assetOpenFile(path);
                break;
            case 1:
                if (assetRequestDone())
                    assetStepOn();
                break;
            case 2:
            {
                const uint32_t index = static_cast<uint32_t>(globals::assetLoadIndex.get());
                const int result = A3DObject_LoadAsync(GuestPtr<A3DObject>{port::load<uint32_t>(table + 4u * index)},
                                                       GuestPtr<GameFile>{globals::assetLoadFile.get()}, 1);
                if (result == 1)
                {
                    globals::assetLoadIndex.set(globals::assetLoadIndex.get() + 1);
                    const uint32_t model = construct();
                    port::store<uint32_t>(table + 4u * static_cast<uint32_t>(globals::assetLoadIndex.get()), model);
                    globals::assetLoadCount.set(globals::assetLoadCount.get() + 1);
                }
                else if (result == -1)
                {
                    assetStepOn();
                    globals::assetLoadCount.set(globals::assetLoadCount.get() + 1);
                }
                break;
            }
            case 3:
                assetCloseFile();
                break;
            case 4:
                if (assetRequestDone())
                    assetLoadDone();
                break;
            default:
                break;
            }
        }
    }

    // Loads DATA\A3D\CHAR.A3D: 81 character part models into the table at 0x1F3B90 (the model being filled is kept in 0x1BDB58;
    // the call for the last file index passes a flag that keeps the frame count). The steps: 0 make the first model and open the
    // file, 1 wait for the open, 2 load the models, 3 close, 4 wait for the close; then the part table is built.
    void CityAssets_LoadChars()
    {
        switch (globals::assetLoadStep.get())
        {
        case 0:
        {
            uint32_t model = sdk::operator_new(0x70u);
            if (model != 0u)
                model = InstanceModel_ctor2(GuestPtr<A3DObject>{model}).address;
            globals::assetLoadModel.set(model);
            assetOpenFile(0x001B7B50u);
            break;
        }
        case 1:
            if (assetRequestDone())
                assetStepOn();
            break;
        case 2:
        {
            const int result = InstanceModel_LoadAsync(GuestPtr<A3DObject>{globals::assetLoadModel.get()}, GuestPtr<GameFile>{globals::assetLoadFile.get()},
                                                       globals::assetLoadIndex.get() == 0x52 ? 1 : 0);
            if (result == 1)
            {
                port::store<uint32_t>(globals::kCharParts + 4u * static_cast<uint32_t>(globals::assetLoadIndex.get()), globals::assetLoadModel.get());
                globals::assetLoadIndex.set(globals::assetLoadIndex.get() + 1);
                uint32_t model = sdk::operator_new(0x70u);
                if (model != 0u)
                    model = InstanceModel_ctor2(GuestPtr<A3DObject>{model}).address;
                globals::assetLoadModel.set(model);
                globals::assetLoadCount.set(globals::assetLoadCount.get() + 1);
            }
            else if (result == -1)
            {
                port::store<uint32_t>(globals::kCharParts + 4u * static_cast<uint32_t>(globals::assetLoadIndex.get()), globals::assetLoadModel.get());
                assetStepOn();
                globals::assetLoadCount.set(globals::assetLoadCount.get() + 1);
            }
            break;
        }
        case 3:
            assetCloseFile();
            break;
        case 4:
            if (assetRequestDone())
            {
                assetLoadDone();
                CityAssets_BuildCharParts();
            }
            break;
        default:
            break;
        }
    }

    PORT_FN(CityAssets_LoadChars);

    // Loads DATA\A3D\CHAROBJ.A3D into the table at 0x1F3B40 (models of 0x60 bytes).
    void CityAssets_LoadCharObjs()
    {
        loadModelList(0x001B7B70u, globals::kCharObjectModels, []() {
            uint32_t model = sdk::operator_new(0x60u);
            if (model != 0u)
                model = InstanceModel_ctor(GuestPtr<A3DObject>{model}).address;
            return model;
        });
    }

    PORT_FN(CityAssets_LoadCharObjs);

    // Loads DATA\A3D\OBJECT.A3D into the table at 0x1F3B00 (base objects of 0x60 bytes).
    void CityAssets_LoadObjects()
    {
        loadModelList(0x001B7B90u, globals::kObjectModels, []() {
            uint32_t model = sdk::operator_new(0x60u);
            if (model != 0u)
                model = InstanceBase_ctor(GuestPtr<A3DObject>{model}).address;
            return model;
        });
    }

    PORT_FN(CityAssets_LoadObjects);

    // The state machine over the asset loads, one step per call (the count of loads done is in 0x1BDB40): 0 makes the shared file
    // object (0x8000) and clears the part table (83 words), 1 characters, 2 objects, 3 character objects, 4 the MYHOME files,
    // 5 deletes the file object. Gives 1 while there is more to do, 0 after the last.
    int CityAssets_LoadStep()
    {
        const uint32_t load = static_cast<uint32_t>(globals::assetLoadsDone.get());
        if (load >= 6u)
            return 0;
        switch (load)
        {
        case 0:
        {
            uint32_t file = sdk::operator_new(8u);
            if (file != 0u)
                file = File_ctor(GuestPtr<GameFile>{file}, 0x8000).address;
            const int32_t done = globals::assetLoadsDone.get();
            globals::assetLoadFile.set(file);
            globals::assetLoadStep.set(0);
            globals::assetLoadIndex.set(0);
            globals::assetLoadsDone.set(done + 1);
            globals::assetLoadCount.set(0);
            for (uint32_t slot = 0; slot < 0x53u; ++slot)
                port::store<uint32_t>(globals::kCharParts + 4u * slot, 0u);
            break;
        }
        case 1:
            CityAssets_LoadChars();
            break;
        case 2:
            CityAssets_LoadObjects();
            break;
        case 3:
            CityAssets_LoadCharObjs();
            break;
        case 4:
            CityAssets_LoadMyHome();
            break;
        default:
        {
            const uint32_t file = globals::assetLoadFile.get();
            if (file != 0u)
            {
                File_dtor(GuestPtr<GameFile>{file}, 1);
                globals::assetLoadFile.set(0u);
            }
            globals::assetLoadsDone.set(globals::assetLoadsDone.get() + 1);
            break;
        }
        }
        return 1;
    }

    PORT_FN(CityAssets_LoadStep);
}
