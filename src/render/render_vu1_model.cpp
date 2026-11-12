// The model programs 0x1A3C80 and 0x1A4780 as native code (plan N4). 0x1A3C80 draws people, the player, menus and
// windows, the title logo, the HUD's clock and phone; 0x1A4780 the buildings and objects. What they do is in
// docs/RENDER_PIPELINE.md ("Model programs"); written from the microcode, checked against captured runs
// (render_vu1_test.cpp).
//
// VU1 data memory as they use it (qwords):
//   0x00-0x03 screen matrix, 0x04-0x07 clip matrix, 0x08-0x0B light direction matrix, 0x0C-0x0F light colours and
//   ambient, 0x10.w / 0x11.w fog a and b (fog = a + b / w), 0x12-0x29 the colour table, 0x2A the output offsets,
//   0x2B the instance colour (w: its colour-table base), 0x2C-0x2F the instance matrix (0x2F.w: the draw mode), 0x30 the
//   batch's colour, 0x31.w the colour-table base kept by the setup, and the draw mode qword kept at 0x32 (0x1A3C80) or
//   0xC0 (0x1A4780). The setup keeps those because the VIF may unpack the next instance into 0x2B-0x2F while a batch runs.
// A batch at TOP: qword 0 the strip's GIF tag, qword 1 x the vertex count, y the tag word without fog, z a colour-table
// index, w the tag word of a triangle fan (for clipped polygons); then four qwords a vertex: position and normal (fixed
// point, 12 fraction bits), colour (integers), and texture coordinates (floats).

#include "render_vu1.h"

