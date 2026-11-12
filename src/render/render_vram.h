#pragma once

// The native renderer's copy of the GS's 4 MB of local memory (plan N3). The renderer applies the game's image transfers to
// it in the order the game sends them, so a texture is read as it was when a draw used it (font glyphs and palettes are
// uploaded into the same places many times a frame). Addressing comes from the runtime's GS memory tables, the ones the
// emulated GS uses.

#include <array>
#include <cstdint>
#include <vector>

namespace mm::render
{
    // TEX0 as the GS reads it.
    struct Tex0
    {
        uint32_t tbp = 0;  // base, in 64-word blocks
        uint32_t tbw = 0;  // buffer width, in 64-texel units
        uint32_t psm = 0;
        uint32_t tw = 0;   // log2 width
        uint32_t th = 0;   // log2 height
        uint32_t tcc = 0;  // 1: use the texture's alpha
        uint32_t tfx = 0;  // 0 modulate, 1 decal, 2 highlight, 3 highlight2
        uint32_t cbp = 0;  // palette base
        uint32_t cpsm = 0; // palette format
        uint32_t csm = 0;  // palette storage mode (0: CSM1)
        uint32_t csa = 0;  // palette entry offset (in 16s)
        uint32_t cld = 0;  // palette load control

        static Tex0 fromRegister(uint64_t value);
        bool indexed() const { return psm == 0x13 || psm == 0x14 || psm == 0x1B || psm == 0x24 || psm == 0x2C; }
    };

    class Vram
    {
    public:
        static constexpr uint32_t kBytes = 4u * 1024u * 1024u;
        static constexpr uint32_t kPages = kBytes / 8192u;

        Vram();

        // Host to local transfers: BITBLTBUF, TRXPOS and TRXREG as last written, then TRXDIR starts one (0 host to local,
        // 2 local to local, done at once). image() takes the transfer's data as it arrives, any number of bytes at a time.
        void setBitbltbuf(uint64_t value) { m_bitbltbuf = value; }
        void setTrxpos(uint64_t value) { m_trxpos = value; }
        void setTrxreg(uint64_t value) { m_trxreg = value; }
        void setTrxdir(uint64_t value);
        void image(const uint8_t *data, size_t bytes);
        bool transferring() const { return m_remaining > 0; }

        // Texels of a texture as RGBA8 (alpha as stored: 0x80 is 1.0), `width` x `height` from (0, 0); indexed formats
        // through `clut` (the GS's 256-entry palette buffer as last loaded; a 4-bit texture uses entries CSA * 16 on).
        // Returns false for a format this renderer does not read yet.
        bool decode(const Tex0 &tex, uint32_t width, uint32_t height, const uint32_t *clut, std::vector<uint32_t> &out) const;

        // Loads the palette buffer the way a TEX0 write with a load request does: a 32-bit palette in CSM1 from CBP, 256
        // entries for 8-bit textures (stored 16 x 16, entries 8-15 and 16-23 of every 32 swapped), 16 for 4-bit ones (8 x 2)
        // placed at CSA * 16.
        void loadClut(const Tex0 &tex, uint32_t *clut) const;

        // A number that changes whenever a page in [first, first + count) is written.
        uint64_t version(uint32_t firstPage, uint32_t pageCount) const;
        // A hash of the bytes of the pages in [first, first + count): the same bytes give the same hash, whatever was written
        // in between (the game uploads its glyphs and palettes again every frame). A page is hashed again only after a write.
        uint64_t contentHash(uint32_t firstPage, uint32_t pageCount) const;
        // The pages a texture (and its palette) lies in.
        static void texturePages(const Tex0 &tex, uint32_t width, uint32_t height, uint32_t &first, uint32_t &count);

        const uint8_t *data() const { return m_memory.data(); }

    private:
        void writePixel(uint32_t x, uint32_t y, uint32_t value);
        uint32_t readPixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const;
        void touch(uint32_t byteAddress);

        std::vector<uint8_t> m_memory;
        std::array<uint64_t, kPages> m_pageVersion{};
        mutable std::array<uint64_t, kPages> m_pageHash{};        // contentHash's per page
        mutable std::array<uint64_t, kPages> m_pageHashVersion{}; // the version m_pageHash was taken at (~0: none yet)
        uint64_t m_clock = 1;

        uint64_t m_bitbltbuf = 0, m_trxpos = 0, m_trxreg = 0;
        // the transfer in progress
        uint32_t m_dbp = 0, m_dbw = 0, m_dpsm = 0, m_x0 = 0, m_y0 = 0, m_width = 0, m_height = 0, m_x = 0, m_y = 0;
        uint64_t m_remaining = 0; // pixels still to come
        std::vector<uint8_t> m_carry; // bytes of a pixel group split across image() calls
    };
}
