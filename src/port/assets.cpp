// Assets built after loading: the character part table.

#include "game_functions.h"
#include "game_globals.h"

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
}
