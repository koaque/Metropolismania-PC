// The terrain passes as native code (plan N4): the base ground 0x1A5EB0, the water bottom 0x1AA340, the lit roads
// 0x1A91C0 (quads) and the cliff sides 0x1A76F0 (triangles). What they do is in docs/RENDER_PIPELINE.md ("Terrain: one
// quad buffer, many passes"); written from the microcode, checked against captured runs (render_vu1.cpp).
//
// VU1 data memory as they use it (qwords): 0x00-0x03 screen matrix, 0x04-0x07 clip matrix, 0x08-0x0B light direction
// matrix, 0x0C-0x0F light colours and ambient, 0x10.w / 0x11.w fog a and b; the pass's parameters: 0x1C colour scale,
// 0x1D TEX0, 0x1E.x output offset, 0x1F the GIF tag (NLOOP: the quads or triangles a batch holds), 0x20.x PRIM (the cliff
// sides: 0x20.y scales a corner's height into its texture t), 0x21-0x2C texture-coordinate sets, 0x2D.w the flag mask,
// 0x2E.x / .y the water bottom's height scale and offset. A batch at TOP: four qwords a quad (corners as floats, their w
// words: TEX0's low word, the texture-coordinate set's address, the flags, 1.0), or three a triangle.

#include "render_vu1.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mm::render
{
    namespace
    {
        struct Vec4
        {
            float x = 0, y = 0, z = 0, w = 0;
            float &operator[](int i) { return (&x)[i]; }
            float operator[](int i) const { return (&x)[i]; }
            Vec4 operator-(const Vec4 &o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
        };

        Vec4 load(const VuMemory &m, uint32_t address)
        {
            return {m.real(address, 0), m.real(address, 1), m.real(address, 2), m.real(address, 3)};
        }

        Vec4 transform(const Vec4 &v, const Vec4 m[4])
        {
            Vec4 r;
            for (int i = 0; i < 4; ++i)
                r[i] = ((m[0][i] * v.x + m[1][i] * v.y) + m[2][i] * v.z) + m[3][i] * v.w;
            return r;
        }

        // a x b, as OPMULA / OPMSUB give it
        Vec4 cross(const Vec4 &a, const Vec4 &b)
        {
            return {a.y * b.z - b.y * a.z, a.z * b.x - b.z * a.x, a.x * b.y - b.x * a.y, 0.0f};
        }

        // ESADD, ERSQRT: the vector over its length
        Vec4 normalized(const Vec4 &v)
        {
            const float k = 1.0f / std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return {v.x * k, v.y * k, v.z * k, v.w};
        }

        uint32_t clipBits(const Vec4 &v)
        {
            const float w = std::fabs(v.w);
            uint32_t bits = 0;
            bits |= v.x > w ? 1u : 0u;
            bits |= v.x < -w ? 2u : 0u;
            bits |= v.y > w ? 4u : 0u;
            bits |= v.y < -w ? 8u : 0u;
            bits |= v.z > w ? 16u : 0u;
            bits |= v.z < -w ? 32u : 0u;
            return bits;
        }

        uint8_t toByte(float f)
        {
            return static_cast<uint8_t>(static_cast<uint32_t>(vuFtoi0(f)));
        }

        int32_t vi16(const VuMemory &m, uint32_t address, int lane)
        {
            return static_cast<int16_t>(m.word(address, lane));
        }

        // What every terrain pass shares: the camera, the fog, the light.
        class TerrainProgram : public VuProgram
        {
        protected:
            void setupCamera(const VuMemory &m)
            {
                for (int i = 0; i < 4; ++i)
                {
                    m_screen[i] = load(m, 0 + i);
                    m_clip[i] = load(m, 4 + i);
                }
                m_fogA = m.real(16, 3);
                m_fogB = m.real(17, 3);
                m_ready = true;
            }

            void takeCamera(const float (&vf)[32][4])
            {
                for (int i = 0; i < 4; ++i)
                {
                    m_screen[i] = {vf[1 + i][0], vf[1 + i][1], vf[1 + i][2], vf[1 + i][3]};
                    m_clip[i] = {vf[5 + i][0], vf[5 + i][1], vf[5 + i][2], vf[5 + i][3]};
                }
                m_fogA = vf[9][3];
                m_fogB = vf[10][3];
                m_ready = true;
            }

            // A corner to the screen: position, 1 / w, fog (a + b / w, within 0..255).
            void project(const Vec4 &corner, Vec4 &screen, float &q, float &fog) const
            {
                const Vec4 t = transform(corner, m_screen);
                q = 1.0f / t.w;
                screen = {t.x * q, t.y * q, t.z * q, t.w * q};
                fog = std::max(std::min(m_fogA + m_fogB * q, 255.0f), 0.0f);
            }

            // A face's colour: its normal through the light direction matrix, each term at least 0, through the light
            // colours plus the ambient, times the pass's colour scale, at most 255.
            static void faceColour(const VuMemory &m, const Vec4 &normal, uint8_t rgba[4])
            {
                Vec4 terms;
                for (int i = 0; i < 4; ++i)
                    terms[i] = std::max((m.real(8, i) * normal.x + m.real(9, i) * normal.y) + m.real(10, i) * normal.z, 0.0f);
                for (int i = 0; i < 4; ++i)
                {
                    const float c = ((m.real(12, i) * terms.x + m.real(13, i) * terms.y) + m.real(14, i) * terms.z) + m.real(15, i);
                    rgba[i] = toByte(std::min(m.real(28, i) * c, 255.0f));
                }
            }

            uint32_t clipOf(const Vec4 &corner) const { return clipBits(transform(corner, m_clip)); }

            static uint64_t tagWord(const VuMemory &m, uint32_t address, int lane)
            {
                return static_cast<uint64_t>(m.word(address, lane)) | (static_cast<uint64_t>(m.word(address, lane + 1)) << 32);
            }

            Vec4 m_screen[4], m_clip[4];
            float m_fogA = 0, m_fogB = 0;
            bool m_ready = false;
        };

        // The quad passes. Per quad: TEX0 and PRIM, then the corners in strip order 0, 1, 3, 2 with the quad's
        // texture-coordinate set in that order; lit passes give corners 0 and 1 the colour of the face (0, 1, 3) and
        // corners 3 and 2 that of (1, 3, 2), each from its normalized normal. The last two corners' drawing kick is off when
        // any corner fails the clip test; a quad whose flags miss the pass's mask is left as it was in the output buffer but
        // for those two ADC bits.
        class QuadPass final : public TerrainProgram
        {
        public:
            enum class Texture
            {
                QuadLowWord, // TEX0's low word from the quad, the high word from 0x1D
                Whole,       // TEX0 whole from 0x1D
            };

            QuadPass(Texture texture, bool lit, bool lowered) : m_texture(texture), m_lit(lit), m_lowered(lowered) {}

            void run(VuMemory &m, int32_t entry, uint32_t top, uint32_t /*itop*/, std::vector<VuPacket> &out) override
            {
                if (entry == 0)
                {
                    setupCamera(m);
                    if (!m_lit)
                        for (int i = 0; i < 4; ++i) // the unlit pass takes 0x1C as its colour, as integers, at setup
                            m_constant[i] = toByte(m.real(28, i));
                }
                else if (entry < 0)
                {
                    if (!m_ready)
                        setupCamera(m);
                    batch(m, top, out);
                }
            }

            bool takeRegisters(const float (&vf)[32][4]) override
            {
                takeCamera(vf);
                if (!m_lit) // vf27: FTOI0 of 0x1C
                    for (int i = 0; i < 4; ++i)
                    {
                        uint32_t bits;
                        std::memcpy(&bits, &vf[27][i], 4);
                        m_constant[i] = static_cast<uint8_t>(bits);
                    }
                return true;
            }

        private:
            void batch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                const int32_t count = vi16(m, 31, 0) & 0x7FFF;
                const int32_t mask = vi16(m, 45, 3);
                const uint32_t prim = static_cast<uint16_t>(m.word(32, 0));
                VuPacket packet;
                packet.tagLo = tagWord(m, 31, 0);
                packet.tagHi = tagWord(m, 31, 2);
                packet.vertices.reserve(static_cast<size_t>(count) * 4);
                for (int32_t i = 0; i < count; ++i)
                {
                    const uint32_t q = top + 4u * static_cast<uint32_t>(i);
                    VuVertex corner[4]; // in strip order
                    corner[0].beforeCount = 2;
                    corner[0].before[1] = {0x00, prim, false}; // PRIM, written for every quad
                    if ((vi16(m, q + 2, 3) & mask) == 0)
                    {
                        corner[0].before[0].stale = true;
                        for (VuVertex &v : corner)
                            v.stale = true;
                        corner[2].adc = corner[3].adc = true;
                        packet.vertices.insert(packet.vertices.end(), corner, corner + 4);
                        continue;
                    }
                    Vec4 c[4];
                    for (int k = 0; k < 4; ++k)
                    {
                        c[k] = load(m, q + k);
                        c[k].w = 1.0f;
                        if (m_lowered) // the water bottom: each height scaled and offset by 0x2E
                            c[k].y = c[k].y * m.real(46, 0) + m.real(46, 1);
                    }
                    const uint64_t low = m_texture == Texture::QuadLowWord ? m.word(q, 3) : m.word(29, 0);
                    corner[0].before[0] = {0x06, low | (static_cast<uint64_t>(m.word(29, 1)) << 32), false}; // TEX0_1
                    const uint32_t set = static_cast<uint16_t>(m.word(q + 1, 3));
                    static const int kOrder[4] = {0, 1, 3, 2}; // strip slot -> corner
                    uint32_t clip = 0;
                    for (int slot = 0; slot < 4; ++slot)
                    {
                        const Vec4 &p = c[kOrder[slot]];
                        Vec4 screen;
                        float w, fog;
                        project(p, screen, w, fog);
                        VuVertex &v = corner[slot];
                        v.x = screen.x, v.y = screen.y, v.z = screen.z, v.fog = fog;
                        v.s = m.real(set + slot, 0) * w;
                        v.t = m.real(set + slot, 1) * w;
                        v.q = m.real(set + slot, 2) * w;
                        clip |= clipOf(p);
                    }
                    if (m_lit)
                    {
                        faceColour(m, normalized(cross(c[0] - c[1], c[3] - c[0])), corner[0].rgba);
                        faceColour(m, normalized(cross(c[3] - c[1], c[2] - c[1])), corner[2].rgba);
                        std::copy(corner[0].rgba, corner[0].rgba + 4, corner[1].rgba);
                        std::copy(corner[2].rgba, corner[2].rgba + 4, corner[3].rgba);
                    }
                    else
                        for (VuVertex &v : corner)
                            std::copy(m_constant, m_constant + 4, v.rgba);
                    if (clip != 0) // ISW.w of 0x8000 over the whole word: F goes to 0 with it
                    {
                        corner[2].adc = corner[3].adc = true;
                        corner[2].fog = corner[3].fog = 0.0f;
                    }
                    packet.vertices.insert(packet.vertices.end(), corner, corner + 4);
                }
                out.push_back(std::move(packet));
            }

            Texture m_texture;
            bool m_lit, m_lowered;
            uint8_t m_constant[4] = {};
        };

        // The cliff sides 0x1A76F0. Per triangle: TEX0 (low word from the triangle, high from 0x1D), its corners with the
        // set's s and q and a t from the corner's height (times 0x20.y), one colour from the face's normalized normal; the
        // third corner's drawing kick is off when a corner fails the clip test or the triangle turns away on screen. A
        // triangle without its flags is left as it was in the output buffer but for that ADC bit.
        class SidePass final : public TerrainProgram
        {
        public:
            void run(VuMemory &m, int32_t entry, uint32_t top, uint32_t /*itop*/, std::vector<VuPacket> &out) override
            {
                if (entry == 0)
                {
                    setupCamera(m);
                    m_heightScale = m.real(32, 1);
                }
                else if (entry < 0)
                {
                    if (!m_ready)
                        setup(m);
                    batch(m, top, out);
                }
            }

            bool takeRegisters(const float (&vf)[32][4]) override
            {
                takeCamera(vf);
                m_heightScale = vf[29][1];
                return true;
            }

        private:
            void setup(const VuMemory &m)
            {
                setupCamera(m);
                m_heightScale = m.real(32, 1);
            }

            void batch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                const int32_t count = vi16(m, 31, 0) & 0x7FFF;
                VuPacket packet;
                packet.tagLo = tagWord(m, 31, 0);
                packet.tagHi = tagWord(m, 31, 2);
                packet.vertices.reserve(static_cast<size_t>(count) * 3);
                for (int32_t i = 0; i < count; ++i)
                {
                    const uint32_t t = top + 3u * static_cast<uint32_t>(i);
                    VuVertex corner[3];
                    corner[0].beforeCount = 1;
                    if (vi16(m, t + 2, 3) == 0)
                    {
                        corner[0].before[0].stale = true;
                        for (VuVertex &v : corner)
                            v.stale = true;
                        corner[2].adc = true;
                        packet.vertices.insert(packet.vertices.end(), corner, corner + 3);
                        continue;
                    }
                    Vec4 c[3], s[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        c[k] = load(m, t + k);
                        c[k].w = 1.0f;
                    }
                    corner[0].before[0] = {0x06, m.word(t, 3) | (static_cast<uint64_t>(m.word(29, 1)) << 32), false}; // TEX0_1
                    const uint32_t set = static_cast<uint16_t>(m.word(t + 1, 3));
                    uint8_t rgba[4];
                    faceColour(m, normalized(cross(c[0] - c[1], c[2] - c[0])), rgba);
                    uint32_t clip = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        float w, fog;
                        project(c[k], s[k], w, fog);
                        VuVertex &v = corner[k];
                        v.x = s[k].x, v.y = s[k].y, v.z = s[k].z, v.fog = fog;
                        v.s = m.real(set + k, 0) * w;
                        v.t = (c[k].y * m_heightScale) * w;
                        v.q = m.real(set + k, 2) * w;
                        std::copy(rgba, rgba + 4, v.rgba);
                        clip |= clipOf(c[k]);
                    }
                    // turned away: the cross product of (0 - 1) and (0 - 2) on screen below zero
                    const float e0x = s[0].x - s[1].x, e0y = s[0].y - s[1].y, e1x = s[0].x - s[2].x, e1y = s[0].y - s[2].y;
                    const bool away = e0x * e1y - e1x * e0y < 0.0f;
                    if (clip != 0 || away) // ISW.w of 0x8000 over the whole word: F goes to 0 with it
                    {
                        corner[2].adc = true;
                        corner[2].fog = 0.0f;
                    }
                    packet.vertices.insert(packet.vertices.end(), corner, corner + 3);
                }
                out.push_back(std::move(packet));
            }

            float m_heightScale = 0;
        };
    }

    VuProgram *nativeTerrainProgram(uint32_t uploadPacket)
    {
        static QuadPass s_ground(QuadPass::Texture::QuadLowWord, true, false);
        static QuadPass s_bottom(QuadPass::Texture::Whole, true, true);
        static QuadPass s_roads(QuadPass::Texture::Whole, false, false);
        static SidePass s_sides;
        switch (uploadPacket)
        {
        case 0x001A5EB0u:
            return &s_ground;
        case 0x001AA340u:
            return &s_bottom;
        case 0x001A91C0u:
            return &s_roads;
        case 0x001A76F0u:
            return &s_sides;
        default:
            return nullptr;
        }
    }
}
