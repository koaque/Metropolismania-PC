// The native renderer's GS: GIF packets in, a frame's draw list out. See render_gs.h.

#include "render_gs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace mm::render
{
    namespace
    {
        constexpr uint32_t PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, TEX0_2 = 0x07,
                           CLAMP_1 = 0x08, CLAMP_2 = 0x09, FOG = 0x0A, XYZF3 = 0x0C, XYZ3 = 0x0D, TEX1_1 = 0x14, TEX1_2 = 0x15,
                           XYOFFSET_1 = 0x18, XYOFFSET_2 = 0x19, PRMODECONT = 0x1A, PRMODE = 0x1B, TEXCLUT = 0x1C, TEXA = 0x3B,
                           FOGCOL = 0x3D, SCISSOR_1 = 0x40, SCISSOR_2 = 0x41, ALPHA_1 = 0x42, ALPHA_2 = 0x43, TEST_1 = 0x47,
                           TEST_2 = 0x48, FBA_1 = 0x4A, FBA_2 = 0x4B, FRAME_1 = 0x4C, FRAME_2 = 0x4D, ZBUF_1 = 0x4E, ZBUF_2 = 0x4F, BITBLTBUF = 0x50, TRXPOS = 0x51,
                           TRXREG = 0x52, TRXDIR = 0x53;

        uint64_t mix(uint64_t h, uint64_t v)
        {
            h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        }

        void warnOnce(const char *what)
        {
            static std::unordered_map<std::string, bool> s_seen;
            if (s_seen.emplace(what, true).second)
                std::cerr << "[render] " << what << std::endl;
        }
    }

    bool DrawState::operator==(const DrawState &o) const
    {
        return topology == o.topology && textured == o.textured && tfx == o.tfx && tcc == o.tcc && linear == o.linear &&
               wms == o.wms && wmt == o.wmt && snap == o.snap && hud == o.hud && std::memcmp(region, o.region, sizeof(region)) == 0 &&
               blend == o.blend && fixAlpha == o.fixAlpha && alphaTest == o.alphaTest &&
               alphaRef == o.alphaRef && alphaFail == o.alphaFail && depthTest == o.depthTest && depthWrite == o.depthWrite &&
               colorWrite == o.colorWrite && fog == o.fog && fogColor == o.fogColor && texture == o.texture &&
               std::memcmp(scissor, o.scissor, sizeof(scissor)) == 0;
    }

    // ---- GIF packets ----

    void Gs::gif(const uint8_t *data, size_t bytes)
    {
        size_t pos = 0;
        uint8_t qword[16];
        while (pos < bytes)
        {
            // gather one quadword
            const uint8_t *q;
            if (!m_pending.empty() || bytes - pos < 16)
            {
                const size_t take = std::min<size_t>(16 - m_pending.size(), bytes - pos);
                m_pending.insert(m_pending.end(), data + pos, data + pos + take);
                pos += take;
                if (m_pending.size() < 16)
                    return;
                std::memcpy(qword, m_pending.data(), 16);
                m_pending.clear();
                q = qword;
            }
            else
            {
                q = data + pos;
                pos += 16;
            }
            uint64_t lo, hi;
            std::memcpy(&lo, q, 8);
            std::memcpy(&hi, q + 8, 8);

            if (!m_inPacket)
            {
                m_tagLo = lo;
                m_tagHi = hi;
                m_loops = static_cast<uint32_t>(lo & 0x7FFF);
                const bool pre = ((lo >> 46) & 1) != 0;
                m_mode = static_cast<uint32_t>((lo >> 58) & 3);
                if (m_mode == 3)
                    m_mode = 2;
                m_nreg = static_cast<uint32_t>((lo >> 60) & 0xF);
                if (m_nreg == 0)
                    m_nreg = 16;
                m_regIndex = 0;
                if (pre && m_mode != 2)
                    write(PRIM, (lo >> 47) & 0x7FF);
                m_inPacket = m_loops != 0;
                continue;
            }

            if (m_mode == 2) // IMAGE: the transfer's data
            {
                m_vram.image(q, 16);
                if (--m_loops == 0)
                    m_inPacket = false;
                continue;
            }

            if (m_mode == 1) // REGLIST: two 64-bit register values per quadword
            {
                for (int half = 0; half < 2 && m_inPacket; ++half)
                {
                    const uint32_t reg = static_cast<uint32_t>((m_tagHi >> (4 * m_regIndex)) & 0xF);
                    const uint64_t value = half == 0 ? lo : hi;
                    if (reg != 0xE && reg != 0xF)
                        write(reg, value);
                    if (++m_regIndex >= m_nreg)
                    {
                        m_regIndex = 0;
                        if (--m_loops == 0)
                            m_inPacket = false; // the second half of a last odd quadword is padding
                    }
                }
                continue;
            }

            // PACKED: one register per quadword
            const uint32_t reg = static_cast<uint32_t>((m_tagHi >> (4 * m_regIndex)) & 0xF);
            switch (reg)
            {
            case 0x0:
                write(PRIM, lo & 0x7FF);
                break;
            case 0x1:
            {
                const uint64_t rgba = (lo & 0xFF) | (((lo >> 32) & 0xFF) << 8) | ((hi & 0xFF) << 16) | (((hi >> 32) & 0xFF) << 24);
                uint32_t qbits;
                std::memcpy(&qbits, &m_q, 4);
                write(RGBAQ, rgba | (static_cast<uint64_t>(qbits) << 32));
                break;
            }
            case 0x2:
            {
                uint32_t qbits = static_cast<uint32_t>(hi);
                std::memcpy(&m_q, &qbits, 4);
                write(ST, lo);
                break;
            }
            case 0x3:
                write(UV, (lo & 0x3FFF) | (((lo >> 32) & 0x3FFF) << 16));
                break;
            case 0x4: // XYZF2 (ADC: no draw)
            {
                const uint64_t value = (lo & 0xFFFF) | (((lo >> 32) & 0xFFFF) << 16) | (((hi >> 4) & 0xFFFFFF) << 32) |
                                       (((hi >> 36) & 0xFF) << 56);
                write(((hi >> 47) & 1) ? XYZF3 : XYZF2, value);
                break;
            }
            case 0x5: // XYZ2
            {
                const uint64_t value = (lo & 0xFFFF) | (((lo >> 32) & 0xFFFF) << 16) | ((hi & 0xFFFFFFFF) << 32);
                write(((hi >> 47) & 1) ? XYZ3 : XYZ2, value);
                break;
            }
            case 0x6:
            case 0x7:
            case 0x8:
            case 0x9:
                write(reg, lo);
                break;
            case 0xA:
                write(FOG, ((hi >> 36) & 0xFF) << 56);
                break;
            case 0xC:
            case 0xD:
            {
                const bool fogged = reg == 0xC;
                const uint64_t value = fogged ? ((lo & 0xFFFF) | (((lo >> 32) & 0xFFFF) << 16) | (((hi >> 4) & 0xFFFFFF) << 32) |
                                                 (((hi >> 36) & 0xFF) << 56))
                                              : ((lo & 0xFFFF) | (((lo >> 32) & 0xFFFF) << 16) | ((hi & 0xFFFFFFFF) << 32));
                write(fogged ? XYZF3 : XYZ3, value);
                break;
            }
            case 0xE:
                write(static_cast<uint32_t>(hi & 0xFF), lo);
                break;
            default:
                break;
            }
            if (++m_regIndex >= m_nreg)
            {
                m_regIndex = 0;
                if (--m_loops == 0)
                    m_inPacket = false;
            }
        }
    }

    // ---- registers ----

    void Gs::write(uint32_t reg, uint64_t value)
    {
        switch (reg)
        {
        case PRIM:
            m_prim = value;
            m_queued = 0;
            m_stripIndex = 0;
            break;
        case RGBAQ:
            m_rgbaq = value;
            break;
        case ST:
            m_st = value;
            break;
        case UV:
            m_uv = value;
            break;
        case XYZF2:
        case XYZ2:
        case XYZF3:
        case XYZ3:
        {
            const bool withFog = reg == XYZF2 || reg == XYZF3;
            if (withFog)
                m_fog = value & 0xFF00000000000000ull;
            if (m_queued < 3)
                m_queue[m_queued++] = makeVertex(value, withFog);
            kick(reg == XYZF2 || reg == XYZ2);
            break;
        }
        case TEX0_1:
        case TEX0_2:
        {
            const int ctx = reg == TEX0_2 ? 1 : 0;
            m_tex0[ctx] = value;
            const Tex0 t = Tex0::fromRegister(value);
            bool load = false;
            switch (t.cld)
            {
            case 1:
                load = true;
                break;
            case 2:
                load = true;
                m_cbp0 = t.cbp;
                break;
            case 3:
                load = true;
                m_cbp1 = t.cbp;
                break;
            case 4:
                load = t.cbp != m_cbp0;
                m_cbp0 = t.cbp;
                break;
            case 5:
                load = t.cbp != m_cbp1;
                m_cbp1 = t.cbp;
                break;
            default:
                break;
            }
            if (load && t.indexed())
            {
                m_vram.loadClut(t, m_clut);
                uint64_t h = 0;
                for (uint32_t c : m_clut)
                    h = mix(h, c);
                m_clutHash = h;
            }
            break;
        }
        case CLAMP_1:
        case CLAMP_2:
            m_clamp[reg - CLAMP_1] = value;
            break;
        case FOG:
            m_fog = value;
            break;
        case TEX1_1:
        case TEX1_2:
            m_tex1[reg - TEX1_1] = value;
            break;
        case XYOFFSET_1:
        case XYOFFSET_2:
            m_xyoffset[reg - XYOFFSET_1] = value;
            break;
        case PRMODECONT:
            m_prmodecont = value;
            break;
        case PRMODE:
            m_prmode = value;
            break;
        case TEXCLUT:
            m_texclut = value;
            break;
        case TEXA:
            m_texa = value;
            break;
        case FOGCOL:
            m_fogcol = value;
            break;
        case SCISSOR_1:
        case SCISSOR_2:
            m_scissor[reg - SCISSOR_1] = value;
            break;
        case ALPHA_1:
        case ALPHA_2:
            m_alpha[reg - ALPHA_1] = value;
            break;
        case TEST_1:
        case TEST_2:
            m_test[reg - TEST_1] = value;
            break;
        case FBA_1:
        case FBA_2:
            m_fba[reg - FBA_1] = value;
            break;
        case ZBUF_1:
        case ZBUF_2:
            m_zbuf[reg - ZBUF_1] = value;
            break;
        case FRAME_1:
        case FRAME_2:
            m_frameReg[reg - FRAME_1] = value;
            break;
        case BITBLTBUF:
            m_vram.setBitbltbuf(value);
            break;
        case TRXPOS:
            m_vram.setTrxpos(value);
            break;
        case TRXREG:
            m_vram.setTrxreg(value);
            break;
        case TRXDIR:
            m_vram.setTrxdir(value);
            break;
        default:
            break;
        }
    }

    // ---- vertices and primitives ----

    Vertex Gs::makeVertex(uint64_t xyz, bool withFog)
    {
        const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
        const int ctx = static_cast<int>((attributes >> 9) & 1);
        const uint32_t ofx = static_cast<uint32_t>(m_xyoffset[ctx] & 0xFFFF);
        const uint32_t ofy = static_cast<uint32_t>((m_xyoffset[ctx] >> 32) & 0xFFFF) & ~0xFu; // the half-line offset dropped
        const uint32_t x = static_cast<uint32_t>(xyz & 0xFFFF);
        const uint32_t y = static_cast<uint32_t>((xyz >> 16) & 0xFFFF);
        Vertex v{};
        v.x = (static_cast<float>(x) - static_cast<float>(ofx)) / 16.0f;
        v.y = (static_cast<float>(y) - static_cast<float>(ofy)) / 16.0f * 2.0f; // 224-line field -> 448-line frame
        const uint32_t z = withFog ? static_cast<uint32_t>((xyz >> 32) & 0xFFFFFF) : static_cast<uint32_t>(xyz >> 32);
        v.z = static_cast<float>(static_cast<double>(z) / 4294967296.0);
        v.rgba = static_cast<uint32_t>(m_rgbaq);
        uint32_t qbits = static_cast<uint32_t>(m_rgbaq >> 32);
        float q;
        std::memcpy(&q, &qbits, 4);
        if (((attributes >> 8) & 1) != 0) // FST: texel coordinates in UV
        {
            const Tex0 t = Tex0::fromRegister(m_tex0[ctx]);
            v.s = (static_cast<float>(m_uv & 0x3FFF) / 16.0f) / static_cast<float>(1u << std::min(t.tw, 10u));
            v.t = (static_cast<float>((m_uv >> 16) & 0x3FFF) / 16.0f) / static_cast<float>(1u << std::min(t.th, 10u));
            v.q = 1.0f;
        }
        else
        {
            uint32_t sbits = static_cast<uint32_t>(m_st), tbits = static_cast<uint32_t>(m_st >> 32);
            std::memcpy(&v.s, &sbits, 4);
            std::memcpy(&v.t, &tbits, 4);
            v.q = q != 0.0f ? q : 1.0f;
        }
        v.fog = static_cast<float>((m_fog >> 56) & 0xFF) / 255.0f;
        return v;
    }

    // A vertex from a native VU1 program: as makeVertex, from the float values the program had before the GS's fixed point.
    Vertex Gs::makeVertex(const VuVertex &in)
    {
        const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
        const int ctx = static_cast<int>((attributes >> 9) & 1);
        const float ofx = static_cast<float>(m_xyoffset[ctx] & 0xFFFF) / 16.0f;
        const float ofy = static_cast<float>(static_cast<uint32_t>((m_xyoffset[ctx] >> 32) & 0xFFFF) & ~0xFu) / 16.0f;
        Vertex v{};
        v.x = in.x - ofx;
        v.y = (in.y - ofy) * 2.0f;
        v.z = static_cast<float>(static_cast<double>(std::max(in.z, 0.0f)) / 4294967296.0);
        std::memcpy(&v.rgba, in.rgba, 4);
        v.s = in.s;
        v.t = in.t;
        v.q = in.q != 0.0f ? in.q : 1.0f;
        v.fog = std::clamp(in.fog, 0.0f, 255.0f) / 255.0f;
        return v;
    }

    void Gs::vuPacket(const VuPacket &packet)
    {
        m_fromVu = true;
        struct Done
        {
            bool &flag;
            ~Done() { flag = false; }
        } done{m_fromVu};
        for (const auto &reg : packet.registers)
            write(reg.first, reg.second);
        if (((packet.tagLo >> 46) & 1) != 0) // PRE
            write(PRIM, (packet.tagLo >> 47) & 0x7FF);
        for (const VuVertex &in : packet.vertices)
        {
            for (int i = 0; i < in.beforeCount; ++i)
                if (!in.before[i].stale)
                    write(in.before[i].reg, in.before[i].value);
            if (in.stale)
                continue; // a skipped slot: its kick never draws, and the next PRIM write starts the strip anew
            if (in.hasSt)
                m_q = in.q;
            if (in.hasRgba)
            {
                uint32_t rgba, qbits;
                std::memcpy(&rgba, in.rgba, 4);
                std::memcpy(&qbits, &m_q, 4);
                m_rgbaq = rgba | (static_cast<uint64_t>(qbits) << 32);
            }
            if (m_queued < 3)
            {
                VuVertex v = in;
                if (!in.hasRgba) // the colour the packet before set
                    std::memcpy(v.rgba, &m_rgbaq, 4);
                if (!in.hasSt)
                {
                    std::memcpy(&v.s, &m_st, 4);
                    const uint32_t t = static_cast<uint32_t>(m_st >> 32);
                    std::memcpy(&v.t, &t, 4);
                    v.q = m_q;
                }
                m_queue[m_queued++] = makeVertex(v);
            }
            kick(!in.adc);
        }
    }

    void Gs::kick(bool draw)
    {
        const uint32_t type = static_cast<uint32_t>(m_prim & 7);
        auto shift = [&](int keep) // keep the last `keep` vertices
        {
            for (int i = 0; i < keep; ++i)
                m_queue[i] = m_queue[m_queued - keep + i];
            m_queued = keep;
        };
        switch (type)
        {
        case 0: // point: a one-pixel square
            if (draw)
            {
                Vertex quad[6];
                const Vertex &p = m_queue[0];
                const float dx[6] = {0, 1, 0, 1, 1, 0}, dy[6] = {0, 0, 2, 0, 2, 2};
                for (int i = 0; i < 6; ++i)
                {
                    quad[i] = p;
                    quad[i].x += dx[i];
                    quad[i].y += dy[i];
                }
                emit(quad, 6, Topology::Triangles);
            }
            m_queued = 0;
            break;
        case 1: // line
            if (m_queued == 2)
            {
                if (draw)
                    emit(m_queue, 2, Topology::Lines);
                m_queued = 0;
            }
            break;
        case 2: // line strip
            if (m_queued == 2)
            {
                if (draw)
                    emit(m_queue, 2, Topology::Lines);
                shift(1);
            }
            break;
        case 3: // triangle
            if (m_queued == 3)
            {
                if (draw)
                    emit(m_queue, 3, Topology::Triangles);
                m_queued = 0;
            }
            break;
        case 4: // triangle strip
            if (m_queued == 3)
            {
                if (draw)
                    emit(m_queue, 3, Topology::Triangles);
                shift(2);
            }
            break;
        case 5: // triangle fan
            if (m_queued == 3)
            {
                if (draw)
                    emit(m_queue, 3, Topology::Triangles);
                m_queue[1] = m_queue[2];
                m_queued = 2;
            }
            break;
        case 6: // sprite: two corners, the second one's colour, depth and fog
            if (m_queued == 2)
            {
                if (draw)
                {
                    const Vertex &a = m_queue[0], &b = m_queue[1];
                    Vertex c[4] = {a, b, a, b};
                    for (Vertex &v : c)
                    {
                        v.z = b.z;
                        v.rgba = b.rgba;
                        v.fog = b.fog;
                        v.q = 1.0f;
                    }
                    // The GS fills the pixels from one corner rounded up to the other rounded up (x0 <= x < x1, and the
                    // frame's lines likewise), so the corners go to whole pixels and the texture coordinates move with them;
                    // its Q is the second corner's.
                    const float q = b.q != 0.0f ? b.q : 1.0f;
                    float xa = a.x, xb = b.x, ya = a.y, yb = b.y;
                    float sa = a.s / q, sb = b.s / q, ta = a.t / q, tb = b.t / q;
                    const auto toPixels = [](float &p0, float &p1, float &t0, float &t1)
                    {
                        const float n0 = std::ceil(p0), n1 = std::ceil(p1);
                        if (p1 != p0)
                        {
                            const float k = (t1 - t0) / (p1 - p0);
                            const float u0 = t0 + (n0 - p0) * k, u1 = t0 + (n1 - p0) * k;
                            t0 = u0, t1 = u1;
                        }
                        p0 = n0, p1 = n1;
                    };
                    toPixels(xa, xb, sa, sb);
                    toPixels(ya, yb, ta, tb);
                    if (xa == xb || ya == yb) // no pixel
                    {
                        m_queued = 0;
                        break;
                    }
                    c[0].x = xa, c[0].y = ya, c[0].s = sa, c[0].t = ta;
                    c[1].x = xb, c[1].y = ya, c[1].s = sb, c[1].t = ta;
                    c[2].x = xa, c[2].y = yb, c[2].s = sa, c[2].t = tb;
                    c[3].x = xb, c[3].y = yb, c[3].s = sb, c[3].t = tb;
                    const Vertex tris[6] = {c[0], c[1], c[2], c[1], c[3], c[2]};
                    // One texel to one frame pixel along an axis: sampled at the 1x pixel centres along it.
                    uint8_t snap = 0;
                    const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
                    if (((attributes >> 4) & 1) != 0)
                    {
                        const Tex0 t = Tex0::fromRegister(m_tex0[(attributes >> 9) & 1]);
                        const float texels = std::fabs(c[3].s - c[0].s) * static_cast<float>(1u << std::min(t.tw, 10u));
                        const float lines = std::fabs(c[3].t - c[0].t) * static_cast<float>(1u << std::min(t.th, 10u));
                        const float width = std::fabs(c[3].x - c[0].x), height = std::fabs(c[3].y - c[0].y);
                        if (width > 0.0f && std::fabs(texels - width) < 0.5f)
                            snap |= 1;
                        if (height > 0.0f && std::fabs(lines - height) < 0.5f)
                            snap |= 2;
                    }
                    emit(tris, 6, Topology::Triangles, snap);
                }
                m_queued = 0;
            }
            break;
        default:
            m_queued = 0;
            break;
        }
    }

    uint64_t Gs::textureKey()
    {
        const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
        const int ctx = static_cast<int>((attributes >> 9) & 1);
        const Tex0 t = Tex0::fromRegister(m_tex0[ctx]);
        const uint32_t width = 1u << std::min(t.tw, 10u), height = 1u << std::min(t.th, 10u);
        uint32_t first = 0, count = 0;
        Vram::texturePages(t, width, height, first, count);
        // Two keys: one from the texture's place, format, palette and the version of the memory pages it lies in, which says
        // when to decode again; and the one returned, from its size and decoded pixels, so that a texture whose pages were
        // written (the game uploads glyphs and palettes into shared pages every frame) but whose texels did not change keeps
        // its GPU copy.
        uint64_t where = mix(0, t.tbp);
        where = mix(where, t.tbw);
        where = mix(where, t.psm);
        where = mix(where, t.tw);
        where = mix(where, t.th);
        where = mix(where, m_vram.version(first, count));
        if (t.indexed())
        {
            where = mix(where, t.psm == 0x14 ? t.csa : 0u);
            where = mix(where, m_clutHash);
        }
        auto known = m_contentOf.find(where);
        if (known != m_contentOf.end())
            return known->second;
        TextureImage image;
        image.width = width;
        image.height = height;
        if (!m_vram.decode(t, width, height, m_clut, image.pixels))
        {
            warnOnce("texture format not read yet");
            image.pixels.assign(static_cast<size_t>(width) * height, 0xFFFF00FFu);
        }
        uint64_t key = mix(mix(0x51ED270B27u, width), height);
        for (uint32_t texel : image.pixels)
            key = (key ^ texel) * 0x100000001B3ull;
        key |= 1; // never 0
        if (m_contentOf.size() > 50000) // old places: forget them
            m_contentOf.clear();
        m_contentOf.emplace(where, key);
        if (m_decoded.find(key) == m_decoded.end())
        {
            m_frame.newTextures.emplace(key, std::move(image));
            m_decoded.emplace(key, true);
            if (m_decoded.size() > 20000) // contents not seen for long: forget them (the GPU side drops unused textures too)
                m_decoded.clear();
        }
        return key;
    }

    DrawState Gs::currentState(Topology topology)
    {
        const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
        const int ctx = static_cast<int>((attributes >> 9) & 1);
        DrawState s;
        s.topology = topology;
        if (((attributes >> 4) & 1) != 0) // TME
        {
            const Tex0 t = Tex0::fromRegister(m_tex0[ctx]);
            s.textured = 1;
            s.tfx = static_cast<uint8_t>(t.tfx);
            s.tcc = static_cast<uint8_t>(t.tcc);
            s.linear = static_cast<uint8_t>((m_tex1[ctx] >> 5) & 1);
            if (frameBufferTexture(t, ctx, m_feedbackOrigin))
            {
                // A picture the game drew earlier (the other field buffer): the previous native frame, at its own place.
                s.texture = kPreviousFrame;
                s.wms = s.wmt = 1;
                return finishState(s, attributes, ctx);
            }
            const uint64_t clamp = m_clamp[ctx];
            s.wms = static_cast<uint8_t>(clamp & 3);
            s.wmt = static_cast<uint8_t>((clamp >> 2) & 3);
            if (s.wms >= 2)
            {
                s.region[0] = static_cast<uint16_t>((clamp >> 4) & 0x3FF);
                s.region[1] = static_cast<uint16_t>((clamp >> 14) & 0x3FF);
            }
            if (s.wmt >= 2)
            {
                s.region[2] = static_cast<uint16_t>((clamp >> 24) & 0x3FF);
                s.region[3] = static_cast<uint16_t>((clamp >> 34) & 0x3FF);
            }
            s.texture = textureKey();
        }
        return finishState(s, attributes, ctx);
    }

    // A texture that starts in one of the frame buffers the game drew into in an earlier frame. `origin` gets the texture's
    // place in that buffer, in pixels and field lines.
    bool Gs::frameBufferTexture(const Tex0 &t, int ctx, float origin[2]) const
    {
        if (t.psm != 0x00 || (t.tbp % 32u) != 0u) // CT32 from a page start: the only case the game has
            return false;
        for (const DrawBuffer &buffer : m_drawBuffers)
        {
            if (buffer.width == 0)
                continue;
            const uint32_t first = buffer.page * 32u, end = (buffer.page + buffer.width * buffer.rows) * 32u;
            if (t.tbp < first || t.tbp >= end)
                continue;
            const uint32_t page = (t.tbp - first) / 32u;
            origin[0] = static_cast<float>((page % buffer.width) * 64u);
            origin[1] = static_cast<float>((page / buffer.width) * 32u);
            const uint32_t height = 1u << std::min(t.th, 10u);
            if (height > 32u && t.tbw != buffer.width)
                warnOnce("frame buffer read as a texture with another width, drawn as if it had the buffer's");
            if (buffer.page == static_cast<uint32_t>(m_frameReg[ctx] & 0x1FF))
                warnOnce("the frame buffer being drawn read as a texture, drawn from the previous frame");
            return true;
        }
        return false;
    }

    DrawState Gs::finishState(DrawState &s, uint64_t attributes, int ctx)
    {
        if (((attributes >> 6) & 1) != 0) // ABE
        {
            const uint64_t alpha = m_alpha[ctx];
            const uint32_t a = alpha & 3, b = (alpha >> 2) & 3, c = (alpha >> 4) & 3, d = (alpha >> 6) & 3;
            s.fixAlpha = static_cast<uint8_t>((alpha >> 32) & 0xFF);
            if (a == 0 && b == 1 && c == 0 && d == 1)
                s.blend = BlendMix;
            else if (a == 0 && b == 2 && c == 0 && d == 1)
                s.blend = BlendAdd;
            else if (a == 0 && b == 1 && c == 2 && d == 1)
                s.blend = BlendMixFix;
            else if (a == 0 && b == 2 && c == 2 && d == 1)
                s.blend = BlendAddFix;
            else if (a == 2 && b == 0 && c == 0 && d == 1)
                s.blend = BlendSubtract;
            else
            {
                warnOnce("blend equation not mapped yet, drawn as a mix");
                s.blend = BlendMix;
            }
        }
        const uint64_t test = m_test[ctx];
        const bool ate = (test & 1) != 0;
        const uint32_t atst = (test >> 1) & 7, afail = (test >> 12) & 3;
        s.alphaRef = static_cast<uint8_t>((test >> 4) & 0xFF);
        s.alphaTest = static_cast<uint8_t>(ate ? atst : 1u);
        s.alphaFail = static_cast<uint8_t>(afail);
        s.depthTest = static_cast<uint8_t>(((test >> 16) & 1) ? ((test >> 17) & 3) : 1u);
        s.depthWrite = ((m_zbuf[ctx] >> 32) & 1) ? 0 : 1;
        if (ate && atst == 0) // every pixel fails: AFAIL says what is still written
        {
            s.alphaTest = 1;
            if (afail == 1) // frame buffer only
                s.depthWrite = 0;
            else if (afail == 2) // Z only
                s.colorWrite = 0;
            else if (afail == 3) // RGB only
                s.depthWrite = 0;
            else
                s.colorWrite = 0, s.depthWrite = 0;
        }
        else if (ate && atst != 1 && afail != 0)
            warnOnce("alpha test with a fail mode other than keep, drawn as keep");
        if (((attributes >> 5) & 1) != 0) // FGE
        {
            s.fog = 1;
            s.fogColor = static_cast<uint32_t>(m_fogcol & 0xFFFFFF);
        }
        const uint64_t sc = m_scissor[ctx];
        s.scissor[0] = static_cast<uint16_t>(sc & 0x7FF);
        s.scissor[1] = static_cast<uint16_t>((sc >> 16) & 0x7FF);
        s.scissor[2] = static_cast<uint16_t>(((sc >> 32) & 0x7FF) * 2);
        s.scissor[3] = static_cast<uint16_t>(((sc >> 48) & 0x7FF) * 2 + 1);
        return s;
    }

    void Gs::emit(const Vertex *v, int count, Topology topology, uint8_t snap)
    {
        DrawState state = currentState(topology);
        if (state.textured && state.texture != kPreviousFrame)
            state.snap = snap;
        if (m_span != 0 && !(state.textured && state.texture == kPreviousFrame))
        {
            if (m_span == 2)
                state.hud = 2;
            else if (m_fromVu)
                state.hud = 1; // a model: the whole of it the same way
            else
            {
                float minX = v[0].x, maxX = v[0].x;
                for (int i = 1; i < count; ++i)
                    minX = std::min(minX, v[i].x), maxX = std::max(maxX, v[i].x);
                state.hud = minX <= 0.5f && maxX >= 639.5f ? 0 : 1; // across the whole width: a background, a fade, the Z clear
            }
        }
        if (state.colorWrite == 0 && state.depthWrite == 0)
            return;
        const uint64_t attributes = (m_prmodecont & 1) ? m_prim : m_prmode;
        const bool flat = ((attributes >> 3) & 1) == 0;
        const uint32_t first = static_cast<uint32_t>(m_frame.vertices.size());
        const bool previousFrame = state.texture == kPreviousFrame;
        float size[2] = {1.0f, 1.0f};
        if (previousFrame)
        {
            const uint64_t attributesNow = (m_prmodecont & 1) ? m_prim : m_prmode;
            const Tex0 t = Tex0::fromRegister(m_tex0[(attributesNow >> 9) & 1]);
            size[0] = static_cast<float>(1u << std::min(t.tw, 10u));
            size[1] = static_cast<float>(1u << std::min(t.th, 10u));
        }
        for (int i = 0; i < count; ++i)
        {
            Vertex out = v[i];
            if (flat)
                out.rgba = v[count - 1].rgba; // the GS shades a flat primitive with its last vertex
            if (previousFrame) // texture coordinates -> the frame's, 0..1 over 640 x 224 (s and t keep their q)
            {
                out.s = (m_feedbackOrigin[0] * out.q + out.s * size[0]) / 640.0f;
                out.t = (m_feedbackOrigin[1] * out.q + out.t * size[1]) / 224.0f;
            }
            m_frame.vertices.push_back(out);
        }
        if (!m_frame.batches.empty() && m_frame.batches.back().state == state)
            m_frame.batches.back().count += static_cast<uint32_t>(count);
        else
            m_frame.batches.push_back(Batch{state, first, static_cast<uint32_t>(count)});
    }

    Frame Gs::takeFrame()
    {
        // The buffer this frame was drawn into, for later frames that read it back.
        {
            const uint32_t page = static_cast<uint32_t>(m_frameReg[0] & 0x1FF);
            const uint32_t width = static_cast<uint32_t>((m_frameReg[0] >> 16) & 0x3F);
            const uint32_t lines = static_cast<uint32_t>((m_scissor[0] >> 48) & 0x7FF) + 1u;
            bool known = false;
            for (DrawBuffer &buffer : m_drawBuffers)
                if (buffer.page == page && buffer.width != 0)
                {
                    buffer.width = width, buffer.rows = (lines + 31u) / 32u;
                    known = true;
                }
            if (!known && width != 0)
            {
                m_drawBuffers[m_nextDrawBuffer] = DrawBuffer{page, width, (lines + 31u) / 32u};
                m_nextDrawBuffer = (m_nextDrawBuffer + 1) % 4;
            }
        }
        Frame out = std::move(m_frame);
        m_frame = Frame{};
        m_frame.number = out.number + 1;
        m_span = 0;
        return out;
    }
}
