// The renderer's VIF1. See render_vif.h.

#include "render_vif.h"

#include "enhance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

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
                else if (num == kSpanMark && imm == static_cast<uint32_t>(port::enhance::Span::WorldCamera))
                    m_adjustCamera = true;
                else if (num == kSpanMark && imm == static_cast<uint32_t>(port::enhance::Span::Extras))
                    m_gs.tintExtras(true);
                else if (num == kSpanMark)
                    m_gs.beginSpan(static_cast<uint8_t>(imm));
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

        // The common case, without a write mask or the row modes and with a write cycle the same as the read one: each vector
        // whole into the next qword (V4-32 a plain copy), as the general loop below would write it.
        if (!masked && m_mode == 0 && cl == wl && !v45)
        {
            const uint32_t size = 32u >> vl;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t *lanes = m_memory.qword((address + i) & 0x3FF);
                const uint8_t *v = data + static_cast<size_t>(i) * vectorBytes;
                if (vl == 0 && components == 4)
                {
                    std::memcpy(lanes, v, 16);
                    continue;
                }
                uint32_t value[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
                for (uint32_t c = 0; c < components; ++c)
                {
                    if (size == 32)
                        std::memcpy(&value[c], v + c * 4u, 4);
                    else if (size == 16)
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
                std::memcpy(lanes, value, 16);
            }
            if (m_adjustCamera && address == 0 && count >= 8)
                adjustWorldCamera(count);
            return bytes;
        }

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
        if (m_adjustCamera && address == 0 && count >= 8)
            adjustWorldCamera(count);
        return bytes;
    }

    // A matrix row maps a world point to (x, y, z, w) by the sum of the rows times its coordinates; the screen matrix's x
    // is the camera space x times the screen scale plus the screen centre (2048, mm_Camera_ctor) times w, the clip
    // matrix's x has no centre. A view k times as wide at the same height: the scale part divided by k in both.
    // With a longer view distance (MMPC_VIEW_DISTANCE, factor d) the far plane moves out by d (moveFarPlane), and so does
    // the fog: the programs take fog = a + b / w, so b (0x11.w, in the same upload) times d gives at distance d x what was
    // had at x.
    void Vif1::adjustWorldCamera(uint32_t count)
    {
        m_adjustCamera = false;
        if (const float d = port::enhance::viewDistance(); d != 1.0f)
        {
            moveFarPlane(d);
            if (count >= 0x12)
                m_memory.setReal(0x11, 3, m_memory.real(0x11, 3) * d);
        }
        if (const float k = port::enhance::widescreen(); k != 1.0f)
        {
            constexpr float kScreenCentre = 2048.0f;
            const float a = 1.0f / k, b = kScreenCentre * (1.0f - a);
            for (uint32_t row = 0; row < 4; ++row)
                m_memory.setReal(row, 0, m_memory.real(row, 0) * a + m_memory.real(row, 3) * b);
            for (uint32_t row = 4; row < 8; ++row)
                m_memory.setReal(row, 0, m_memory.real(row, 0) * a);
        }
    }

    // The far plane d times farther. The programs drop a triangle when a corner fails the clip test, +z included, and the
    // depth past the far plane falls below the far depth (the renderer takes it as 0), so what the extras pass draws past
    // the camera's far plane (50 for the city camera, while the game's own terrain stops at 28 to 35) would not show.
    // The clip matrix's z is P z + Q of the camera space z (its w), with P = (f + n) / (f - n) and Q = -2 f n / (f - n);
    // the screen matrix's z is A + B z, so after the divide by w = z the depth is A / z + B: the near depth at n and the far
    // depth at f (Camera_UpdateProjection). Under a camera's view (affine) rows 0 to 2 have z = P w and z = B w, and row 3
    // gives Q and A; n, f and the two depths follow. Both are rebuilt for d f and applied to each row as z' = alpha z +
    // beta w, which keeps the rest of the matrices as the game made them.
    void Vif1::moveFarPlane(float d)
    {
        // z / w of the row among rows 0 to 2 with the largest w (the view direction's largest component)
        const auto ratio = [&](uint32_t first) {
            uint32_t best = first;
            for (uint32_t row = first + 1; row < first + 3; ++row)
                if (std::fabs(m_memory.real(row, 3)) > std::fabs(m_memory.real(best, 3)))
                    best = row;
            return static_cast<double>(m_memory.real(best, 2)) / m_memory.real(best, 3);
        };
        const auto constant = [&](uint32_t first, double r) {
            return static_cast<double>(m_memory.real(first + 3, 2)) - r * m_memory.real(first + 3, 3);
        };
        const double p = ratio(4), q = constant(4, p);
        const double n = -q / (p + 1.0), f = -q / (p - 1.0);
        const double b = ratio(0), a = constant(0, b);
        const double nearDepth = a / n + b, farDepth = a / f + b;
        static bool s_logged = false; // the first camera's values, once, to see what the upload holds
        if (!s_logged)
        {
            s_logged = true;
            std::cerr << "[render] view distance: the world camera's near " << n << ", far " << f << " (depth " << nearDepth << " to "
                      << farDepth << "), far moved to " << f * d << std::endl;
        }
        if (!(n > 0.0 && f > n && std::isfinite(f)) || a == 0.0)
            return; // not a perspective camera of this kind: left as it is
        const double far = f * static_cast<double>(d);
        const double p2 = (far + n) / (far - n), q2 = -2.0 * far * n / (far - n);
        const double a2 = far * n * (nearDepth - farDepth) / (far - n), b2 = (farDepth * far - nearDepth * n) / (far - n);
        const auto apply = [&](uint32_t first, double alpha, double beta) {
            for (uint32_t row = first; row < first + 4; ++row)
                m_memory.setReal(row, 2, static_cast<float>(alpha * m_memory.real(row, 2) + beta * m_memory.real(row, 3)));
        };
        const double alphaClip = q2 / q, alphaScreen = a2 / a;
        apply(4, alphaClip, p2 - alphaClip * p);
        apply(0, alphaScreen, b2 - alphaScreen * b);
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
