// The weather as native code (plan N4): the rain 0x1A8A00 (streaks as lines) and the snow 0x1A9A90 (flakes as sprites),
// both drawn over the terrain quad buffer (docs/RENDER_PIPELINE.md, "Weather and water"). Written from the microcode,
// checked against captured runs (render_vu1.cpp). Their ground passes are terrain passes (render_vu1_terrain.cpp).
//
// Both hash a quad's position into a table index from the low bits of a float's bit pattern, as the microcode does (MTIR
// of a float), so the arithmetic must round as the VU does: toward zero (runVuProgram sets that).

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

        int32_t vi16(const VuMemory &m, uint32_t address, int lane)
        {
            return static_cast<int16_t>(m.word(address, lane));
        }

        // MTIR: the low 16 bits of a float's bits.
        uint32_t lowBits(float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            return bits & 0xFFFF;
        }

        constexpr float kHashOffset = 0.4634700119495392f; // the LOI both programs add before hashing

        class WeatherProgram : public VuProgram
        {
        protected:
            void setupCamera(const VuMemory &m, uint32_t clipMatrix)
            {
                for (int i = 0; i < 4; ++i)
                {
                    m_screen[i] = load(m, 0 + i);
                    m_clip[i] = load(m, clipMatrix + i);
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

            // A point to the screen: its vertex position and fog (a + b / w, within 0..255), and 1 / w.
            float project(const Vec4 &p, VuVertex &v) const
            {
                const Vec4 t = transform(p, m_screen);
                const float q = 1.0f / t.w;
                v.x = t.x * q, v.y = t.y * q, v.z = t.z * q;
                v.fog = std::max(std::min(m_fogA + m_fogB * q, 255.0f), 0.0f);
                return q;
            }

            Vec4 m_screen[4], m_clip[4];
            float m_fogA = 0, m_fogB = 0;
            bool m_ready = false;
        };

        // The rain: eight quads a batch (flags masked by 0x15.x), twelve streaks on each. A streak is a pair of end points
        // (0x26-0x3D) moved to the quad's corner 0 in x and z and down by one of sixteen drop offsets (0x16-0x25) that a
        // hash of the corner picks; when its second end is not above the ground (y not below 0), both ends go up by the
        // offset's x (40). Each end goes out as a line vertex with its colour (0x13 and 0x14 times the ambient light, at
        // most 255, from the setup); the second end's drawing kick is off when either end fails the clip test.
        class RainProgram final : public WeatherProgram
        {
        public:
            void run(VuMemory &m, int32_t entry, uint32_t top, uint32_t /*itop*/, std::vector<VuPacket> &out) override
            {
                if (entry == 0)
                    setup(m);
                else if (entry < 0)
                {
                    if (!m_ready)
                        setup(m);
                    batch(m, top, out);
                }
            }

            bool takeRegisters(const float (&vf)[32][4], const int32_t (&vi)[16]) override
            {
                takeCamera(vf);
                for (int i = 0; i < 4; ++i) // vf17, vf18: the two colours as integers
                {
                    uint32_t a, b;
                    std::memcpy(&a, &vf[17][i], 4);
                    std::memcpy(&b, &vf[18][i], 4);
                    m_colour[0][i] = static_cast<uint8_t>(a);
                    m_colour[1][i] = static_cast<uint8_t>(b);
                }
                m_mask = vi[12];
                return true;
            }

        private:
            void setup(const VuMemory &m)
            {
                setupCamera(m, 4);
                for (int end = 0; end < 2; ++end)
                    for (int i = 0; i < 4; ++i)
                    {
                        float c = m.real(19 + end, i);
                        if (i < 3) // times the ambient, at most 255 (the I register still holds the 255.0 programs leave)
                            c = std::min(c * m.real(15, i), 255.0f);
                        m_colour[end][i] = static_cast<uint8_t>(vuFtoi0(c));
                    }
                m_mask = vi16(m, 21, 0);
            }

            void batch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                VuPacket packet;
                packet.tagLo = static_cast<uint64_t>(m.word(18, 0)) | (static_cast<uint64_t>(m.word(18, 1)) << 32);
                packet.tagHi = static_cast<uint64_t>(m.word(18, 2)) | (static_cast<uint64_t>(m.word(18, 3)) << 32);
                packet.vertices.reserve(8 * 12 * 2);
                for (uint32_t quad = 0; quad < 8; ++quad)
                {
                    const uint32_t q = top + 4u * quad;
                    VuVertex ends[2];
                    ends[0].hasSt = ends[1].hasSt = false;
                    if ((vi16(m, q + 2, 3) & m_mask) == 0)
                    {
                        ends[0].stale = ends[1].stale = true;
                        ends[1].adc = true;
                        for (int streak = 0; streak < 12; ++streak)
                            packet.vertices.insert(packet.vertices.end(), ends, ends + 2);
                        continue;
                    }
                    const Vec4 corner = load(m, q);
                    const float hash = (corner.x + kHashOffset) * (corner.z + kHashOffset);
                    const Vec4 drop = load(m, 22 + (lowBits(hash) & 0xF));
                    for (uint32_t streak = 0; streak < 12; ++streak)
                    {
                        Vec4 a = load(m, 38 + 2 * streak), b = load(m, 39 + 2 * streak);
                        a.x += corner.x, a.z += corner.z, b.x += corner.x, b.z += corner.z;
                        a.y += drop.y, b.y += drop.y;
                        if (!(b.y < 0.0f)) // not above the ground: wrapped up
                        {
                            a.y -= drop.x;
                            b.y -= drop.x;
                        }
                        VuVertex v[2];
                        v[0].hasSt = v[1].hasSt = false;
                        project(a, v[0]);
                        project(b, v[1]);
                        std::copy(m_colour[0], m_colour[0] + 4, v[0].rgba);
                        std::copy(m_colour[1], m_colour[1] + 4, v[1].rgba);
                        if ((clipBits(transform(a, m_clip)) | clipBits(transform(b, m_clip))) != 0)
                        {
                            v[1].adc = true; // ISW.w of 0x8000 over the whole word
                            v[1].fog = 0.0f;
                        }
                        packet.vertices.insert(packet.vertices.end(), v, v + 2);
                    }
                }
                out.push_back(std::move(packet));
            }

            uint8_t m_colour[2][4] = {};
            int32_t m_mask = 0;
        };

        // The snow: eight quads a batch (flags masked by 0x67.y), with as many flakes each as 0x67.x says. A hash of the
        // quad's corner 0 picks one of eight rotation matrices (0x23 on, four qwords apart) and one of 32 offsets (0x47 on);
        // a flake is one of thirteen points (0x16 on) turned by the matrix and moved by the offset, up 40 when not above the
        // ground, then to the corner in x and z. It goes out as a sprite from its point to the point plus 0x15 times 10 / w,
        // with the corner texture coordinates at 0x13 and 0x14 (its colour is the packet before's); the second corner's
        // drawing kick is off when the point fails the clip test (through the matrix at 0x43-0x46).
        class SnowProgram final : public WeatherProgram
        {
        public:
            void run(VuMemory &m, int32_t entry, uint32_t top, uint32_t /*itop*/, std::vector<VuPacket> &out) override
            {
                if (entry == 0)
                    setup(m);
                else if (entry < 0)
                {
                    if (!m_ready)
                        setup(m);
                    batch(m, top, out);
                }
            }

            bool takeRegisters(const float (&vf)[32][4], const int32_t (&vi)[16]) override
            {
                takeCamera(vf);
                m_size = {vf[21][0], vf[21][1], vf[21][2], vf[21][3]};
                m_st[0] = {vf[22][0], vf[22][1], vf[22][2], vf[22][3]};
                m_st[1] = {vf[23][0], vf[23][1], vf[23][2], vf[23][3]};
                m_mask = vi[12];
                return true;
            }

        private:
            void setup(const VuMemory &m)
            {
                setupCamera(m, 67);
                m_size = load(m, 21);
                m_st[0] = load(m, 19);
                m_st[1] = load(m, 20);
                m_mask = vi16(m, 103, 1);
            }

            void batch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                VuPacket packet;
                packet.tagLo = static_cast<uint64_t>(m.word(18, 0)) | (static_cast<uint64_t>(m.word(18, 1)) << 32);
                packet.tagHi = static_cast<uint64_t>(m.word(18, 2)) | (static_cast<uint64_t>(m.word(18, 3)) << 32);
                for (uint32_t quad = 0; quad < 8; ++quad)
                {
                    const uint32_t q = top + 4u * quad;
                    const int32_t flakes = vi16(m, 103, 0);
                    if (flakes <= 0)
                        continue;
                    VuVertex corners[2];
                    for (int k = 0; k < 2; ++k)
                    {
                        corners[k].hasRgba = false;
                        corners[k].s = m_st[k].x, corners[k].t = m_st[k].y, corners[k].q = m_st[k].z;
                    }
                    if ((vi16(m, q + 2, 3) & m_mask) == 0)
                    {
                        corners[0].stale = corners[1].stale = true;
                        corners[1].adc = true;
                        for (int32_t f = 0; f < flakes; ++f)
                            packet.vertices.insert(packet.vertices.end(), corners, corners + 2);
                        continue;
                    }
                    const Vec4 corner = load(m, q);
                    const float hx = corner.x * 1.0e-4f + kHashOffset, hz = corner.z * 1.0e-4f + kHashOffset;
                    const uint32_t bits = lowBits(hx * hz + hz);
                    const uint32_t rotation = 35 + (bits & 0x1C);
                    const Vec4 rows[3] = {load(m, rotation), load(m, rotation + 1), load(m, rotation + 2)};
                    const Vec4 offset = load(m, 71 + (bits & 0x1F));
                    for (int32_t f = 0; f < flakes; ++f)
                    {
                        Vec4 p = load(m, 22 + static_cast<uint32_t>(f));
                        Vec4 r;
                        for (int i = 0; i < 3; ++i)
                            r[i] = ((rows[0][i] * p.x + rows[1][i] * p.y) + rows[2][i] * p.z) + offset[i];
                        r.w = p.w;
                        if (!(r.y < 0.0f)) // not above the ground: wrapped up
                            r.y -= 40.0f;
                        r.x += corner.x, r.z += corner.z;
                        VuVertex v[2] = {corners[0], corners[1]};
                        const float w = project(r, v[0]);
                        Vec4 extent;
                        for (int i = 0; i < 4; ++i)
                            extent[i] = (m_size[i] * w) * 10.0f;
                        v[1].x = v[0].x + extent.x, v[1].y = v[0].y + extent.y, v[1].z = v[0].z + extent.z;
                        v[1].fog = v[0].fog;
                        if (clipBits(transform(r, m_clip)) != 0)
                        {
                            v[1].adc = true; // ISW.w of 0x8000 over the whole word
                            v[1].fog = 0.0f;
                        }
                        packet.vertices.insert(packet.vertices.end(), v, v + 2);
                    }
                }
                out.push_back(std::move(packet));
            }

            Vec4 m_size, m_st[2];
            int32_t m_mask = 0;
        };
    }

    VuProgram *nativeWeatherProgram(uint32_t uploadPacket)
    {
        static RainProgram s_rain;
        static SnowProgram s_snow;
        switch (uploadPacket)
        {
        case 0x001A8A00u:
            return &s_rain;
        case 0x001A9A90u:
            return &s_snow;
        default:
            return nullptr;
        }
    }
}