#include <algorithm>
#include <cmath>

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

        // The VU's ITOF12 and ITOF0: integers to floats, with 12 fraction bits or none.
        Vec4 fixed12(const VuMemory &m, uint32_t address)
        {
            const float k = 1.0f / 4096.0f;
            return {m.integer(address, 0) * k, m.integer(address, 1) * k, m.integer(address, 2) * k, m.integer(address, 3) * k};
        }

        Vec4 integers(const VuMemory &m, uint32_t address)
        {
            return {static_cast<float>(m.integer(address, 0)), static_cast<float>(m.integer(address, 1)),
                    static_cast<float>(m.integer(address, 2)), static_cast<float>(m.integer(address, 3))};
        }

        // v.x * m[0] + v.y * m[1] + v.z * m[2] + v.w * m[3], in the order the VU's MULA / MADDA chain adds them.
        Vec4 transform(const Vec4 &v, const Vec4 m[4])
        {
            Vec4 r;
            for (int i = 0; i < 4; ++i)
                r[i] = ((m[0][i] * v.x + m[1][i] * v.y) + m[2][i] * v.z) + m[3][i] * v.w;
            return r;
        }

        Vec4 transform3(const Vec4 &v, const Vec4 m[3])
        {
            Vec4 r;
            for (int i = 0; i < 4; ++i)
                r[i] = (m[0][i] * v.x + m[1][i] * v.y) + m[2][i] * v.z;
            return r;
        }

        // CLIPw.xyz: six bits (+x, -x, +y, -y, +z, -z), set when x, y or z lies outside -|w| .. +|w|.
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

        // A product the VU's MAC flags call positive: not zero, not negative, not below the smallest normal float (which
        // the VU flushes to zero).
        bool positive(float product)
        {
            return product > 0.0f && std::fpclassify(product) == FP_NORMAL;
        }

        uint8_t toByte(float f)
        {
            return static_cast<uint8_t>(static_cast<uint32_t>(vuFtoi0(f)));
        }

        uint64_t tagWords(uint32_t x, uint32_t y) { return static_cast<uint64_t>(x) | (static_cast<uint64_t>(y) << 32); }

        // One corner of a polygon being clipped: as the microcode keeps it, four qwords.
        struct Corner
        {
            Vec4 position; // model space
            Vec4 colour;   // lit, clamped, before the conversion to integers
            Vec4 texture;  // as in the batch
            Vec4 clip;     // position through the clip matrix
        };

        class ModelProgram final : public VuProgram
        {
        public:
            // `modeAddress`: where the setup keeps the draw mode qword; `clipping`: the program has 0x1A4780's three loops
            // (mode 1 clips, mode 0 no fog, others fog) rather than 0x1A3C80's two (mode 0 no fog, others fog).
            // `unlit`: 0x1A8330, 0x1A3C80's no-fog loop with the light replaced by the vertex's own colour (times the batch's
            // red, green and blue only), but for the strip's last vertex, which keeps the lit colour; the draw mode not read.
            ModelProgram(uint32_t modeAddress, bool clipping, bool unlit = false)
                : m_modeAddress(modeAddress), m_clipping(clipping), m_unlit(unlit)
            {
            }

            void run(VuMemory &memory, int32_t entry, uint32_t top, uint32_t /*itop*/, std::vector<VuPacket> &out) override
            {
                if (entry == 0)
                    setup(memory);
                else if (entry < 0)
                    batch(memory, top, out);
            }

            bool takeRegisters(const float (&vf)[32][4], const int32_t (&)[16]) override
            {
                for (int i = 0; i < 4; ++i)
                {
                    m_screen[i] = {vf[1 + i][0], vf[1 + i][1], vf[1 + i][2], vf[1 + i][3]};
                    m_clip[i] = {vf[5 + i][0], vf[5 + i][1], vf[5 + i][2], vf[5 + i][3]};
                }
                for (int i = 0; i < 3; ++i)
                    m_light[i] = {vf[9 + i][0], vf[9 + i][1], vf[9 + i][2], vf[9 + i][3]};
                m_ready = true;
                return true;
            }

        private:
            // MSCAL 0: the instance matrix into the three camera matrices, so the batches work in model space; the
            // instance colour into twelve colour-table entries; the colour-table base and the draw mode kept.
            void setup(VuMemory &m)
            {
                Vec4 instance[4] = {load(m, 44), load(m, 45), load(m, 46), load(m, 47)};
                instance[3].w = 1.0f;
                Vec4 screen[4], clip[4], light[4];
                for (int i = 0; i < 4; ++i)
                {
                    screen[i] = load(m, 0 + i);
                    clip[i] = load(m, 4 + i);
                    light[i] = load(m, 8 + i);
                }
                for (int i = 0; i < 4; ++i)
                {
                    m_screen[i] = transform(instance[i], screen);
                    m_clip[i] = transform(instance[i], clip);
                }
                for (int i = 0; i < 3; ++i)
                    m_light[i] = transform(instance[i], light);
                Vec4 colour = load(m, 43);
                colour.w = 1.0f;
                for (uint32_t entry : {18u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u})
                    for (int lane = 0; lane < 4; ++lane)
                        m.setReal(entry, lane, colour[lane]);
                m.qword(49)[3] = m.qword(43)[3];
                std::copy(m.qword(47), m.qword(47) + 4, m.qword(m_modeAddress));
                if (m_clipping)
                    m.qword(191)[0] = 0; // which of the two clipped-polygon buffers is next
                m_ready = true;
            }

            void batch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                if (!m_ready)
                    setup(m);
                // ILW reads the low 16 bits of a word (VI registers are 16 bits wide)
                const auto vi = [&](uint32_t address, int lane) { return static_cast<int32_t>(static_cast<int16_t>(m.word(address, lane))); };
                // the batch's colour: the table entry at the instance's base plus the batch's index (kept at 0x30)
                const uint32_t colourEntry = (18u + static_cast<uint32_t>(vi(49, 3) + vi(top + 1, 2))) & (kVuQwords - 1);
                m_batchColour = load(m, colourEntry);
                std::copy(m.qword(colourEntry), m.qword(colourEntry) + 4, m.qword(48));
                for (int i = 0; i < 4; ++i)
                    m_lightColour[i] = load(m, 12 + i);
                const int32_t mode = m_unlit ? 0 : vi(m_modeAddress, 3);
                if (m_clipping && mode == 1)
                    clippingBatch(m, top, out);
                else
                    stripBatch(m, top, mode != 0, out);
            }

            // The light at a vertex: its normal into light space, each light's term at least 0, through the light colours
            // plus the ambient.
            Vec4 light(const VuMemory &m, uint32_t normalAddress) const
            {
                Vec4 terms = transform3(fixed12(m, normalAddress), m_light);
                for (int i = 0; i < 4; ++i)
                    terms[i] = std::max(terms[i], 0.0f);
                Vec4 lit;
                for (int i = 0; i < 4; ++i)
                    lit[i] = ((m_lightColour[0][i] * terms.x + m_lightColour[1][i] * terms.y) + m_lightColour[2][i] * terms.z) +
                             m_lightColour[3][i];
                return lit;
            }

            // The fog and no-fog loops (both programs). Every vertex to the screen, every triangle's facing (the cross
            // product of its edges on screen, times the sign in its last vertex's normal w). A vertex is written unless all
            // three triangles it belongs to face away (the last two always are); a written vertex's drawing kick is off
            // when the triangle it ends faces away or one of the last three written vertices failed the clip test.
            void stripBatch(VuMemory &m, uint32_t top, bool fog, std::vector<VuPacket> &out)
            {
                const int32_t count = static_cast<int16_t>(m.word(top + 1, 0));
                if (count < 3)
                    return;
                const uint32_t base = top + 2;
                const float fogA = m.real(16, 3), fogB = m.real(17, 3);
                m_screenPos.resize(static_cast<size_t>(count));
                m_q.resize(static_cast<size_t>(count));
                for (int32_t j = 0; j < count; ++j)
                {
                    const Vec4 t = transform(fixed12(m, base + 4u * j), m_screen);
                    const float q = 1.0f / t.w;
                    m_q[j] = q;
                    m_screenPos[j] = {t.x * q, t.y * q, t.z * q, t.w * q};
                }
                m_facing.assign(static_cast<size_t>(count), 0);
                for (int32_t k = 0; k + 2 < count; ++k)
                {
                    const Vec4 &a = m_screenPos[k], &b = m_screenPos[k + 1], &c = m_screenPos[k + 2];
                    const float e0x = a.x - b.x, e0y = a.y - b.y, e1x = c.x - b.x, e1y = c.y - b.y;
                    const float cross = e1x * e0y - e0x * e1y;
                    const float sign = static_cast<float>(m.integer(base + 4u * (k + 2) + 1, 3)) / 4096.0f;
                    m_facing[k] = positive(cross * sign) ? 1 : 0;
                }
                const auto facing = [&](int32_t k) { return k >= 0 ? m_facing[k] : 0; };

                VuPacket packet;
                packet.tagLo = tagWords(m.word(top, 0), fog ? m.word(top, 1) : m.word(top + 1, 1)); // no fog: qword 1's tag word
                packet.tagHi = tagWords(m.word(top, 2), m.word(top, 3));
                packet.vertices.reserve(static_cast<size_t>(count));
                uint32_t clipHistory = m_clipFlags; // the clip bits of the last three written vertices, newest lowest
                for (int32_t j = 0; j < count; ++j)
                {
                    const bool tail = j >= count - 2;
                    if (!tail && !(facing(j) || facing(j - 1) || facing(j - 2)))
                        continue;
                    const uint32_t v = base + 4u * j;
                    clipHistory = ((clipHistory << 6) | clipBits(transform(fixed12(m, v), m_clip))) & 0xFFFFFF;
                    VuVertex o;
                    const Vec4 &s = m_screenPos[j];
                    o.x = s.x, o.y = s.y, o.z = s.z;
                    o.fog = fog ? std::max(std::min(fogA + fogB * m_q[j], 255.0f), 0.0f) : s.w;
                    o.s = m.real(v + 3, 0) * m_q[j];
                    o.t = m.real(v + 3, 1) * m_q[j];
                    o.q = m.real(v + 3, 2) * m_q[j];
                    const Vec4 lit = light(m, v + 1), colour = integers(m, v + 2);
                    if (m_unlit)
                        for (int i = 0; i < 4; ++i)
                        {
                            const float own = j == count - 1 ? lit[i] * colour[i] : colour[i];
                            o.rgba[i] = toByte(std::min(i < 3 ? own * m_batchColour[i] : own, 255.0f));
                        }
                    else
                        for (int i = 0; i < 4; ++i)
                            o.rgba[i] = toByte(std::min(lit[i] * colour[i] * m_batchColour[i], 255.0f));
                    o.adc = (clipHistory & 0x3FFFF) != 0 || !facing(j - 2);
                    packet.vertices.push_back(o);
                }
                packet.tagLo = (packet.tagLo & ~0xFFFFFFFFull) | 0x8000u | static_cast<uint32_t>(packet.vertices.size()); // NLOOP, EOP
                m_clipFlags = clipHistory;
                out.push_back(std::move(packet));
            }

            // 0x1A4780's mode 1: every vertex is written (no shortening); its drawing kick is off when the triangle it ends
            // faces away or one of its three vertices failed the clip test. A triangle that faces the camera, fails the
            // test and is not wholly outside one plane of the clip volume is clipped against -z, -x, +x, -y and +y, and
            // the polygon left goes out at once as a triangle fan of its own; the strip goes out at the end, with all its
            // vertices and qword 1's tag word.
            void clippingBatch(VuMemory &m, uint32_t top, std::vector<VuPacket> &out)
            {
                const int32_t count = static_cast<int16_t>(m.word(top + 1, 0));
                if (count < 1)
                    return;
                const uint32_t base = top + 2;
                VuPacket strip;
                strip.tagLo = tagWords(m.word(top, 0), m.word(top + 1, 1));
                strip.tagHi = tagWords(m.word(top, 2), m.word(top, 3));
                strip.vertices.reserve(static_cast<size_t>(count));
                Vec4 screen[3] = {}; // the last three vertices on screen, newest first (zero before the first)
                Corner corners[3];   // and as the clipper takes them
                uint32_t clipHistory = m_clipFlags;
                for (int32_t k = 0; k < count; ++k)
                {
                    const uint32_t v = base + 4u * k;
                    const Vec4 position = fixed12(m, v);
                    const Vec4 t = transform(position, m_screen);
                    const float q = 1.0f / t.w;
                    screen[2] = screen[1], screen[1] = screen[0];
                    screen[0] = {t.x * q, t.y * q, t.z * q, t.w * q};
                    const Vec4 lit = light(m, v + 1), vertexColour = integers(m, v + 2);
                    Vec4 colour;
                    for (int i = 0; i < 4; ++i)
                        colour[i] = lit[i] * vertexColour[i] * (i < 3 ? m_batchColour[i] : 1.0f); // alpha: not the batch's
                    for (int i = 0; i < 4; ++i)
                        colour[i] = std::min(colour[i], 255.0f);
                    const Vec4 texture = load(m, v + 3);
                    const Vec4 clip = transform(position, m_clip);
                    clipHistory = ((clipHistory << 6) | clipBits(clip)) & 0xFFFFFF;
                    corners[2] = corners[1], corners[1] = corners[0];
                    corners[0] = {position, colour, texture, clip};

                    VuVertex o;
                    o.x = screen[0].x, o.y = screen[0].y, o.z = screen[0].z, o.fog = screen[0].w;
                    o.s = texture.x * q, o.t = texture.y * q, o.q = texture.z * q;
                    for (int i = 0; i < 4; ++i)
                        o.rgba[i] = toByte(colour[i]);
                    // facing: the cross product of (oldest - middle) and (oldest - newest) on screen, times the three
                    // depths (so a triangle with corners behind the camera turns over) and the newest normal's sign
                    const Vec4 &a = screen[2], &b = screen[1], &c = screen[0];
                    const float e0x = a.x - b.x, e0y = a.y - b.y, e1x = a.x - c.x, e1y = a.y - c.y;
                    const float cross = e0x * e1y - e1x * e0y;
                    const float depths = (c.z * b.z) * a.z;
                    const float sign = static_cast<float>(m.integer(v + 1, 3)) / 4096.0f;
                    const bool away = !positive((depths * cross) * sign);
                    const bool clipped = (clipHistory & 0x3FFFF) != 0;
                    o.adc = away || clipped;
                    strip.vertices.push_back(o);
                    if (away || !clipped)
                        continue;
                    // wholly outside one plane: all three corners' bits for -z, -y, +y, -x or +x
                    bool outside = false;
                    for (uint32_t plane : {0x020820u, 0x008208u, 0x004104u, 0x002082u, 0x001041u})
                        outside = outside || (clipHistory & plane) == plane;
                    if (!outside)
                        clipTriangle(m, top, corners, out);
                }
                m_clipFlags = clipHistory;
                out.push_back(std::move(strip));
            }

            // Sutherland-Hodgman against one plane of the clip volume: `lane` 0 x, 1 y, 2 z; `sign` -1 for the lower plane
            // (lane < -w), +1 for the upper (lane > w). As the microcode: a corner inside goes out, then the crossing
            // point when the next corner is outside; the crossing point alone when this one is outside and the next inside.
            static void clipPass(const std::vector<Corner> &in, std::vector<Corner> &out, int lane, float sign)
            {
                out.clear();
                const auto outside = [&](const Corner &c) {
                    const float w = std::fabs(c.clip.w);
                    return sign < 0 ? c.clip[lane] < -w : c.clip[lane] > w;
                };
                const auto crossing = [&](const Corner &a, const Corner &b) {
                    const float da = a.clip[lane] - a.clip.w * sign, db = b.clip[lane] - b.clip.w * sign;
                    const float t = std::fabs(da / (db - da));
                    Corner c;
                    for (int i = 0; i < 4; ++i)
                    {
                        c.position[i] = (b.position[i] - a.position[i]) * t + a.position[i];
                        c.colour[i] = (b.colour[i] - a.colour[i]) * t + a.colour[i];
                        c.texture[i] = (b.texture[i] - a.texture[i]) * t + a.texture[i];
                        c.clip[i] = (b.clip[i] - a.clip[i]) * t + a.clip[i];
                    }
                    return c;
                };
                const size_t n = in.size();
                for (size_t i = 0; i < n; ++i)
                {
                    const Corner &a = in[i], &b = in[(i + 1) % n];
                    const bool aOut = outside(a), bOut = outside(b);
                    if (!aOut)
                    {
                        out.push_back(a);
                        if (bOut)
                            out.push_back(crossing(a, b));
                    }
                    else if (!bOut)
                        out.push_back(crossing(a, b));
                }
            }

            void clipTriangle(const VuMemory &m, uint32_t top, const Corner (&corners)[3], std::vector<VuPacket> &out)
            {
                m_polygon.assign({corners[2], corners[1], corners[0]}); // oldest first
                static const struct
                {
                    int lane;
                    float sign;
                } kPlanes[5] = {{2, -1.0f}, {0, -1.0f}, {0, 1.0f}, {1, -1.0f}, {1, 1.0f}};
                for (const auto &plane : kPlanes)
                {
                    clipPass(m_polygon, m_spare, plane.lane, plane.sign);
                    m_polygon.swap(m_spare);
                    if (m_polygon.size() < 3)
                        return;
                }
                VuPacket fan;
                fan.tagLo = tagWords(0x8000u | static_cast<uint32_t>(m_polygon.size()), m.word(top + 1, 3));
                fan.tagHi = tagWords(m.word(top, 2), m.word(top, 3));
                for (const Corner &c : m_polygon)
                {
                    const Vec4 t = transform(c.position, m_screen);
                    const float q = 1.0f / t.w;
                    VuVertex o;
                    o.x = t.x * q, o.y = t.y * q, o.z = t.z * q, o.fog = t.w * q;
                    o.s = c.texture.x * q, o.t = c.texture.y * q, o.q = c.texture.z * q;
                    for (int i = 0; i < 4; ++i)
                        o.rgba[i] = toByte(c.colour[i]);
                    fan.vertices.push_back(o);
                }
                out.push_back(std::move(fan));
            }

            uint32_t m_modeAddress;
            bool m_clipping, m_unlit;
            Vec4 m_screen[4], m_clip[4], m_light[3];
            Vec4 m_lightColour[4], m_batchColour;
            bool m_ready = false;
            uint32_t m_clipFlags = 0; // the VU's clip flag register carries over from run to run
            std::vector<Vec4> m_screenPos;
            std::vector<float> m_q;
            std::vector<uint8_t> m_facing;
            std::vector<Corner> m_polygon, m_spare;
        };
    }

    VuProgram *nativeModelProgram0()
    {
        static ModelProgram s_program(0x32, false);
        return &s_program;
    }

    VuProgram *nativeModelProgram1()
    {
        static ModelProgram s_program(0xC0, true);
        return &s_program;
    }

    VuProgram *nativeUnlitModelProgram()
    {
        static ModelProgram s_program(0x32, false, true);
        return &s_program;
    }
}
