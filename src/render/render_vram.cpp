// The native renderer's copy of the GS local memory: image transfers in, textures and palettes out. See render_vram.h.

#include "render_vram.h"

#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace mm::render
{
    namespace
    {
        constexpr uint32_t kPsmCt32 = 0x00, kPsmCt24 = 0x01, kPsmCt16 = 0x02, kPsmCt16s = 0x0A, kPsmT8 = 0x13, kPsmT4 = 0x14;

        uint32_t bitsPerPixel(uint32_t psm)
        {
            switch (psm)
            {
            case kPsmCt32:
                return 32;
            case kPsmCt24:
                return 24;
            case kPsmCt16:
            case kPsmCt16s:
                return 16;
            case kPsmT8:
                return 8;
            case kPsmT4:
                return 4;
            default:
                return 0;
            }
        }

        uint32_t byteAddress(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
        {
            switch (psm)
            {
            case kPsmCt32:
            case kPsmCt24:
                return GSPSMCT32::addrPSMCT32(bp, bw, x, y);
            case kPsmCt16:
                return GSPSMCT16::addrPSMCT16(bp, bw, x, y);
            case kPsmCt16s:
                return GSPSMCT16::addrPSMCT16S(bp, bw, x, y);
            case kPsmT8:
                return GSPSMT8::addrPSMT8(bp, bw, x, y);
            default:
                return 0;
            }
        }
    }

    Tex0 Tex0::fromRegister(uint64_t value)
    {
        Tex0 t;
        t.tbp = static_cast<uint32_t>(value & 0x3FFF);
        t.tbw = static_cast<uint32_t>((value >> 14) & 0x3F);
        t.psm = static_cast<uint32_t>((value >> 20) & 0x3F);
        t.tw = static_cast<uint32_t>((value >> 26) & 0xF);
        t.th = static_cast<uint32_t>((value >> 30) & 0xF);
        t.tcc = static_cast<uint32_t>((value >> 34) & 1);
        t.tfx = static_cast<uint32_t>((value >> 35) & 3);
        t.cbp = static_cast<uint32_t>((value >> 37) & 0x3FFF);
        t.cpsm = static_cast<uint32_t>((value >> 51) & 0xF);
        t.csm = static_cast<uint32_t>((value >> 55) & 1);
        t.csa = static_cast<uint32_t>((value >> 56) & 0x1F);
        t.cld = static_cast<uint32_t>((value >> 61) & 7);
        return t;
    }

    Vram::Vram() : m_memory(kBytes, 0u) { m_pageHashVersion.fill(~0ull); }

    void Vram::touch(uint32_t byteAddress) { m_pageVersion[(byteAddress & (kBytes - 1u)) >> 13] = m_clock; }

    void Vram::setTrxdir(uint64_t value)
    {
        const uint32_t direction = static_cast<uint32_t>(value & 3);
        const uint32_t sbp = static_cast<uint32_t>(m_bitbltbuf & 0x3FFF), sbw = static_cast<uint32_t>((m_bitbltbuf >> 16) & 0x3F);
        const uint32_t spsm = static_cast<uint32_t>((m_bitbltbuf >> 24) & 0x3F);
        m_dbp = static_cast<uint32_t>((m_bitbltbuf >> 32) & 0x3FFF);
        m_dbw = static_cast<uint32_t>((m_bitbltbuf >> 48) & 0x3F);
        m_dpsm = static_cast<uint32_t>((m_bitbltbuf >> 56) & 0x3F);
        const uint32_t ssax = static_cast<uint32_t>(m_trxpos & 0x7FF), ssay = static_cast<uint32_t>((m_trxpos >> 16) & 0x7FF);
        m_x0 = static_cast<uint32_t>((m_trxpos >> 32) & 0x7FF);
        m_y0 = static_cast<uint32_t>((m_trxpos >> 48) & 0x7FF);
        m_width = static_cast<uint32_t>(m_trxreg & 0xFFF);
        m_height = static_cast<uint32_t>((m_trxreg >> 32) & 0xFFF);
        m_x = m_y = 0;
        m_carry.clear();
        ++m_clock;
        if (direction == 0)
        {
            m_remaining = static_cast<uint64_t>(m_width) * m_height;
            if (bitsPerPixel(m_dpsm) == 0)
            {
                static bool s_warned = false;
                if (!s_warned)
                    std::cerr << "[render] image transfer to format " << m_dpsm << " not handled" << std::endl;
                s_warned = true;
            }
        }
        else if (direction == 2)
        {
            // Local to local, done at once (same format on both sides, as the game uses it).
            for (uint32_t y = 0; y < m_height; ++y)
                for (uint32_t x = 0; x < m_width; ++x)
                {
                    const uint32_t value = readPixel(spsm, sbp, sbw, ssax + x, ssay + y);
                    m_x = x;
                    m_y = y;
                    writePixel(m_x0 + x, m_y0 + y, value);
                }
            m_remaining = 0;
        }
        else
            m_remaining = 0; // local to host: the game never reads VRAM back
    }

    void Vram::writePixel(uint32_t x, uint32_t y, uint32_t value)
    {
        uint8_t *mem = m_memory.data();
        x &= 0x7FF;
        y &= 0x7FF;
        switch (m_dpsm)
        {
        case kPsmCt32:
        {
            const uint32_t a = byteAddress(kPsmCt32, m_dbp, m_dbw, x, y) & (kBytes - 4u);
            std::memcpy(mem + a, &value, 4);
            touch(a);
            break;
        }
        case kPsmCt24:
        {
            const uint32_t a = byteAddress(kPsmCt24, m_dbp, m_dbw, x, y) & (kBytes - 4u);
            std::memcpy(mem + a, &value, 3);
            touch(a);
            break;
        }
        case kPsmCt16:
        case kPsmCt16s:
        {
            const uint32_t a = byteAddress(m_dpsm, m_dbp, m_dbw, x, y) & (kBytes - 2u);
            const uint16_t half = static_cast<uint16_t>(value);
            std::memcpy(mem + a, &half, 2);
            touch(a);
            break;
        }
        case kPsmT8:
        {
            const uint32_t a = byteAddress(kPsmT8, m_dbp, m_dbw, x, y) & (kBytes - 1u);
            mem[a] = static_cast<uint8_t>(value);
            touch(a);
            break;
        }
        case kPsmT4:
        {
            const uint32_t nibble = GSPSMT4::addrPSMT4(m_dbp, m_dbw, x, y) & (kBytes * 2u - 1u);
            uint8_t &byte = mem[nibble >> 1];
            byte = (nibble & 1u) ? static_cast<uint8_t>((byte & 0x0F) | ((value & 0xF) << 4)) : static_cast<uint8_t>((byte & 0xF0) | (value & 0xF));
            touch(nibble >> 1);
            break;
        }
        default:
            break;
        }
    }

    uint32_t Vram::readPixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const
    {
        const uint8_t *mem = m_memory.data();
        x &= 0x7FF;
        y &= 0x7FF;
        switch (psm)
        {
        case kPsmCt32:
        case kPsmCt24:
        {
            uint32_t v = 0;
            std::memcpy(&v, mem + (byteAddress(kPsmCt32, bp, bw, x, y) & (kBytes - 4u)), 4);
            return v;
        }
        case kPsmCt16:
        case kPsmCt16s:
        {
            uint16_t v = 0;
            std::memcpy(&v, mem + (byteAddress(psm, bp, bw, x, y) & (kBytes - 2u)), 2);
            return v;
        }
        case kPsmT8:
            return mem[byteAddress(kPsmT8, bp, bw, x, y) & (kBytes - 1u)];
        case kPsmT4:
        {
            const uint32_t nibble = GSPSMT4::addrPSMT4(bp, bw, x, y) & (kBytes * 2u - 1u);
            const uint8_t byte = mem[nibble >> 1];
            return (nibble & 1u) ? (byte >> 4) : (byte & 0xF);
        }
        default:
            return 0;
        }
    }

    void Vram::image(const uint8_t *data, size_t bytes)
    {
        const uint32_t bpp = bitsPerPixel(m_dpsm);
        if (m_remaining == 0 || bpp == 0)
            return;
        ++m_clock;
        // Whole pixels (two for 4-bit) at a time; a group split between calls waits in m_carry.
        const uint32_t groupBytes = bpp == 4 ? 1u : bpp / 8u;
        const uint32_t groupPixels = bpp == 4 ? 2u : 1u;
        size_t pos = 0;
        auto next = [&](uint32_t value)
        {
            if (m_remaining == 0)
                return;
            writePixel(m_x0 + m_x, m_y0 + m_y, value);
            if (++m_x >= m_width)
            {
                m_x = 0;
                ++m_y;
            }
            --m_remaining;
        };
        auto group = [&](const uint8_t *g)
        {
            switch (bpp)
            {
            case 32:
                next(static_cast<uint32_t>(g[0]) | (static_cast<uint32_t>(g[1]) << 8) | (static_cast<uint32_t>(g[2]) << 16) |
                     (static_cast<uint32_t>(g[3]) << 24));
                break;
            case 24:
                next(static_cast<uint32_t>(g[0]) | (static_cast<uint32_t>(g[1]) << 8) | (static_cast<uint32_t>(g[2]) << 16));
                break;
            case 16:
                next(static_cast<uint32_t>(g[0]) | (static_cast<uint32_t>(g[1]) << 8));
                break;
            case 8:
                next(g[0]);
                break;
            case 4:
                next(g[0] & 0xFu);
                next(g[0] >> 4);
                break;
            default:
                break;
            }
        };
        if (!m_carry.empty())
        {
            while (m_carry.size() < groupBytes && pos < bytes)
                m_carry.push_back(data[pos++]);
            if (m_carry.size() < groupBytes)
                return;
            group(m_carry.data());
            m_carry.clear();
        }
        for (; pos + groupBytes <= bytes && m_remaining > 0; pos += groupBytes)
            group(data + pos);
        if (m_remaining > 0)
            for (; pos < bytes; ++pos)
                m_carry.push_back(data[pos]);
        (void)groupPixels;
    }

    void Vram::loadClut(const Tex0 &tex, uint32_t *clut) const
    {
        if (tex.cpsm != kPsmCt32 && tex.cpsm != kPsmCt24)
            return; // 16-bit palettes: not used by this game
        if (tex.psm == kPsmT8)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                const uint32_t swizzled = (i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
                clut[i] = readPixel(kPsmCt32, tex.cbp, 1, swizzled & 0xFu, swizzled >> 4);
            }
        }
        else if (tex.psm == kPsmT4)
        {
            const uint32_t base = (tex.csa & 0xFu) * 16u;
            for (uint32_t i = 0; i < 16; ++i)
                clut[(base + i) & 0xFFu] = readPixel(kPsmCt32, tex.cbp, 1, i & 7u, i >> 3);
        }
    }

    bool Vram::decode(const Tex0 &tex, uint32_t width, uint32_t height, const uint32_t *clut, std::vector<uint32_t> &out) const
    {
        out.resize(static_cast<size_t>(width) * height);
        const uint32_t csaBase = (tex.csa & 0xFu) * 16u;
        for (uint32_t y = 0; y < height; ++y)
        {
            uint32_t *row = out.data() + static_cast<size_t>(y) * width;
            for (uint32_t x = 0; x < width; ++x)
            {
                switch (tex.psm)
                {
                case kPsmCt32:
                    row[x] = readPixel(kPsmCt32, tex.tbp, tex.tbw, x, y);
                    break;
                case kPsmCt24:
                    row[x] = (readPixel(kPsmCt32, tex.tbp, tex.tbw, x, y) & 0x00FFFFFFu) | 0x80000000u;
                    break;
                case kPsmT8:
                    row[x] = clut[readPixel(kPsmT8, tex.tbp, tex.tbw, x, y) & 0xFFu];
                    break;
                case kPsmT4:
                    row[x] = clut[(csaBase + readPixel(kPsmT4, tex.tbp, tex.tbw, x, y)) & 0xFFu];
                    break;
                default:
                    return false;
                }
            }
        }
        return true;
    }

    void Vram::texturePages(const Tex0 &tex, uint32_t width, uint32_t height, uint32_t &first, uint32_t &count)
    {
        // The lowest and highest byte the texture can touch: the corners, and the far end of each block row, cover it.
        uint32_t low = ~0u, high = 0u;
        auto take = [&](uint32_t x, uint32_t y)
        {
            uint32_t a = 0;
            if (tex.psm == kPsmT4)
                a = (GSPSMT4::addrPSMT4(tex.tbp, tex.tbw, x, y) >> 1);
            else
                a = byteAddress(tex.psm, tex.tbp, tex.tbw, x, y);
            low = std::min(low, a);
            high = std::max(high, a);
        };
        for (uint32_t y = 0; y < height; y += 8)
        {
            take(0, y);
            take(width - 1, y);
        }
        take(0, height - 1);
        take(width - 1, height - 1);
        first = (low & (kBytes - 1u)) >> 13;
        const uint32_t last = (high & (kBytes - 1u)) >> 13;
        count = last >= first ? last - first + 1 : kPages - first;
    }

    uint64_t Vram::version(uint32_t firstPage, uint32_t pageCount) const
    {
        uint64_t v = 0;
        for (uint32_t i = 0; i < pageCount; ++i)
            v = std::max(v, m_pageVersion[(firstPage + i) % kPages]);
        return v;
    }

    uint64_t Vram::contentHash(uint32_t firstPage, uint32_t pageCount) const
    {
        uint64_t h = 0x9E3779B97F4A7C15ull ^ pageCount;
        for (uint32_t i = 0; i < pageCount; ++i)
        {
            const uint32_t page = (firstPage + i) % kPages;
            if (m_pageHashVersion[page] != m_pageVersion[page])
            {
                const uint8_t *bytes = m_memory.data() + static_cast<size_t>(page) * 8192u;
                uint64_t p = 0x243F6A8885A308D3ull ^ page;
                for (uint32_t w = 0; w < 8192u / 8u; ++w)
                {
                    uint64_t word;
                    std::memcpy(&word, bytes + w * 8u, 8);
                    p ^= word * 0xBF58476D1CE4E5B9ull;
                    p = ((p << 27) | (p >> 37)) * 0x94D049BB133111EBull;
                }
                m_pageHash[page] = p;
                m_pageHashVersion[page] = m_pageVersion[page];
            }
            h ^= m_pageHash[page] + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        }
        return h;
    }
}
