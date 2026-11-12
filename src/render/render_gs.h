#pragma once

// The native renderer's GS (plan N3): the registers the game sets, primitive assembly from vertex kicks, and the frame's draw
// list. Fed by GIF packets in the order the game sends them; image transfers go to the renderer's VRAM copy.

#include "render_vram.h"
#include "render_vu1.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm::render
{
    // One vertex of the frame, in a progressive 640 x 448 frame (the field's half-line offset dropped).
    struct Vertex
    {
        float x, y;       // pixels
        float z;          // GS Z / 2^32 (the depth test compares as the GS does: larger is nearer)
        uint32_t rgba;    // as the GS has it: 0x80 is 1.0
        float s, t, q;    // texture coordinates, divided per pixel (s/q, t/q are 0..1 across the texture)
        float fog;        // 0..1, 1 = no fog
    };

    enum class Topology : uint8_t
    {
        Triangles,
        Lines,
    };

    // Everything a draw needs besides its vertices; draws with equal state go in one batch.
    struct DrawState
    {
        Topology topology = Topology::Triangles;
        uint8_t textured = 0;
        uint8_t tfx = 0, tcc = 0;
        uint8_t linear = 0;            // bilinear filtering
        uint8_t wms = 0, wmt = 0;      // CLAMP's wrap modes: 0 repeat, 1 clamp, 2 region clamp, 3 region repeat
        uint8_t snap = 0;              // 1 x, 2 y: a sprite drawn one texel to one frame pixel, sampled at the 1x pixel centre
        uint16_t region[4] = {};       // MINU, MAXU, MINV, MAXV
        uint8_t hud = 0;               // the city's HUD (Gs::beginSpan), not a frame-buffer read: on a widened frame it keeps
                                       // its 4:3 proportions. 1 the HUD (2D: not across the whole width), 2 a backdrop, whose
                                       // points at or past the 4:3 edges go out to the wide edges
        uint8_t blend = 0;             // 0 off, else a BlendMode
        uint8_t fixAlpha = 0;          // FIX of the blend, when it uses one
        uint8_t alphaTest = 1;         // ATST when the test is on, 1 (always) when off
        uint8_t alphaRef = 0;
        uint8_t alphaFail = 0;         // AFAIL
        uint8_t depthTest = 1;         // ZTST (1 always when off)
        uint8_t depthWrite = 1;
        uint8_t colorWrite = 1;
        uint8_t fog = 0;
        uint32_t fogColor = 0;
        uint64_t texture = 0;          // texture cache key, 0 for none
        uint16_t scissor[4] = {0, 639, 0, 447}; // x0, x1, y0, y1 in frame pixels

        bool operator==(const DrawState &o) const;
    };

    enum BlendMode : uint8_t
    {
        BlendNone = 0,
        BlendMix,          // (Cs - Cd) * As + Cd
        BlendAdd,          // Cs * As + Cd
        BlendMixFix,       // (Cs - Cd) * FIX + Cd
        BlendAddFix,       // Cs * FIX + Cd
        BlendSubtract,     // Cd - Cs * As
    };

    // The texture key of draws that read a frame buffer the game drew into earlier: the previous native frame.
    constexpr uint64_t kPreviousFrame = 2; // texture contents have odd keys

    struct Batch
    {
        DrawState state;
        uint32_t first = 0; // first vertex
        uint32_t count = 0;
    };

    // A texture the frame uses: decoded once per change of its pixels or palette.
    struct TextureImage
    {
        uint32_t width = 0, height = 0;
        std::vector<uint32_t> pixels;
    };

    struct Frame
    {
        std::vector<Vertex> vertices;
        std::vector<Batch> batches;
        std::unordered_map<uint64_t, TextureImage> newTextures; // decoded this frame, to be uploaded
        uint64_t number = 0;
        std::string shot; // a file to write the frame to once drawn (requestShot)
        bool wide = false; // drawn with the city's widened view (MMPC_WIDESCREEN): shown that much wider
    };

    class Gs
    {
    public:
        explicit Gs(Vram &vram) : m_vram(vram) {}

        // GIF packets (path 2 or 3): tags and their data. Partial packets are kept until the rest arrives.
        void gif(const uint8_t *data, size_t bytes);
        // A register write as A+D.
        void write(uint32_t reg, uint64_t value);
        // What a native VU1 program sends: its A+D writes, its GIF tag and its vertices, with float positions.
        void vuPacket(const VuPacket &packet);
        // A context's drawing environment as the emulated GS has it (the game sets it outside its DMA transfers too).
        void setEnvironment(int ctx, uint64_t frame, uint64_t xyoffset, uint64_t scissor, uint64_t zbuf, uint64_t test)
        {
            m_frameReg[ctx & 1] = frame;
            m_xyoffset[ctx & 1] = xyoffset;
            m_scissor[ctx & 1] = scissor;
            m_zbuf[ctx & 1] = zbuf;
            m_test[ctx & 1] = test;
        }

        // The frame being built, handed over and started anew.
        Frame takeFrame();
        // The frame list reached a span the port marked (render_vif.h kSpanMark, port::enhance::Span): what follows, but for
        // frame-buffer reads, is marked DrawState::hud. Until the next span or the frame's end.
        void beginSpan(uint8_t span) { m_span = span; }
        // Texture contents the GPU side let go: they are decoded and sent again if a draw uses them.
        void forget(const std::vector<uint64_t> &keys)
        {
            for (uint64_t key : keys)
                m_decoded.erase(key);
            m_contentOf.clear();
            m_contentOfBytes.clear();
        }
        Frame &frame() { return m_frame; }

    private:
        void kick(bool draw);
        void emit(const Vertex *v, int count, Topology topology, uint8_t snap = 0);
        DrawState currentState(Topology topology);
        DrawState finishState(DrawState &s, uint64_t attributes, int ctx);
        bool frameBufferTexture(const Tex0 &t, int ctx, float origin[2]) const;
        Vertex makeVertex(uint64_t xyz, bool withFog);
        Vertex makeVertex(const VuVertex &v);
        uint64_t textureKey();

        Vram &m_vram;
        Frame m_frame;

        // registers (context 1 and 2 where the GS has two)
        uint64_t m_prim = 0, m_rgbaq = 0, m_st = 0, m_uv = 0, m_fog = 0;
        uint64_t m_prmodecont = 1, m_prmode = 0;
        uint64_t m_tex0[2] = {}, m_tex1[2] = {}, m_clamp[2] = {}, m_alpha[2] = {}, m_test[2] = {}, m_zbuf[2] = {};
        uint64_t m_xyoffset[2] = {}, m_scissor[2] = {0x000001BF0000027Full, 0x000001BF0000027Full}, m_fba[2] = {};
        uint64_t m_fogcol = 0, m_texa = 0, m_texclut = 0;
        uint64_t m_frameReg[2] = {};

        // the frame buffers drawn into in earlier frames (pages, width in pages, height in page rows)
        struct DrawBuffer
        {
            uint32_t page = 0, width = 0, rows = 0;
        };
        DrawBuffer m_drawBuffers[4] = {};
        int m_nextDrawBuffer = 0;
        float m_feedbackOrigin[2] = {};
        uint8_t m_span = 0;    // the span drawn now (beginSpan): 0 the world, 1 the HUD, 2 a backdrop
        bool m_fromVu = false; // primitives now come from a native VU1 program
        float m_q = 1.0f;
        uint32_t m_clut[256] = {};
        uint32_t m_cbp0 = ~0u, m_cbp1 = ~0u;
        uint64_t m_clutHash = 0;

        // vertex queue
        Vertex m_queue[3] = {};
        int m_queued = 0;
        int m_stripIndex = 0;

        // GIF packet in progress
        uint64_t m_tagLo = 0, m_tagHi = 0;
        uint32_t m_loops = 0, m_regIndex = 0, m_nreg = 0;
        uint32_t m_mode = 0; // 0 packed, 1 reglist, 2 image
        bool m_inPacket = false;
        std::vector<uint8_t> m_pending; // a quadword split across gif() calls

        std::unordered_map<uint64_t, bool> m_decoded;       // texture contents decoded since start
        std::unordered_map<uint64_t, uint64_t> m_contentOf; // a texture's place and page versions -> its content key
        std::unordered_map<uint64_t, uint64_t> m_contentOfBytes; // a texture's place and its pages' bytes -> its content key
        // 8-bit palettes as loaded from a place with given bytes: the 256 entries and their hash (the game loads its palettes
        // again every frame)
        struct LoadedClut
        {
            uint32_t entries[256];
            uint64_t hash;
        };
        std::unordered_map<uint64_t, LoadedClut> m_cluts;
    };
}
