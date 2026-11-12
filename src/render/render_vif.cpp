// The renderer's VIF1. See render_vif.h.

#include "render_vif.h"

#include <algorithm>
#include <cstring>

namespace mm::render
{
    void Vif1::run(const uint8_t *data, size_t size)
    {
        size_t pos = 0;
        while (pos + 4 <= size)
        {
            uint32_t code;
            std::memcpy(&code, data + pos, 4);
            pos += 4;
            const uint32_t imm = code & 0xFFFF, num = (code >> 16) & 0xFF, cmd = (code >> 24) & 0x7F;
            if (cmd >= 0x60)
            {
                pos += unpack(code, data + pos, size - pos);
                continue;
            }
            switch (cmd)
            {
            case 0x00: // NOP, or the walker's mark of a program upload
                if (num == kProgramMarkLow)
                    m_markLow = imm;
                else if (num == kProgramMarkHigh)
                    m_pending = (imm << 16) | m_markLow;
                break;
            case 0x01: // STCYCL
                m_cl = imm & 0xFF;
                m_wl = (imm >> 8) & 0xFF;
                break;
            case 0x02: // OFFSET: also clears the double buffer flag and points TOPS at BASE
                m_ofst = imm & 0x3FF;
                m_tops = m_base;
                m_dbf = false;
                break;
            case 0x03: // BASE
                m_base = imm & 0x3FF;
                break;
            case 0x04: // ITOP
                m_itops = imm & 0x3FF;
                break;
            case 0x05: // STMOD
                m_mode = imm & 3;
                break;
            case 0x14: // MSCAL
            case 0x15: // MSCALF
                start(static_cast<int32_t>(imm) * 8);
                break;
            case 0x17: // MSCNT
                start(-1);
                break;
            case 0x20: // STMASK
                if (pos + 4 > size)
                    return;
                std::memcpy(&m_mask, data + pos, 4);
                pos += 4;
                break;
            case 0x30: // STROW
            case 0x31: // STCOL
                if (pos + 16 > size)
                    return;
                std::memcpy(cmd == 0x30 ? m_row : m_col, data + pos, 16);
                pos += 16;
                break;
            case 0x4A: // MPG: the program comes in with it
                pos += static_cast<size_t>(num != 0 ? num : 256) * 8u;
                if (m_pending != 0)
                    m_program = m_pending;
                break;
            case 0x50: // DIRECT
            case 0x51: // DIRECTHL
            {
                pos = (pos + 15) & ~static_cast<size_t>(15);
                const size_t bytes = static_cast<size_t>(imm != 0 ? imm : 65536) * 16u;
                if (pos + bytes > size)
                    return;
                m_gs.gif(data + pos, bytes);
                pos += bytes;
                break;
            }
            default:
                break; // MSKPATH3, MARK, FLUSHE, FLUSH, FLUSHA: no data
            }
        }
    }

    // One UNPACK: returns the bytes of data it takes. As the runtime's VIF1 does it: with CL >= WL, WL vectors written of
    // every CL; with CL < WL, CL vectors of every WL from the data and the rest filled (from ROW, or as the mask says).
    size_t Vif1::unpack(uint32_t code, const uint8_t *data, size_t available)
    {
        const uint32_t imm = code & 0xFFFF, cmd = (code >> 24) & 0x7F;
        const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, components = vn + 1;
        const bool masked = (cmd & 0x10) != 0;
        const uint32_t count = ((code >> 16) & 0xFF) != 0 ? (code >> 16) & 0xFF : 256;
        const bool v45 = vl == 3;
        const uint32_t vectorBytes = v45 ? 2u : components * ((32u >> vl) / 8u);
        const uint32_t cl = m_cl != 0 ? m_cl : 1, wl = m_wl != 0 ? m_wl : 1;
        uint32_t sourceVectors = count;
        if (cl < wl)
            sourceVectors = (count / wl) * cl + std::min(count % wl, cl);
        const size_t bytes = (static_cast<size_t>(sourceVectors) * vectorBytes + 3u) & ~static_cast<size_t>(3);
        if (bytes > available)
            return available;
        uint32_t address = imm & 0x3FF;
        if (imm & 0x8000)
            address = (address + m_tops) & 0x3FF;
        const bool zeroExtend = (imm & 0x4000) != 0;

        uint32_t source = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t cycle = i % wl;
            const bool fromData = cl >= wl || cycle < cl;
            const uint32_t destination = cl >= wl ? (address + (i / wl) * cl + cycle) & 0x3FF : (address + i) & 0x3FF;
            uint32_t *lanes = m_memory.qword(destination);
            uint32_t value[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
            bool decoded = false;
            if (fromData && source < sourceVectors)
            {
                const uint8_t *v = data + static_cast<size_t>(source) * vectorBytes;
                ++source;
                decoded = true;
                if (v45)
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
                    for (uint32_t c = 0; c < components; ++c)
                    {
                        if (vl == 0)
                            std::memcpy(&value[c], v + c * 4u, 4);
                        else if (vl == 1)
                        {
                            uint16_t raw;
                            std::memcpy(&raw, v + c * 2u, 2);
                            value[c] = zeroExtend ? raw : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                        }
                        else
                            value[c] = zeroExtend ? v[c] : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v[c])));
                    }
                    if (components == 1)
                        value[1] = value[2] = value[3] = value[0];
                    else if (components == 2) // the hardware writes x y x y
                        value[2] = value[0], value[3] = value[1];
                }
            }
            const uint32_t slot = std::min(cycle, 3u);
            for (uint32_t field = 0; field < 4; ++field)
            {
                uint32_t spec = masked ? (m_mask >> ((slot * 4u + field) * 2u)) & 3u : 0u;
                if (!decoded && spec == 0)
                    spec = 1; // a filled vector takes ROW where it would take data
                if (spec == 0)
                {
                    uint32_t v = value[field];
                    if (!v45 && (m_mode == 1 || m_mode == 2))
                    {
                        v += m_row[field];
                        if (m_mode == 2)
                            m_row[field] = v;
                    }
                    lanes[field] = v;
                }
                else if (spec == 1)
                    lanes[field] = m_row[field];
                else if (spec == 2)
                    lanes[field] = m_col[slot];
                // 3: write-protected
            }
        }
        return bytes;
    }

    // MSCAL / MSCNT: the run gets TOPS as TOP, then the double buffer flips.
    void Vif1::start(int32_t entry)
    {
        const uint32_t top = m_tops & 0x3FF, itop = m_itops & 0x3FF;
        m_tops = (m_dbf ? m_base : m_base + m_ofst) & 0x3FF;
        m_dbf = !m_dbf;
        VuProgram *program = nativeProgram(m_program);
        if (program == nullptr)
        {
            ++m_missing[m_program];
            return;
        }
        m_packets.clear();
        runVuProgram(*program, m_memory, entry, top, itop, m_packets);
        for (const VuPacket &packet : m_packets)
            m_gs.vuPacket(packet);
    }
}
