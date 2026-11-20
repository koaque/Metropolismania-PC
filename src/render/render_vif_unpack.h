#pragma once

// The general UNPACK loop of the renderer's VIF1 (render_vif.cpp): any format, the write cycle (CL, WL), the write mask
// and the row modes, as the runtime's VIF1 does them. Made per format and row mode at compile time, with the mask read
// once per UNPACK, since at a long view distance (MMPC_VIEW_DISTANCE) this loop was a third of the render thread.

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace mm::render::unpack
{
    struct Job
    {
        uint32_t *memory;          // VU1 data memory, 1024 qwords
        const uint8_t *data;       // the UNPACK's data
        uint32_t address, count;   // first qword (TOPS already added), vectors written
        uint32_t sourceVectors;    // vectors taken from the data
        uint32_t vectorBytes;      // bytes a vector of the data
        uint32_t cl, wl;           // the write cycle (both at least 1)
        bool zeroExtend;           // the USN bit
        bool masked;               // the m bit of the code (STMASK applies)
        uint32_t mask;             // STMASK
        uint32_t *row;             // STROW (mode 2 writes it)
        const uint32_t *col;       // STCOL
    };

    // A vector of the data as the four lanes of a qword. Lanes it does not set keep `value`'s (the qword before).
    template <uint32_t VL, uint32_t COMPONENTS, bool V45>
    inline void decode(const uint8_t *v, bool zeroExtend, uint32_t value[4])
    {
        if constexpr (V45)
        {
            uint16_t packed;
            std::memcpy(&packed, v, 2);
            value[0] = (packed & 0x1Fu) << 3;
            value[1] = (packed & 0x3E0u) >> 2;
            value[2] = (packed & 0x7C00u) >> 7;
            value[3] = (packed & 0x8000u) >> 8;
        }
        else
        {
            for (uint32_t c = 0; c < COMPONENTS; ++c)
            {
                if constexpr (VL == 0)
                    std::memcpy(&value[c], v + c * 4u, 4);
                else if constexpr (VL == 1)
                {
                    uint16_t raw;
                    std::memcpy(&raw, v + c * 2u, 2);
                    value[c] = zeroExtend ? raw : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                }
                else
                    value[c] = zeroExtend ? v[c] : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v[c])));
            }
            if constexpr (COMPONENTS == 1)
                value[1] = value[2] = value[3] = value[0];
            else if constexpr (COMPONENTS == 2) // the hardware writes x y x y
                value[2] = value[0], value[3] = value[1];
        }
    }

    // ROW: 0 data as it is, 1 data plus ROW (offset mode), 2 data plus ROW, kept in ROW (difference mode).
    template <uint32_t VL, uint32_t COMPONENTS, bool V45, uint32_t ROW, bool MASKED>
    void run(const Job &job)
    {
        // what each field of each cycle slot takes (mask: 0 data, 1 ROW, 2 COL, 3 protected); a vector filled without
        // data (CL < WL) takes ROW where it would take data
        uint8_t spec[4][4] = {}, filled[4][4];
        for (uint32_t slot = 0; slot < 4; ++slot)
            for (uint32_t field = 0; field < 4; ++field)
            {
                const uint32_t s = MASKED ? (job.mask >> ((slot * 4u + field) * 2u)) & 3u : 0u;
                spec[slot][field] = static_cast<uint8_t>(s);
                filled[slot][field] = static_cast<uint8_t>(s == 0 ? 1u : s);
            }
        uint32_t *const row = job.row;
        const bool skipping = job.cl >= job.wl; // WL of every CL qwords written, the rest left
        uint32_t cycle = 0, block = 0, source = 0;
        for (uint32_t i = 0; i < job.count; ++i)
        {
            const uint32_t destination = (skipping ? job.address + block * job.cl + cycle : job.address + i) & 0x3FFu;
            uint32_t *lanes = job.memory + static_cast<size_t>(destination) * 4u;
            const uint32_t slot = std::min(cycle, 3u);
            if ((skipping || cycle < job.cl) && source < job.sourceVectors)
            {
                uint32_t value[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
                decode<VL, COMPONENTS, V45>(job.data + static_cast<size_t>(source) * job.vectorBytes, job.zeroExtend, value);
                ++source;
                for (uint32_t field = 0; field < 4; ++field)
                {
                    const uint32_t s = MASKED ? spec[slot][field] : 0u;
                    if (s == 0)
                    {
                        uint32_t v = value[field];
                        if constexpr (ROW != 0)
                        {
                            v += row[field];
                            if constexpr (ROW == 2)
                                row[field] = v;
                        }
                        lanes[field] = v;
                    }
                    else if (s == 1)
                        lanes[field] = row[field];
                    else if (s == 2)
                        lanes[field] = job.col[slot];
                }
            }
            else
            {
                for (uint32_t field = 0; field < 4; ++field)
                {
                    const uint32_t s = MASKED ? filled[slot][field] : 1u;
                    if (s == 1)
                        lanes[field] = row[field];
                    else if (s == 2)
                        lanes[field] = job.col[slot];
                }
            }
            if (++cycle == job.wl)
            {
                cycle = 0;
                ++block;
            }
        }
    }

    template <uint32_t VL, uint32_t COMPONENTS, bool V45>
    inline void runRow(const Job &job, uint32_t row)
    {
        if (job.masked)
        {
            if (row == 1)
                run<VL, COMPONENTS, V45, 1, true>(job);
            else if (row == 2)
                run<VL, COMPONENTS, V45, 2, true>(job);
            else
                run<VL, COMPONENTS, V45, 0, true>(job);
        }
        else
        {
            if (row == 1)
                run<VL, COMPONENTS, V45, 1, false>(job);
            else if (row == 2)
                run<VL, COMPONENTS, V45, 2, false>(job);
            else
                run<VL, COMPONENTS, V45, 0, false>(job);
        }
    }

    // vn, vl: the code's format bits (vl 3 = V4-5); mode: STMOD (1 offset, 2 difference; V4-5 takes no ROW).
    inline void general(const Job &job, uint32_t vn, uint32_t vl, uint32_t mode)
    {
        if (vl == 3)
        {
            runRow<3, 4, true>(job, 0);
            return;
        }
        const uint32_t row = mode == 1 || mode == 2 ? mode : 0;
        switch (vl * 4 + vn)
        {
        case 0: runRow<0, 1, false>(job, row); break;
        case 1: runRow<0, 2, false>(job, row); break;
        case 2: runRow<0, 3, false>(job, row); break;
        case 3: runRow<0, 4, false>(job, row); break;
        case 4: runRow<1, 1, false>(job, row); break;
        case 5: runRow<1, 2, false>(job, row); break;
        case 6: runRow<1, 3, false>(job, row); break;
        case 7: runRow<1, 4, false>(job, row); break;
        case 8: runRow<2, 1, false>(job, row); break;
        case 9: runRow<2, 2, false>(job, row); break;
        case 10: runRow<2, 3, false>(job, row); break;
        default: runRow<2, 4, false>(job, row); break;
        }
    }
}
