// The movie player's buffers and small state accessors: the read ring (0x50000 bytes of the PSS stream, then its write
// position +0x50000, fill +0x50004 and size +0x50008), the video output ring (frames of 0x12C000 bytes tagged with a
// 0x39240-byte header each), the video input ring's timestamp list and the audio ring. The parts that talk to the
// hardware or wait on semaphores are not here (they belong to the threading step).

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

#include <bit>
#include <initializer_list>

namespace mm
{
    // ---- subtitles and display

    void Movie_SetSubtitleState(uint16_t state) { port::store<uint16_t>(0x001BDD80u, state); }

    // The display flags the vblank handler reads (0x1BDD68 on, 0x1BDD9C frame ready).
    void Movie_DisplayStop()
    {
        port::store<int32_t>(0x001BDD68u, 0);
        port::store<int32_t>(0x001BDD9Cu, 0);
    }

    // ---- video decoder state

    int Movie_VideoDecGetState(int videoDec) { return port::load<int32_t>(static_cast<uint32_t>(videoDec) + 0xA8u); }
    void Movie_VideoDecSetStateStart(int videoDec) { port::store<int32_t>(static_cast<uint32_t>(videoDec) + 0xA8u, 1); }

    // ---- the read ring

    void Movie_ReadBufDelete() {}

    void Movie_ReadBufCreate(int readBuf)
    {
        const uint32_t ring = static_cast<uint32_t>(readBuf);
        port::store<int32_t>(ring + 0x50004u, 0);
        port::store<int32_t>(ring + 0x50000u, 0);
        port::store<int32_t>(ring + 0x50008u, 0x50000);
    }

    // The bytes taken off the front (at most what is filled).
    int Movie_ReadBufEndGet(int readBuf, int size)
    {
        const uint32_t ring = static_cast<uint32_t>(readBuf);
        const int32_t filled = port::load<int32_t>(ring + 0x50004u);
        if (filled <= size)
            size = filled;
        port::store<int32_t>(ring + 0x50004u, static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(ring + 0x50004u)) - static_cast<uint32_t>(size)));
        return size;
    }

    // Where the next bytes go (*ptr, unless the ring is full) and how much room there is.
    int Movie_ReadBufBeginPut(int readBuf, GuestPtr<int32_t> ptr)
    {
        const uint32_t ring = static_cast<uint32_t>(readBuf);
        const int32_t room = static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(ring + 0x50008u)) - static_cast<uint32_t>(port::load<int32_t>(ring + 0x50004u)));
        if (room != 0)
            *ptr = static_cast<int32_t>(ring + static_cast<uint32_t>(port::load<int32_t>(ring + 0x50000u)));
        return room;
    }

    // Where the filled bytes start (*ptr, unless empty) and how many there are.
    int Movie_ReadBufBeginGet(int readBuf, GuestPtr<int32_t> ptr)
    {
        const uint32_t ring = static_cast<uint32_t>(readBuf);
        if (port::load<int32_t>(ring + 0x50004u) != 0)
        {
            const int32_t size = port::load<int32_t>(ring + 0x50008u);
            const int32_t start = static_cast<int32_t>(static_cast<uint32_t>(size) + (static_cast<uint32_t>(port::load<int32_t>(ring + 0x50000u)) - static_cast<uint32_t>(port::load<int32_t>(ring + 0x50004u))));
            *ptr = static_cast<int32_t>(ring + static_cast<uint32_t>(remainder(start, size)));
        }
        return port::load<int32_t>(ring + 0x50004u);
    }

    // The bytes put in (at most the room), advancing the write position round the ring.
    int Movie_ReadBufEndPut(int readBuf, int size)
    {
        const uint32_t ring = static_cast<uint32_t>(readBuf);
        const int32_t room = static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(ring + 0x50008u)) - static_cast<uint32_t>(port::load<int32_t>(ring + 0x50004u)));
        if (room <= size)
            size = room;
        const int32_t end = static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(ring + 0x50000u)) + static_cast<uint32_t>(size));
        port::store<int32_t>(ring + 0x50000u, remainder(end, port::load<int32_t>(ring + 0x50008u)));
        port::store<int32_t>(ring + 0x50004u, static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(ring + 0x50004u)) + static_cast<uint32_t>(size)));
        return size;
    }

    // ---- the video output ring (frames of 0x12C000 bytes, headers of 0x39240 bytes in the tag array)

    void Movie_VoBufDelete() {}

    void Movie_VoBufReset(int vobuf)
    {
        port::store<int32_t>(static_cast<uint32_t>(vobuf) + 0xCu, 0);
        port::store<int32_t>(static_cast<uint32_t>(vobuf) + 8u, 0);
    }

    int Movie_VoBufIsFull(int vobuf)
    {
        const uint32_t buf = static_cast<uint32_t>(vobuf);
        return port::load<int32_t>(buf + 0xCu) == port::load<int32_t>(buf + 0x10u) ? 1 : 0;
    }

    void Movie_VoBufDecCount(int vobuf)
    {
        const uint32_t buf = static_cast<uint32_t>(vobuf);
        if (port::load<int32_t>(buf + 0xCu) > 0)
            port::store<int32_t>(buf + 0xCu, port::load<int32_t>(buf + 0xCu) - 1);
    }

    // The ring over `data` (frames) and `tags` (headers), `size` frames, empty; every header's first word is cleared.
    void Movie_VoBufCreate(GuestPtr<uint32_t> vobuf, int data, int tags, int size)
    {
        vobuf[0] = static_cast<uint32_t>(data);
        vobuf[1] = static_cast<uint32_t>(tags);
        vobuf[4] = static_cast<uint32_t>(size);
        vobuf[3] = 0;
        vobuf[2] = 0;
        for (int i = 0; i < size; ++i)
            port::store<uint32_t>(vobuf[1] + 0x39240u * static_cast<uint32_t>(i), 0);
    }

    // The oldest decoded frame (0 when none).
    int Movie_VoBufGetData(GuestPtr<int32_t> vobuf)
    {
        int32_t frame = 0;
        if (vobuf[3] != vobuf[4])
            frame = static_cast<int32_t>(static_cast<uint32_t>(vobuf[0]) + static_cast<uint32_t>(vobuf[2]) * 0x12C000u);
        return frame;
    }

    // The header of the oldest decoded frame (0 when none).
    int Movie_VoBufGetTag(int vobuf)
    {
        const uint32_t buf = static_cast<uint32_t>(vobuf);
        int32_t tag = 0;
        if (port::load<int32_t>(buf + 0xCu) != 0)
        {
            const int32_t size = port::load<int32_t>(buf + 0x10u);
            const int32_t index = remainder(static_cast<int32_t>(static_cast<uint32_t>(size) + (static_cast<uint32_t>(port::load<int32_t>(buf + 8u)) - static_cast<uint32_t>(port::load<int32_t>(buf + 0xCu)))), size);
            tag = static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(buf + 4u)) + static_cast<uint32_t>(index) * 0x39240u);
        }
        return tag;
    }

    // ---- the audio ring

    // Whether the audio has caught up with what was read.
    int Movie_AudioDecIsPreset(int audio)
    {
        const uint32_t dec = static_cast<uint32_t>(audio);
        return port::load<int32_t>(dec + 0x54u) < port::load<int32_t>(dec + 0x48u) ? 0 : 1;
    }

    // Where PCM data can be put: mode 0 (still filling the header) the free part of the header and then the ring; else
    // the free space at the write position (wrapping into a second part at the ring's start when it runs past the end).
    void Movie_AudioDecBeginPut(GuestPtr<int32_t> audio, GuestPtr<int32_t> ptr0, GuestPtr<int32_t> len0, GuestPtr<int32_t> ptr1, GuestPtr<int32_t> len1)
    {
        if (audio[0] == 0)
        {
            *ptr0 = static_cast<int32_t>(audio.address + static_cast<uint32_t>(audio[0xB]) + 4u);
            *len0 = 0x28 - audio[0xB];
            *ptr1 = audio[0xC];
            *len1 = audio[0xF];
            return;
        }
        const int32_t write = audio[0xD];
        const int32_t room = audio[0xF] - audio[0xE];
        if (audio[0xF] - write < room)
        {
            *ptr0 = static_cast<int32_t>(static_cast<uint32_t>(audio[0xC]) + static_cast<uint32_t>(write));
            *len0 = audio[0xF] - audio[0xD];
            *ptr1 = audio[0xC];
            *len1 = room - (audio[0xF] - audio[0xD]);
        }
        else
        {
            *ptr0 = static_cast<int32_t>(static_cast<uint32_t>(audio[0xC]) + static_cast<uint32_t>(write));
            *len0 = room;
            *ptr1 = 0;
            *len1 = 0;
        }
    }

    // ---- the video input ring's timestamps: records of 0x18 bytes (two 64-bit stamps, a position at +0x10 and a length
    // at +0x14) in a ring of +0x54 records, +0x58 of them in use ending before +0x5C. A new stamp `ts` (position +0x10,
    // length +0x14) is matched against the oldest records: each one it overlaps is shortened from the front by what ts
    // covers, and dropped when empty, until one does not overlap or the lists run out.
    int Movie_ViBufModifyPts(int vibuf, int ts)
    {
        const uint32_t buf = static_cast<uint32_t>(vibuf), stamp = static_cast<uint32_t>(ts);
        const int32_t count = port::load<int32_t>(buf + 0x54u);
        int32_t index = remainder(static_cast<int32_t>(static_cast<uint32_t>(count) + (static_cast<uint32_t>(port::load<int32_t>(buf + 0x5Cu)) - static_cast<uint32_t>(port::load<int32_t>(buf + 0x58u)))), count);
        const int32_t span = static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(buf + 8u)) << 11);
        if (port::load<int32_t>(buf + 0x58u) <= 0)
            return 0;
        bool again = true;
        do
        {
            const uint32_t record = static_cast<uint32_t>(port::load<int32_t>(buf + 0x50u)) + 0x18u * static_cast<uint32_t>(index);
            int32_t left = port::load<int32_t>(record + 0x14u);
            if (left == 0)
                return 0;
            const int32_t length = port::load<int32_t>(stamp + 0x14u);
            if (length == 0)
                return 0;
            const int32_t start = port::load<int32_t>(record + 0x10u), at = port::load<int32_t>(stamp + 0x10u);
            if (remainder(static_cast<int32_t>(static_cast<uint32_t>(start) + static_cast<uint32_t>(span) - static_cast<uint32_t>(at)), span) < length)
            {
                const int32_t reach = static_cast<int32_t>(static_cast<uint32_t>(at) + static_cast<uint32_t>(length) - static_cast<uint32_t>(start));
                if (!(left <= reach))
                    left = reach;
                port::store<int32_t>(record + 0x10u, remainder(static_cast<int32_t>(static_cast<uint32_t>(start) + static_cast<uint32_t>(left)), span));
                port::store<int32_t>(record + 0x14u, static_cast<int32_t>(static_cast<uint32_t>(port::load<int32_t>(record + 0x14u)) - static_cast<uint32_t>(left)));
                if (port::load<int32_t>(record + 0x14u) == 0)
                {
                    if (port::load<int64_t>(record) >= 0)
                    {
                        port::store<int64_t>(record, -1);
                        port::store<int64_t>(record + 8u, -1);
                        port::store<int32_t>(record + 0x10u, 0);
                        port::store<int32_t>(record + 0x14u, 0);
                    }
                    int32_t used = port::load<int32_t>(buf + 0x58u) - 1;
                    if (used < 0)
                        used = 0;
                    port::store<int32_t>(buf + 0x58u, used);
                }
            }
            else
                again = false;
            index = remainder(index + 1, port::load<int32_t>(buf + 0x54u));
        } while (again);
        return 0; // every exit leaves v0 = 0
    }

    PORT_FN(Movie_SetSubtitleState);
    PORT_FN(Movie_DisplayStop);
    PORT_FN(Movie_VideoDecGetState);
    PORT_FN(Movie_VideoDecSetStateStart);
    PORT_FN(Movie_ReadBufDelete);
    PORT_FN(Movie_ReadBufCreate);
    PORT_FN(Movie_ReadBufEndGet);
    PORT_FN(Movie_ReadBufBeginPut);
    PORT_FN(Movie_ReadBufBeginGet);
    PORT_FN(Movie_ReadBufEndPut);
    PORT_FN(Movie_VoBufDelete);
    PORT_FN(Movie_VoBufReset);
    PORT_FN(Movie_VoBufIsFull);
    PORT_FN(Movie_VoBufDecCount);
    PORT_FN(Movie_VoBufCreate);
    PORT_FN(Movie_VoBufGetData);
    PORT_FN(Movie_VoBufGetTag);
    PORT_FN(Movie_AudioDecIsPreset);
    PORT_FN(Movie_AudioDecBeginPut);
    PORT_FN(Movie_ViBufModifyPts);

    // ---- the rest of the movie player's small functions (batch 41)

    namespace
    {
        // The IPU-to-memory DMA channel (channel 4) and the global DMA enable.
        constexpr uint32_t kIpuToChcr = 0x1000B400u, kIpuToMadr = 0x1000B410u, kIpuToQwc = 0x1000B420u, kIpuToTadr = 0x1000B430u;
        constexpr uint32_t kDmacEnabler = 0x1000F520u, kDmacEnablew = 0x1000F590u;

        // Stops the IPU DMA channel: the whole controller held (bit 16 of the enable word) around writing 5 to the channel.
        void stopIpuDma()
        {
            sdk::DIntr();
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) | 0x10000u);
            port::writeHardware32(kIpuToChcr, 5u);
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) & 0xFFFEFFFFu);
            sdk::EIntr();
        }
    }

    // The video-out DMA end interrupt: one decoded frame less in the ring (when one was being sent), and interrupts on.
    // Returns 0.
    int Movie_DmaEndHandler()
    {
        const GuestVar<int32_t> sending{0x001BDD74u};
        if (sending.get() != 0)
        {
            Movie_VoBufDecCount(0x00717DA0);
            sending.set(0);
        }
        port::g_call.ctx->cop0_status |= 0x10000;
        return 0;
    }

    // The decoder's input ring: stops the IPU DMA and frees the ring's semaphore (+0x40).
    int Movie_ViBufDelete(int vibuf)
    {
        stopIpuDma();
        port::writeHardware32(kIpuToQwc, 0u);
        port::writeHardware32(kIpuToMadr, 0u);
        port::writeHardware32(kIpuToTadr, 0u);
        sdk::DeleteSema(port::load<int32_t>(static_cast<uint32_t>(vibuf) + 0x40u));
        return 1;
    }

    // Empties the decoder's input ring (counts +0xC/+0x10/+0x14 and +0x58/+0x5C cleared, +0x44 set), marks every timestamp
    // record (+0x50, +0x54 of them, 0x18 bytes) unused, chains the ring's `+8` blocks of 0x800 bytes from +0 as tags (REF
    // tag 0x30000080 into the data, +4), ends the chain with a tag back to its start, and starts it on the IPU DMA channel.
    int Movie_ViBufReset(GuestPtr<int32_t> vibuf)
    {
        const uint32_t v = vibuf.address;
        port::store<int32_t>(v + 0xCu, 0);
        port::store<int32_t>(v + 0x10u, 0);
        port::store<int32_t>(v + 0x14u, 0);
        port::store<int32_t>(v + 0x44u, 1);
        port::store<int32_t>(v + 0x58u, 0);
        port::store<int32_t>(v + 0x5Cu, 0);
        uint32_t offset = 0;
        for (int i = 0; i < port::load<int32_t>(v + 0x54u); ++i)
        {
            const uint32_t record = port::load<uint32_t>(v + 0x50u) + offset;
            port::store<uint64_t>(record, ~0ull);
            port::store<uint64_t>(record + 8u, ~0ull);
            port::store<uint32_t>(record + 0x10u, 0u);
            port::store<uint32_t>(record + 0x14u, 0u);
            offset += 0x18u;
        }
        int blocks = 0;
        uint32_t data = 0, tag = 0;
        while (blocks < port::load<int32_t>(v + 8u))
        {
            const uint64_t address = (port::load<uint32_t>(v) + data) & 0x0FFFFFFFu;
            port::store<uint64_t>(port::load<uint32_t>(v + 4u) + tag, (address << 32) | 0x30000080ull);
            ++blocks;
            data += 0x800u;
            tag += 0x10u;
        }
        const uint32_t tags = port::load<uint32_t>(v + 4u);
        port::store<uint64_t>(tags + (static_cast<uint32_t>(blocks) << 4), (static_cast<uint64_t>(tags & 0x0FFFFFFFu) << 32) | 0x20000000ull);
        port::writeHardware32(kIpuToQwc, 0u);
        port::writeHardware32(kIpuToMadr, port::load<uint32_t>(v) & 0x0FFFFFFFu);
        port::writeHardware32(kIpuToTadr, port::load<uint32_t>(v + 4u) & 0x0FFFFFFFu);
        stopIpuDma();
        return 1;
    }

    // Sets up the decoder's input ring over `data` (the buffer), `tags` (its DMA chain), `size` blocks of 0x800 bytes and the
    // `tsCount` timestamp records at `ts`, creates its semaphore (+0x40, count and maximum 1) and resets it.
    int Movie_ViBufCreate(int vibuf, int data, int tags, int size, int ts, int tsCount)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t v = static_cast<uint32_t>(vibuf);
        port::store<int32_t>(v, data);
        port::store<uint32_t>(v + 4u, (static_cast<uint32_t>(tags) & 0x0FFFFFFFu) | 0x20000000u);
        port::store<int32_t>(v + 8u, size);
        port::store<int32_t>(v + 0x18u, size << 11);
        port::store<int32_t>(v + 0x50u, ts);
        port::store<int32_t>(v + 0x54u, tsCount);
        port::store<int32_t>(sp + 0x28u, 1);
        port::store<int32_t>(sp + 0x24u, 1);
        port::store<int32_t>(v + 0x40u, sdk::CreateSema(sp + 0x20u));
        Movie_ViBufReset(GuestPtr<int32_t>{v});
        port::store<uint64_t>(v + 0x48u, 0ull);
        return 1;
    }

    // Marks the frame being sent as done sending (header word 2), counts it (+0xC) and moves on (+8, around the ring of +0x10).
    void Movie_VoBufIncCount(int vobuf)
    {
        const uint32_t buf = static_cast<uint32_t>(vobuf);
        sdk::DIntr();
        port::store<int32_t>(port::load<uint32_t>(buf + 4u) + static_cast<uint32_t>(port::load<int32_t>(buf + 8u)) * 0x39240u, 2);
        port::store<int32_t>(buf + 0xCu, port::load<int32_t>(buf + 0xCu) + 1);
        port::store<int32_t>(buf + 8u, mm::remainder(port::load<int32_t>(buf + 8u) + 1, port::load<int32_t>(buf + 0x10u)));
        sdk::EIntr();
    }

    int Movie_VideoDecDelete(int videoDec)
    {
        Movie_ViBufDelete(videoDec + 0x48);
        sdk::sceMpegDelete(videoDec);
        return 1;
    }

    // Hands its own arguments on to the library (the original has none of its own; the fifth, the callback's data, is in t0).
    int Movie_VideoDecAddStrCallback(int a0, int a1, int a2, int a3, int a4)
    {
        sdk::sceMpegAddStrCallback(a0, a1, a2, a3, a4);
        return 1;
    }

    // Lays the subtitle text (a zero-ended string; a newline pads the line out with spaces to the next multiple of 52
    // columns) into the 3 x 52 buffer at 0x215F00 and gives each of its 156 cells the glyph of its character, the empty
    // glyph (0) past the end of the text (the glyph table at +0x50 of the movie state, 0x100 apart, glyph at +4).
    void Movie_LayoutSubtitle(GuestPtr<char> text)
    {
        constexpr uint32_t kLine = 0x00215F00u;
        port::store<uint8_t>(kLine, 0);
        int length = 0;
        for (; *text != 0; ++text)
        {
            if (*text != '\n')
            {
                port::store<uint8_t>(kLine + static_cast<uint32_t>(length), static_cast<uint8_t>(*text));
                ++length;
                continue;
            }
            const int next = (length / 0x34 + 1) * 0x34;
            while (length < next)
            {
                port::store<uint8_t>(kLine + static_cast<uint32_t>(length), 0x20);
                ++length;
            }
        }
        port::store<uint8_t>(kLine + static_cast<uint32_t>(length), 0);
        uint32_t read = kLine;
        uint32_t cell = port::load<uint32_t>(0x001BDD90u) + 0x50u;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 0x34; ++column)
            {
                const uint8_t c = port::load<uint8_t>(read);
                if (c != 0)
                    ++read;
                port::store<int32_t>(cell + 4u, Font_GetGlyph(c));
                cell += 0x100u;
            }
    }

    // Sends the decoded frame's two parts (an IOP-side ring of two pieces, an EE-side data of two pieces) to the IOP by SIF
    // DMA, one transfer at a time (each started after a cache flush and waited for). The EE side is first cut down to
    // what the IOP side holds; the pieces are then copied in order over the seam between the IOP's two parts.
    // Returns how many bytes of the EE data were sent.
    int Movie_SendToIop2Area(int iop0, int iopSize0, int iop1, int iopSize1, int ee0, int eeSize0, int ee1, int eeSize1)
    {
        port::GuestFrame<0x110> frame;
        const uint32_t sp = frame.ptr().address;
        // One transfer descriptor (data, destination, size, mode 0) in the frame's slot, as the original lays them out.
        const auto transfer = [&](uint32_t slot, int data, int destination, int size) {
            port::store<int32_t>(sp + slot, data);
            port::store<int32_t>(sp + slot + 4u, destination);
            port::store<int32_t>(sp + slot + 8u, size);
            port::store<int32_t>(sp + slot + 0xCu, 0);
            sdk::FlushCache(0);
            const uint32_t id = sdk::sceSifSetDma(sp + slot, 1);
            while (sdk::sceSifDmaStat(id) >= 0)
            {
            }
        };
        if (iopSize0 + iopSize1 < eeSize0 + eeSize1)
        {
            const int over = (eeSize0 + eeSize1) - (iopSize0 + iopSize1);
            if (over < eeSize1)
                eeSize1 -= over;
            else
            {
                eeSize0 -= over - eeSize1;
                eeSize1 = 0;
            }
        }
        const int rest = iopSize0 - eeSize0;
        if (eeSize0 >= iopSize0)
        {
            if (iopSize0 > 0)
                transfer(0x90, ee0, iop0, iopSize0);
            if (eeSize0 - iopSize0 > 0)
                transfer(0xA0, ee0 + iopSize0, iop1, eeSize0 - iopSize0);
            if (eeSize1 > 0)
                transfer(0xB0, ee1, (iop1 + eeSize0) - iopSize0, eeSize1);
        }
        else if (eeSize1 < rest)
        {
            if (eeSize0 > 0)
                transfer(0xF0, ee0, iop0, eeSize0);
            if (eeSize1 > 0)
                transfer(0x100, ee1, iop0 + eeSize0, eeSize1);
        }
        else
        {
            if (eeSize0 > 0)
                transfer(0xC0, ee0, iop0, eeSize0);
            if (rest > 0)
                transfer(0xD0, ee1, iop0 + eeSize0, rest);
            if (eeSize1 - rest > 0)
                transfer(0xE0, (ee1 + iopSize0) - eeSize0, iop1, eeSize1 - rest);
        }
        return eeSize0 + eeSize1;
    }

    namespace
    {
        // The movie frame's size in pixels as the player keeps it: the width (0x1BDDBC), the height (0x1BDDC0) and the
        // left edge (0x1BDDB4), all signed 16-bit.
        struct MovieFrameSize
        {
            int32_t width;
            int32_t height;
            int32_t left;
        };

        MovieFrameSize movieFrameSize()
        {
            return {port::load<int16_t>(0x001BDDBCu), port::load<int16_t>(0x001BDDC0u), port::load<int16_t>(0x001BDDB4u)};
        }

        // The GS texture setup the image packet needs from the frame: the buffer width in 64-pixel units, doubled
        // and per 32 lines (as halves of that), and the texture's base address block (the left edge in 64-pixel units). The
        // original's rounding of each division toward zero is the language's.
        struct MovieTexture
        {
            int32_t blocks;
            int64_t base;
        };

        MovieTexture movieTexture(const MovieFrameSize &frame)
        {
            const int32_t lines = ((frame.height / 2 + 0x1F) / 32) << 5;
            const int32_t columns = ((frame.width + 0x3F) / 64) << 6;
            const int32_t product = static_cast<int32_t>(static_cast<uint32_t>(columns) * static_cast<uint32_t>(lines));
            const int32_t blocks = static_cast<int32_t>(static_cast<uint32_t>(product) << 1) / 64;
            const int32_t unit = (frame.left + 0x3F) / 64;
            int64_t base = (static_cast<int64_t>(unit) << 38) >> 38;
            const int32_t scaled = static_cast<int32_t>(static_cast<uint32_t>(unit) << 6);
            if (scaled < 0)
                base = (scaled + 0x3F) >> 6;
            return {blocks, base};
        }
    }

    // Builds the GIF packet that sends one decoded movie frame (`image`, in 16 x 16 macroblocks of 1 KB) to the GS as a
    // texture upload, at `packet`: unless `half` is set, the transfer set-up (BITBLTBUF, TRXREG) and one REF to each macroblock
    // behind its own TRXPOS and TRXDIR; and in every case the closing packet that draws the texture as a sprite (TEX0, PRIM,
    // the vertex pair from the frame size). The two GIF tags are the constants at 0x1B3830 and 0x1B3840.
    void Movie_BuildImagePacket(uint32_t packet, uint32_t image, int half, int width, int height)
    {
        port::GuestFrame<0xE0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t pk = sp + 0xB0u, tagUpload = sp + 0xC0u, tagDraw = sp + 0xD0u;
        const auto quadAt = [](uint32_t at) { return port::Quad{port::load<uint64_t>(at), port::load<uint64_t>(at + 8u)}; };
        port::store<uint64_t>(tagUpload, port::load<uint64_t>(0x001B3830u));
        port::store<uint64_t>(tagUpload + 8u, port::load<uint64_t>(0x001B3838u));
        port::store<uint64_t>(tagDraw, port::load<uint64_t>(0x001B3840u));
        port::store<uint64_t>(tagDraw + 8u, port::load<uint64_t>(0x001B3848u));
        port::store<int32_t>(sp + 0xACu, height);
        const int columns = width >> 4, rows = height >> 4;
        sdk::sceGifPkInit(pk, (packet & 0x0FFFFFFFu) | 0x20000000u);
        sdk::sceGifPkReset(pk);
        const MovieFrameSize size = movieFrameSize();
        if (half == 0)
        {
            const MovieTexture texture = movieTexture(size);
            sdk::sceGifPkCnt(pk);
            sdk::sceGifPkOpenGifTag(pk, quadAt(tagUpload));
            sdk::sceGifPkAddGsAD(pk, 0x50, (static_cast<uint64_t>(static_cast<uint32_t>(texture.blocks)) << 32) |
                                               (static_cast<uint64_t>(static_cast<uint32_t>(texture.base)) << 48));
            sdk::sceGifPkAddGsAD(pk, 0x52, 0x1000000010ull);
            sdk::sceGifPkCloseGifTag(pk);
            uint32_t source = image;
            for (int column = 0; column < columns; ++column)
                for (int row = 0; row < rows; ++row)
                {
                    sdk::sceGifPkCnt(pk);
                    sdk::sceGifPkOpenGifTag(pk, quadAt(tagUpload));
                    sdk::sceGifPkAddGsAD(pk, 0x51, (static_cast<uint64_t>(static_cast<uint32_t>(column * 0x10)) << 32) |
                                                       (static_cast<uint64_t>(static_cast<uint32_t>(row * 0x10)) << 48));
                    sdk::sceGifPkAddGsAD(pk, 0x53, 0);
                    sdk::sceGifPkCloseGifTag(pk);
                    const uint32_t reserved = sdk::sceGifPkReserve(pk, 4);
                    port::store<uint64_t>(reserved, 0x0800000000000040ull);
                    port::store<uint64_t>(reserved + 8u, 0ull);
                    sdk::sceGifPkRef(pk, source & 0x0FFFFFFFu, 0x40);
                    source += 0x400u;
                }
        }
        const int32_t left = (0x800 - size.width / 2) * 0x10;
        const int32_t halfHeight = size.height / 2;
        const int32_t top = (0x800 - halfHeight / 2) * 0x10;
        sdk::sceGifPkEnd(pk);
        sdk::sceGifPkOpenGifTag(pk, quadAt(tagDraw));
        sdk::sceGifPkAddGsAD(pk, 0x3F, 0);
        sdk::sceGifPkAddGsAD(pk, 0x14, 0x60);
        const MovieTexture texture = movieTexture(size);
        sdk::sceGifPkAddGsAD(pk, 6, (static_cast<uint64_t>(texture.base) << 14) | static_cast<uint64_t>(static_cast<int64_t>(texture.blocks)) | 0xAA8000000ull);
        sdk::sceGifPkAddGsAD(pk, 0, 0x116);
        sdk::sceGifPkAddGsAD(pk, 3, 0x80008);
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left)) | (static_cast<uint64_t>(static_cast<int64_t>(top)) << 16));
        sdk::sceGifPkAddGsAD(pk, 3, static_cast<uint64_t>(static_cast<int64_t>(width * 16 + 8)) |
                                        (static_cast<uint64_t>(static_cast<int64_t>(height * 16 + 8)) << 16));
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left + size.width * 16)) |
                                        (static_cast<uint64_t>(static_cast<int64_t>(top + halfHeight * 16 + 1)) << 16));
        sdk::sceGifPkCloseGifTag(pk);
        sdk::sceGifPkTerminate(pk);
    }

    // Builds the subtitle's DMA packets once, at 0x20C1A0: a header (the GS set-up for the glyph sheet: a texture upload of
    // the font from 0x1B3860, then the colour and texture state), then a quad of 52 x 3 glyph cells of 0x100 bytes each (an
    // empty glyph; its texture and the two corner vertices, 0xC0 apart horizontally from 0x6CA0 and 0xB0 per row from
    // y 0x8440), and a closing packet. The cells' table is kept at 0x1BDD90 for mm_Movie_LayoutSubtitle, which fills the
    // glyphs. The cell's TEX0 word is what the original's field-by-field edits come to: 0x4004B005'55406581.
    void Movie_BuildSubtitlePacket()
    {
        constexpr uint32_t kHeader = 0x0020C1A0u;
        const auto words = [](uint32_t at, std::initializer_list<uint32_t> list) {
            for (const uint32_t word : list)
            {
                port::store<uint32_t>(at, word);
                at += 4;
            }
        };
        words(kHeader + 0x00u, {0x10000008u, 0u, 0x11000000u, 0x50000008u});
        words(kHeader + 0x10u, {0x6u, 0x10000000u, 0xEu, 0u});
        words(kHeader + 0x20u, {0x44u, 0x10000020u, 0x42u, 0u});
        words(kHeader + 0x30u, {0xFu, 0x10000000u, 0x47u, 0u});
        words(kHeader + 0x40u, {0u, 0u, 0x51u, 0u});
        words(kHeader + 0x50u, {0x8u, 0x2u, 0x52u, 0u});
        words(kHeader + 0x60u, {0u, 0x12580u, 0x50u, 0u});
        words(kHeader + 0x70u, {0u, 0u, 0x53u, 0u});
        words(kHeader + 0x80u, {0x8004u, 0x8000000u, 0u, 0u});
        words(kHeader + 0x90u, {0x30000004u, 0x001B3860u & 0x7FFFFFFFu, 0u, 0x50000004u});
        words(kHeader + 0xA0u, {0x10000002u, 0u, 0x11000000u, 0x50000002u});
        words(kHeader + 0xB0u, {0x8001u, 0x10000000u, 0xEu, 0u});
        words(kHeader + 0xC0u, {0xCu, 0x16u, 0x52u, 0u});
        words(kHeader + 0xD0u, {0x10000003u, 0u, 0x11000000u, 0x50000003u});
        words(kHeader + 0xE0u, {0x8002u, 0x10000000u, 0xEu, 0u});
        words(kHeader + 0xF0u, {0x4C00Au, 0x54u, 0x8u, 0u});
        words(kHeader + 0x100u, {0x80808080u, 0x3F800000u, 0x1u, 0u});
        uint32_t cell = kHeader + 0x110u;
        port::store<uint32_t>(0x001BDD90u, cell);
        const int32_t depth = sdk::fptosi(ps2f(std::bit_cast<float>(0x4B742400u)));
        int32_t top = 0x8440, bottom = 0x84F0;
        for (int row = 0; row < 3; ++row)
        {
            int32_t left = 0x6CA0;
            for (int column = 0; column < 0x34; ++column)
            {
                words(cell + 0x00u, {0x10000004u, 0u, 0x11000000u, 0x50000004u});
                words(cell + 0x10u, {2u, 0x10000000u, 0xEu, 0u});
                words(cell + 0x20u, {0u, 0x14012581u, 0x50u, 0u});
                words(cell + 0x30u, {0u, 0u, 0x53u, 0u});
                words(cell + 0x40u, {0x8009u, 0x08000000u, 0u, 0u});
                words(cell + 0x50u, {0x30000009u, 0u, 0u, 0x50000009u});
                port::store<int32_t>(cell + 0x54u, Font_GetGlyph(0));
                words(cell + 0x60u, {0x10000002u, 0u, 0x11000000u, 0x50000002u});
                words(cell + 0x70u, {0x8001u, 0x10000000u, 0xEu, 0u});
                words(cell + 0x80u, {0u, 0u, 0x3Fu, 0u});
                words(cell + 0x90u, {0x10000006u, 0u, 0x11000000u, 0x50000006u});
                words(cell + 0xA0u, {0x8001u, 0x502B4000u, 0x52526u, 0u});
                if (left < 0 || left + 0xC0 >= 0x10000)
                    left = 0;
                words(cell + 0xB0u, {0x55406581u, 0x4004B005u, 6u, 0u}); // TEX0
                words(cell + 0xC0u, {0u, 0u, 0u, 0u});
                words(cell + 0xD0u, {static_cast<uint32_t>(left), static_cast<uint32_t>(top), static_cast<uint32_t>(depth), 0xFFu});
                words(cell + 0xE0u, {0x3EC00000u, 0x3F300000u, 0u, 0u});
                words(cell + 0xF0u, {static_cast<uint32_t>(left + 0xC0), static_cast<uint32_t>(bottom), static_cast<uint32_t>(depth), 0xFFu});
                cell += 0x100u;
                left += 0xC0;
            }
            top += 0xB0;
            bottom += 0xB0;
        }
        words(cell + 0x00u, {0x10000002u, 0u, 0x11000000u, 0x50000002u});
        words(cell + 0x10u, {0x8001u, 0x10000000u, 0xEu, 0u});
        words(cell + 0x20u, {0u, 0u, 8u, 0u});
        words(cell + 0x30u, {0x60000000u, 0u, 0u, 0u});
        port::store<uint8_t>(0x00215F00u, 0);
    }

    PORT_FN(Movie_BuildImagePacket);
    PORT_FN(Movie_BuildSubtitlePacket);
    PORT_FN(Movie_DmaEndHandler);
    PORT_FN(Movie_ViBufDelete);
    PORT_FN(Movie_ViBufReset);
    PORT_FN(Movie_ViBufCreate);
    PORT_FN(Movie_VoBufIncCount);
    PORT_FN(Movie_VideoDecDelete);
    PORT_FN(Movie_VideoDecAddStrCallback);
    PORT_FN(Movie_LayoutSubtitle);
    PORT_FN(Movie_SendToIop2Area);

    // ---- the movie player's kernel, sound-driver and IPU calls (plan P6 batch 2)

    namespace
    {
        // Unsigned remainder with the EE's rule for a zero divisor (the dividend itself).
        uint32_t remainderUnsigned(uint32_t dividend, uint32_t divisor) { return divisor == 0 ? dividend : dividend % divisor; }

        // The IPU registers and the IPU-to-memory (from-IPU, channel 3) DMA channel.
        constexpr uint32_t kIpuCmd = 0x10002000u, kIpuCtrl = 0x10002010u, kIpuBp = 0x10002020u;
        constexpr uint32_t kIpuFromChcr = 0x1000B000u, kIpuFromMadr = 0x1000B010u, kIpuFromQwc = 0x1000B020u;

        // A field of an object at `base`, by guest address.
        uint32_t word(int object, uint32_t offset) { return port::load<uint32_t>(static_cast<uint32_t>(object) + offset); }
        void setWord(int object, uint32_t offset, uint32_t value) { port::store<uint32_t>(static_cast<uint32_t>(object) + offset, value); }
    }

    // Yields the processor to the other threads of the same priority.
    void Movie_SwitchThread() { sdk::RotateThreadReadyQueue(1); }

    void Movie_Error(int message) { sdk::printf(0x001BAC08u, message); }

    // The MPEG library's error callback: logs the code (+4 of its data), the decoder goes on.
    int Movie_CbError(int, int cbdata)
    {
        sdk::printf(0x001BAD48u, word(cbdata, 4));
        return 1;
    }

    // Frees the audio buffer and the control block that were allocated in IOP memory.
    int Movie_AudioDecDelete(int audio)
    {
        sdk::sceSifFreeSysMemory(word(audio, 0x44));
        sdk::sceSifFreeSysMemory(word(audio, 0x58));
        return 1;
    }

    // Waits until the field shown is not `field`, then shows the movie (display on, nothing ready, subtitle changes off).
    int Movie_DisplayStart(int field)
    {
        int64_t shown;
        do
            shown = sdk::sceGsSyncV(0);
        while (static_cast<int64_t>(field) == shown);
        port::store<int32_t>(0x001BDD68u, 1);
        port::store<int32_t>(0x001BDD9Cu, 0);
        port::store<int32_t>(0x001BDD6Cu, 0);
        return static_cast<int>(shown);
    }

    // Sound driver volume (both cores, the two voice mixer lanes of each).
    void Movie_SetSpuMasterVolume(int volume)
    {
        for (uint32_t core = 0; core < 2u; ++core)
        {
            sdk::sceSdRemote(1, 0x8010, core | 0x980u, volume);
            sdk::sceSdRemote(1, 0x8010, core | 0xA80u, volume);
        }
    }

    namespace
    {
        // Start and resume the same way: both cores' volume to full, then the streaming voice over the IOP buffer (+0x44), its size
        // rounded down to 1 KB, ending at the buffer plus the position kept in +0x50; the state becomes 2 (playing).
        void startAudioStream(int audio)
        {
            sdk::sceSdRemote(1, 0x8010, 0xF80, 0x7FFF);
            sdk::sceSdRemote(1, 0x8010, 0x1080, 0x7FFF);
            const uint32_t size = word(audio, 0x48);
            const int32_t rounded = (static_cast<int32_t>(size) >= 0 ? static_cast<int32_t>(size) : static_cast<int32_t>(size) + 0x3FF) >> 10;
            sdk::sceSdRemote(1, 0x80E0, 0, 0x13, word(audio, 0x44), static_cast<uint32_t>(rounded) << 10, word(audio, 0x44) + word(audio, 0x50));
            setWord(audio, 0, 2);
        }
    }

    void Movie_AudioDecStart(GuestPtr<uint32_t> audio) { startAudioStream(static_cast<int>(audio.address)); }
    void Movie_AudioDecResume(GuestPtr<uint32_t> audio) { startAudioStream(static_cast<int>(audio.address)); }

    // Stops the streaming voice, remembers where the driver had got to (24 bits, relative to the buffer; +0x50) and clears
    // the stream's counters (state 0).
    void Movie_AudioDecReset(GuestPtr<uint32_t> audio)
    {
        const int a = static_cast<int>(audio.address);
        setWord(a, 0, 3);
        sdk::sceSdRemote(1, 0x8010, 0xF80, 0);
        sdk::sceSdRemote(1, 0x8010, 0x1080, 0);
        const int64_t position = sdk::sceSdRemote(1, 0x80E0, 0, 2, 0, 0);
        setWord(a, 0x50, static_cast<uint32_t>(position & 0xFFFFFF) - word(a, 0x44));
        sdk::sceSdRemote(1, 0x80D0, 0, 0, word(a, 0x58), 0x4000, 0x800);
        setWord(a, 0, 0);
        setWord(a, 0x2C, 0);
        setWord(a, 0x34, 0);
        setWord(a, 0x38, 0);
        setWord(a, 0x40, 0);
        setWord(a, 0x54, 0);
        setWord(a, 0x4C, 0);
        setWord(a, 0x50, 0);
    }

    // Stream data arrives in a 40-byte header (state 0, filled up to +0x2C) and then audio: the header is logged once complete,
    // and the ring (+0x30..0x40: base, write position, fill, size, total) takes the rest.
    void Movie_AudioDecEndPut(GuestPtr<int32_t> audio, uint32_t size)
    {
        const int a = static_cast<int>(audio.address);
        if (word(a, 0) == 0)
        {
            uint32_t taken = 0x28u - word(a, 0x2C);
            if (!(taken < size))
                taken = size;
            setWord(a, 0x2C, word(a, 0x2C) + taken);
            if (!(word(a, 0x2C) < 0x28u))
            {
                setWord(a, 0, 1);
                sdk::printf(0x001BA960u);
                const auto byteAt = [&](uint32_t offset) { return static_cast<int32_t>(port::load<int8_t>(static_cast<uint32_t>(a) + offset)); };
                sdk::printf(0x001BA9A0u, byteAt(4), byteAt(5), byteAt(6), byteAt(7), word(a, 8), word(a, 0xC), word(a, 0x10), word(a, 0x14),
                            word(a, 0x18), word(a, 0x1C), word(a, 0x20));
                sdk::printf(0x001BAAE0u, byteAt(0x24), byteAt(0x25), byteAt(0x26), byteAt(0x27), word(a, 0x28));
            }
            size -= taken;
        }
        setWord(a, 0x34, static_cast<uint32_t>(remainder(static_cast<int32_t>(word(a, 0x34) + size), static_cast<int32_t>(word(a, 0x3C)))));
        setWord(a, 0x38, word(a, 0x38) + size);
        setWord(a, 0x40, word(a, 0x40) + size);
    }

    // Closes the stream file: the disc stream is stopped and its buffer freed, or the ordinary file closed.
    int Movie_StrFileClose(GuestPtr<int32_t> file)
    {
        const int f = static_cast<int>(file.address);
        if (word(f, 0) != 0)
        {
            sdk::sceCdStStop();
            sdk::sceSifFreeSysMemory(word(f, 0x2C));
        }
        else
            sdk::sceClose(static_cast<int>(word(f, 0x30)));
        return 1;
    }

    // Reads from the stream file: from the disc stream in sectors (the result in bytes, an error sets the flag at 0x1BDDCC) or
    // an ordinary read.
    int Movie_StrFileRead(GuestPtr<int32_t> file, int buffer, int size)
    {
        const int f = static_cast<int>(file.address);
        if (word(f, 0) == 0)
            return sdk::sceRead(static_cast<int>(word(f, 0x30)), static_cast<uint32_t>(buffer), size);
        port::GuestLocal<int32_t> error;
        const int sectors = sdk::sceCdStRead(size >> 11, static_cast<uint32_t>(buffer), 1, error.ptr().address);
        if (*error != 0)
            port::store<int32_t>(0x001BDDCCu, 1);
        return static_cast<int>(static_cast<uint32_t>(sectors) << 11);
    }

    // ---- the decoder's input ring, under its semaphore (+0x40)

    // Rounds the fill (+0x14) up to a whole block of 0x800 bytes.
    void Movie_ViBufFlush(int vibuf)
    {
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        const int32_t sum = static_cast<int32_t>(word(vibuf, 0x14)) + 0x7FF;
        const int32_t blocks = (sum >= 0 ? sum : sum + 0x7FF) >> 11;
        setWord(vibuf, 0x14, static_cast<uint32_t>(blocks) << 11);
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
    }

    // Bytes in the ring: the whole blocks (+0x10) and the rest (+0x14).
    int Movie_ViBufCount(int vibuf)
    {
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        const uint32_t count = word(vibuf, 0x14) + (word(vibuf, 0x10) << 11);
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
        return static_cast<int>(count);
    }

    // The bytes put in: the fill (+0x14) and the 64-bit total (+0x48) grow.
    void Movie_ViBufEndPut(int vibuf, int size)
    {
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        setWord(vibuf, 0x14, word(vibuf, 0x14) + static_cast<uint32_t>(size));
        port::store<uint64_t>(static_cast<uint32_t>(vibuf) + 0x48u, port::load<uint64_t>(static_cast<uint32_t>(vibuf) + 0x48u) + static_cast<uint64_t>(static_cast<int64_t>(size)));
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
    }

    // Where the next bytes go: up to two pieces (pointer and length each) round the ring, the second used only when the free
    // space wraps. The free space leaves two blocks unused.
    void Movie_ViBufBeginPut(GuestPtr<int32_t> vibuf, GuestPtr<int32_t> ptr0, GuestPtr<int32_t> len0, GuestPtr<int32_t> ptr1, GuestPtr<int32_t> len1)
    {
        const int v = static_cast<int>(vibuf.address);
        sdk::WaitSema(static_cast<int>(word(v, 0x40)));
        const int32_t used = static_cast<int32_t>(word(v, 0xC)) + static_cast<int32_t>(word(v, 0x10));
        const int32_t position = remainder(static_cast<int32_t>((static_cast<uint32_t>(used) << 11) + word(v, 0x14)), static_cast<int32_t>(word(v, 0x18)));
        const int32_t free = static_cast<int32_t>(((word(v, 8) - 2u - word(v, 0x10)) << 11) - word(v, 0x14));
        if (static_cast<int32_t>(word(v, 0x18)) - position < free)
        {
            *ptr0 = static_cast<int32_t>(word(v, 0) + static_cast<uint32_t>(position));
            *len0 = static_cast<int32_t>(word(v, 0x18)) - position;
            *ptr1 = static_cast<int32_t>(word(v, 0));
            *len1 = free - (static_cast<int32_t>(word(v, 0x18)) - position);
        }
        else
        {
            *ptr0 = static_cast<int32_t>(word(v, 0) + static_cast<uint32_t>(position));
            *len0 = free;
            *ptr1 = 0;
            *len1 = 0;
        }
        sdk::SignalSema(static_cast<int>(word(v, 0x40)));
    }

    // Puts a timestamp record (0x18 bytes) into the ring's list, unless it is full; 1 when there was room.
    int Movie_ViBufPutTs(int vibuf, int ts)
    {
        int result = 0;
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        if (static_cast<int32_t>(word(vibuf, 0x58)) < static_cast<int32_t>(word(vibuf, 0x54)))
        {
            Movie_ViBufModifyPts(vibuf, ts);
            const uint64_t first = port::load<uint64_t>(static_cast<uint32_t>(ts));
            result = 1;
            if (static_cast<int64_t>(first) >= 0 || static_cast<int64_t>(port::load<uint64_t>(static_cast<uint32_t>(ts) + 8u)) >= 0)
            {
                const auto entry = [&] { return word(vibuf, 0x50) + word(vibuf, 0x5C) * 0x18u; };
                port::store<uint64_t>(entry(), first);
                port::store<uint64_t>(entry() + 8u, port::load<uint64_t>(static_cast<uint32_t>(ts) + 8u));
                port::store<uint32_t>(entry() + 0x10u, port::load<uint32_t>(static_cast<uint32_t>(ts) + 0x10u));
                port::store<uint32_t>(entry() + 0x14u, port::load<uint32_t>(static_cast<uint32_t>(ts) + 0x14u));
                setWord(vibuf, 0x58, word(vibuf, 0x58) + 1u);
                setWord(vibuf, 0x5C, static_cast<uint32_t>(remainder(static_cast<int32_t>(word(vibuf, 0x5C)) + 1, static_cast<int32_t>(word(vibuf, 0x54)))));
            }
        }
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
        return result;
    }

    // Takes the oldest timestamp record whose data the IPU has now reached (the position in the ring, from the DMA address
    // and the bytes the IPU holds); ts is all ones when there is none. Always 1.
    int Movie_ViBufGetTs(GuestPtr<int32_t> vibufPtr, GuestPtr<uint64_t> ts)
    {
        const int vibuf = static_cast<int>(vibufPtr.address);
        const uint32_t madr = port::readHardware32(kIpuToMadr);
        const uint32_t bp = port::readHardware32(kIpuBp);
        bool found = false;
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        const uint32_t saved = word(vibuf, 0x38);
        const int32_t size = static_cast<int32_t>(word(vibuf, 8) << 11);
        *ts = ~0ull;
        ts[1] = ~0ull;
        const uint32_t held = ((bp >> 16) & 3u) + ((bp >> 8) & 0xFu);
        const int32_t where = static_cast<int32_t>(static_cast<uint32_t>(size) + (madr - (held << 4) + ((saved & 0x7Fu) >> 3)));
        const int32_t count = static_cast<int32_t>(word(vibuf, 0x58));
        const int32_t next = static_cast<int32_t>(word(vibuf, 0x5C));
        const int32_t position = static_cast<int32_t>(remainderUnsigned(static_cast<uint32_t>(where) - word(vibuf, 0), static_cast<uint32_t>(size)) + static_cast<uint32_t>(size));
        for (int32_t i = 0; i < count && !found; ++i)
        {
            const int32_t capacity = static_cast<int32_t>(word(vibuf, 0x54));
            const uint32_t entry = word(vibuf, 0x50) + 0x18u * static_cast<uint32_t>(remainder(capacity + (next - count) + i, capacity));
            const int32_t start = static_cast<int32_t>(port::load<uint32_t>(entry + 0x10u));
            const int32_t length = static_cast<int32_t>(port::load<uint32_t>(entry + 0x14u));
            if (remainder(position - start, size) < length)
            {
                ts[0] = port::load<uint64_t>(entry);
                ts[1] = port::load<uint64_t>(entry + 8u);
                port::store<uint64_t>(entry, ~0ull);
                port::store<uint64_t>(entry + 8u, ~0ull);
                found = true;
                const int32_t live = static_cast<int32_t>(word(vibuf, 0x58));
                setWord(vibuf, 0x58, static_cast<uint32_t>(live - (live > 0 ? 1 : live)));
            }
        }
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
        return 1;
    }

    // Stops both IPU DMA channels, keeping what they had reached (+0x1C.. for the input channel, +0x2C.. for the output channel
    // and the IPU's own registers) so the transfer can be restarted.
    int Movie_ViBufStopDma(int vibuf)
    {
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        setWord(vibuf, 0x44, 0);
        stopIpuDma();
        setWord(vibuf, 0x1C, port::readHardware32(kIpuToMadr));
        setWord(vibuf, 0x20, port::readHardware32(kIpuToTadr));
        setWord(vibuf, 0x24, port::readHardware32(kIpuToQwc));
        setWord(vibuf, 0x28, port::readHardware32(kIpuToChcr));
        while ((port::readHardware32(kIpuCtrl) & 0xF0u) != 0)
        {
        }
        sdk::DIntr();
        port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) | 0x10000u);
        port::writeHardware32(kIpuFromChcr, 0u);
        port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) & 0xFFFEFFFFu);
        sdk::EIntr();
        setWord(vibuf, 0x2C, port::readHardware32(kIpuFromMadr));
        setWord(vibuf, 0x30, port::readHardware32(kIpuFromQwc));
        setWord(vibuf, 0x34, port::readHardware32(kIpuFromChcr));
        setWord(vibuf, 0x38, port::readHardware32(kIpuBp));
        setWord(vibuf, 0x3C, port::readHardware32(kIpuCtrl));
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
        return 1;
    }

    // The MPEG decoder over its work area, with the five callbacks the game handles (error, and the others at 0x1A01B0..0x1A0210
    // and 0x1A0160), and the input ring at +0x48.
    int Movie_VideoDecCreate(int videoDec, int mpegWork, int mpegWorkSize, int data, int tags, int tagSize, int ts, int tsCount)
    {
        sdk::sceMpegCreate(videoDec, mpegWork, mpegWorkSize);
        sdk::sceMpegAddCallback(videoDec, 0, 0x001A0240u, 0);
        sdk::sceMpegAddCallback(videoDec, 1, 0x001A0210u, 0);
        sdk::sceMpegAddCallback(videoDec, 2, 0x001A01E0u, 0);
        sdk::sceMpegAddCallback(videoDec, 3, 0x001A01B0u, 0);
        sdk::sceMpegAddCallback(videoDec, 5, 0x001A0160u, 0);
        setWord(videoDec, 0xA8, 0);
        Movie_ViBufCreate(videoDec + 0x48, data, tags, tagSize, ts, tsCount);
        return 1;
    }

    PORT_FN(Movie_SwitchThread);
    PORT_FN(Movie_Error);
    PORT_FN(Movie_CbError);
    PORT_FN(Movie_AudioDecDelete);
    PORT_FN(Movie_DisplayStart);
    PORT_FN(Movie_SetSpuMasterVolume);
    PORT_FN(Movie_AudioDecStart);
    PORT_FN(Movie_AudioDecResume);
    PORT_FN(Movie_AudioDecReset);
    PORT_FN(Movie_AudioDecEndPut);
    PORT_FN(Movie_StrFileClose);
    PORT_FN(Movie_StrFileRead);
    PORT_FN(Movie_ViBufFlush);
    PORT_FN(Movie_ViBufCount);
    PORT_FN(Movie_ViBufEndPut);
    PORT_FN(Movie_ViBufBeginPut);
    PORT_FN(Movie_ViBufPutTs);
    PORT_FN(Movie_ViBufGetTs);
    PORT_FN(Movie_ViBufStopDma);
    PORT_FN(Movie_VideoDecCreate);

    // ---- the movie player's audio set-up, frame send and DMA restart (plan P6 batch 2)

    // Sets up the audio stream state (all counters zero, the PCM ring at `ring` of `ringSize` bytes), takes `iopSize` bytes of
    // IOP memory for the stream and 0x800 bytes of IOP heap for a silence block it copies there, and sets both cores' volume
    // to a quarter. 0 when an allocation fails.
    int Movie_AudioDecCreate(GuestPtr<uint32_t> audio, int ring, int ringSize, int iopSize)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t sp = frame.ptr().address;
        const int a = static_cast<int>(audio.address);
        setWord(a, 0, 0);
        setWord(a, 0x2C, 0);
        setWord(a, 0x30, static_cast<uint32_t>(ring));
        setWord(a, 0x34, 0);
        setWord(a, 0x38, 0);
        setWord(a, 0x3C, static_cast<uint32_t>(ringSize));
        setWord(a, 0x40, 0);
        setWord(a, 0x54, 0);
        setWord(a, 0x48, static_cast<uint32_t>(iopSize));
        setWord(a, 0x4C, 0);
        setWord(a, 0x50, 0);
        setWord(a, 0x44, sdk::sceSifAllocSysMemory(0, iopSize));
        if (word(a, 0x44) == 0)
        {
            sdk::printf(0x001BAB20u);
            return 0;
        }
        sdk::printf(0x001BAB40u, word(a, 0x44), iopSize);
        setWord(a, 0x58, static_cast<uint32_t>(sdk::sceSifAllocIopHeap(0x800)));
        if (static_cast<int32_t>(word(a, 0x58)) < 0)
        {
            sdk::printf(0x001BAB20u);
            return 0;
        }
        sdk::printf(0x001BAB40u, word(a, 0x58), 0x800);
        sdk::memset(0x0020B900u, 0, 0x800);
        port::store<uint32_t>(sp + 0x30u, 0x0020B900u);
        port::store<uint32_t>(sp + 0x34u, word(a, 0x58));
        port::store<uint32_t>(sp + 0x38u, 0x800u);
        port::store<uint32_t>(sp + 0x3Cu, 0u);
        sdk::FlushCache(0);
        const uint32_t id = sdk::sceSifSetDma(sp + 0x30u, 1);
        while (sdk::sceSifDmaStat(id) >= 0)
        {
        }
        for (uint32_t core = 0; core < 2u; ++core)
        {
            sdk::sceSdRemote(1, 0x8010, core | 0x980u, 0x3FFF);
            sdk::sceSdRemote(1, 0x8010, core | 0xA80u, 0x3FFF);
        }
        return 1;
    }

    // Sends what the IOP buffer has room for from the PCM ring. In state 1 the IOP buffer is filling for the first time
    // (from +0x54, the bytes sent); in state 2 the sound driver's read position decides how much of it is free (to 1 KB);
    // in state 0 and 3 nothing is sent. The EE side is the ring's valid part (+0x34 write position, +0x38 fill), cut to 1 KB
    // units and wrapped at the ring's end into two pieces. Returns how many bytes went; the counters move on by that.
    int Movie_AudioDecSendToIop(GuestPtr<int32_t> audioPtr, int a1, int a2, int a3)
    {
        const int audio = static_cast<int>(audioPtr.address);
        const uint32_t state = word(audio, 0);
        int iop0;
        if (state == 3 || state == 0)
            return 0;
        if (state == 2)
        {
            const int64_t position = sdk::sceSdRemote(1, 0x8100, 0);
            a2 = static_cast<int>(word(audio, 0x44));
            const int32_t size = static_cast<int32_t>(word(audio, 0x48));
            const int32_t filled = static_cast<int32_t>(word(audio, 0x4C));
            const int32_t free = remainder(static_cast<int32_t>(static_cast<uint32_t>(position & 0xFFFFFF) - static_cast<uint32_t>(a2) +
                                                                static_cast<uint32_t>(size) - static_cast<uint32_t>(filled) - 0x400u), size);
            const int32_t units = (free >= 0 ? free : free + 0x3FF) >> 10;
            const int32_t room = units * 0x400;
            iop0 = a2 + filled;
            a1 = size - filled;
            if (a1 < room)
                a3 = room - a1;
            else
            {
                a1 = room;
                a2 = 0;
                a3 = 0;
            }
        }
        else if (state == 1)
        {
            const int32_t sent = static_cast<int32_t>(word(audio, 0x54));
            const int32_t size = static_cast<int32_t>(word(audio, 0x48));
            a2 = 0;
            a3 = 0;
            a1 = size - sent;
            iop0 = static_cast<int>(word(audio, 0x44)) + remainder(sent, size);
        }
        else
            iop0 = audio;
        const int32_t ringSize = static_cast<int32_t>(word(audio, 0x3C));
        const int32_t fill = static_cast<int32_t>(word(audio, 0x38));
        const int32_t ring = static_cast<int32_t>(word(audio, 0x30));
        const int32_t ee0 = ring + remainder(ringSize + (static_cast<int32_t>(word(audio, 0x34)) - fill), ringSize);
        const int32_t rounded = ((fill >= 0 ? fill : fill + 0x3FF) >> 10) * 0x400;
        int32_t first = (ring + ringSize) - ee0;
        if (rounded < first)
            first = rounded;
        const int32_t second = rounded - first;
        int sent = 0;
        if (!(a1 + a3 < 0x400) && !(first + second < 0x400))
            sent = Movie_SendToIop2Area(iop0, a1, a2, a3, ee0, first, ring, second);
        setWord(audio, 0x38, word(audio, 0x38) - static_cast<uint32_t>(sent));
        setWord(audio, 0x54, word(audio, 0x54) + static_cast<uint32_t>(sent));
        setWord(audio, 0x4C, static_cast<uint32_t>(remainder(static_cast<int32_t>(word(audio, 0x4C)) + sent, static_cast<int32_t>(word(audio, 0x48)))));
        return sent;
    }

    // Draws the next slice of the movie frame: builds the packet (at 0x20C100) that sets up the GS for the picture, with the
    // current step (+0x1BDD7C, a halfword, in 1/16 lines) as the sprite's row, advances the step by the speed (+0x1BDD80) within
    // 0..0x7F0 and sends the packet. -1 when the speed is 0 or the picture is finished (speed 0x4000: the step is set to
    // the end); else the step.
    int16_t Movie_SendFrame(int picture)
    {
        port::GuestFrame<0x90> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t pk = sp + 0x70u, tag = sp + 0x80u;
        port::store<uint64_t>(tag, port::load<uint64_t>(0x001B3850u));
        port::store<uint64_t>(tag + 8u, port::load<uint64_t>(0x001B3858u));
        constexpr uint32_t kStep = 0x001BDD7Cu, kSpeed = 0x001BDD80u;
        const int16_t speed = port::load<int16_t>(kSpeed);
        if (speed == 0)
            return -1;
        if (speed == 0x4000)
        {
            port::store<int16_t>(kStep, 0x7F0);
            return -1;
        }
        sdk::sceGifPkInit(pk, (0x0020C100u & 0x0FFFFFFFu) | 0x20000000u);
        sdk::sceGifPkReset(pk);
        sdk::sceGifPkEnd(pk);
        const int32_t width = port::load<int16_t>(0x001BDDBCu);
        const int32_t left = (0x800 - width / 2) * 16;
        const int32_t halfHeight = port::load<int16_t>(0x001BDDC0u) / 2;
        const int32_t top = (0x800 - halfHeight / 2) * 16;
        const int32_t lines = (port::load<int16_t>(kStep) >> 4) & 0xFF;
        sdk::sceGifPkOpenGifTag(pk, port::Quad{port::load<uint64_t>(tag), port::load<uint64_t>(tag + 8u)});
        sdk::sceGifPkAddGsAD(pk, 0x42, 0x2000000044ull);
        sdk::sceGifPkAddGsAD(pk, 0, 0x46);
        const uint64_t colour = port::load<uint8_t>(0x001BDD84u) | (static_cast<uint64_t>(port::load<uint8_t>(0x001BDD85u)) << 8) |
                                (static_cast<uint64_t>(port::load<uint8_t>(0x001BDD86u)) << 16);
        sdk::sceGifPkAddGsAD(pk, 1, (static_cast<uint64_t>(lines & 0xFF) << 24) | colour | 0x100000000ull);
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left)) | (static_cast<uint64_t>(static_cast<int64_t>(top)) << 16));
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left + width * 16)) |
                                        (static_cast<uint64_t>(static_cast<int64_t>(top + halfHeight * 16)) << 16));
        sdk::sceGifPkCloseGifTag(pk);
        sdk::sceGifPkTerminate(pk);
        const int32_t next = static_cast<int32_t>(port::load<int16_t>(kStep)) + static_cast<int32_t>(port::load<int16_t>(kSpeed));
        port::store<int16_t>(kStep, static_cast<int16_t>(next));
        if (!(next < 0x7F1))
            port::store<int16_t>(kStep, 0x7F0);
        if (port::load<int16_t>(kStep) < 0)
            port::store<int16_t>(kStep, 0);
        if (port::load<int16_t>(kStep) != -1)
            sdk::sceDmaSend(picture, 0x0020C100u);
        return port::load<int16_t>(kStep);
    }

    // Starts the video decoder's input again from where StopDma left it: the input channel is pointed at the part of the
    // ring the IPU has not yet taken (the data address, the tag chain from the block it is in, the quadword count), the output
    // channel is resumed where it stood, and the IPU is told to skip the bits it already had. Updates the ring's read
    // position (+0xC) and block count (+0x10) when the IPU has moved on. Always 1.
    int Movie_ViBufRestartDma(GuestPtr<uint32_t> vibufPtr)
    {
        const int v = static_cast<int>(vibufPtr.address);
        sdk::WaitSema(static_cast<int>(word(v, 0x40)));
        const uint32_t bp = word(v, 0x38), chcr = word(v, 0x28), madr = word(v, 0x1C), base = word(v, 0);
        const uint32_t held = ((bp >> 16) & 3u) + ((bp >> 8) & 0xFu);
        uint32_t qwc = word(v, 0x24) + held;
        uint32_t address = madr - held * 16u;
        uint32_t tagAddress = word(v, 0x20);
        uint32_t control = chcr | 0x100u;
        const uint32_t skipBits = bp & 0x7Fu;
        const uint32_t blocks = word(v, 8);
        const auto noteProgress = [&](int32_t index) {
            const int32_t count = static_cast<int32_t>(blocks);
            const int32_t behind = remainder(index, count);
            return behind < 0 || static_cast<int32_t>(word(v, 0x10)) <= behind;
        };
        if (address < base)
        {
            qwc = (base - address) >> 4;
            tagAddress = word(v, 4) & 0x0FFFFFFFu;
            address += blocks << 11;
            const uint32_t mode = madr == base ? 0u : (madr == base + (blocks << 11) ? 0u : 3u);
            control = (chcr & 0x0FFFFFFFu) | (mode << 28) | 0x100u;
            if (noteProgress(static_cast<int32_t>(blocks - word(v, 0xC))))
            {
                setWord(v, 0xC, blocks - 1u);
                setWord(v, 0x10, word(v, 0x10) + 1u);
            }
        }
        else
        {
            const uint32_t tags = word(v, 4);
            const uint32_t end = (tags + (blocks + 1u) * 16u) & 0x0FFFFFFFu;
            const uint32_t madrBlock = madr == end ? 0u : (madr - base) >> 11;
            const uint32_t addressBlock = address == end ? 0u : (address - base) >> 11;
            if (madrBlock != addressBlock)
            {
                qwc = (base + madrBlock * 0x800u - address) >> 4;
                tagAddress = (tags + madrBlock * 16u) & 0x0FFFFFFFu;
                const uint32_t position = base + remainderUnsigned(madr - base, blocks << 11);
                const uint32_t expected = base + static_cast<uint32_t>(remainder(static_cast<int32_t>(word(v, 0xC) + word(v, 0x10)), static_cast<int32_t>(blocks))) * 0x800u;
                const uint32_t mode = position == expected ? 0u : 3u;
                control = (chcr & 0x0FFFFFFFu) | (mode << 28) | 0x100u;
                if (noteProgress(static_cast<int32_t>(addressBlock + blocks - word(v, 0xC))))
                {
                    setWord(v, 0xC, addressBlock);
                    setWord(v, 0x10, word(v, 0x10) + 1u);
                }
            }
        }
        if (word(v, 0x2C) != 0 && word(v, 0x30) != 0)
        {
            port::writeHardware32(kIpuFromMadr, word(v, 0x2C));
            port::writeHardware32(kIpuFromQwc, word(v, 0x30));
            const uint32_t fromControl = word(v, 0x34) | 0x100u;
            sdk::DIntr();
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) | 0x10000u);
            port::writeHardware32(kIpuFromChcr, fromControl);
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) & 0xFFFEFFFFu);
            sdk::EIntr();
        }
        if (word(v, 0x10) != 0)
        {
            while (static_cast<int32_t>(port::readHardware32(kIpuCtrl)) < 0)
            {
            }
            port::writeHardware32(kIpuCmd, skipBits);
            while (static_cast<int32_t>(port::readHardware32(kIpuCtrl)) < 0)
            {
            }
        }
        port::writeHardware32(kIpuToMadr, address);
        port::writeHardware32(kIpuToTadr, tagAddress);
        port::writeHardware32(kIpuToQwc, qwc);
        if (word(v, 0x10) != 0)
        {
            sdk::DIntr();
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) | 0x10000u);
            port::writeHardware32(kIpuToChcr, control);
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) & 0xFFFEFFFFu);
            sdk::EIntr();
        }
        port::writeHardware32(kIpuCtrl, word(v, 0x3C));
        setWord(v, 0x44, 1);
        sdk::SignalSema(static_cast<int>(word(v, 0x40)));
        return 1;
    }

    PORT_FN(Movie_AudioDecCreate);
    PORT_FN(Movie_AudioDecSendToIop);
    PORT_FN(Movie_SendFrame);
    PORT_FN(Movie_ViBufRestartDma);

    // Opens the stream file named `name` ("device:path"): a name without a device is a host file ("host0:" style prefix from
    // 0x1BAC60), the disc device ("cdrom0" at 0x1BAC38) is streamed from the disc (slashes turned to backslashes, upper case,
    // and ";1" added when there is no version), anything else is an ordinary file. file[0] says which (1 = disc stream);
    // an ordinary file keeps its descriptor (+0x30) and size (+4); a disc stream its IOP buffer (+0x2C), the file record (+8)
    // and size (+4). 1 when opened, 0 on an error (logged).
    int Movie_StrFileOpen(GuestPtr<int32_t> filePtr, int name)
    {
        port::GuestFrame<0x1C0> frame;
        const uint32_t sp = frame.ptr().address;
        const int file = static_cast<int>(filePtr.address);
        const uint32_t path = sp + 0x70u, device = sp + 0x170u, mode = sp + 0x1BCu;
        const uint32_t colon = sdk::index(static_cast<uint32_t>(name), ':');
        if (colon == 0)
        {
            sdk::strcpy(device, 0x001BAC60u);
            setWord(file, 0, 0);
            sdk::sprintf(path, 0x001BAC58u, device, static_cast<uint32_t>(name));
        }
        else
        {
            const int length = static_cast<int>(colon - static_cast<uint32_t>(name));
            sdk::strncpy(device, static_cast<uint32_t>(name), length);
            port::store<uint8_t>(device + static_cast<uint32_t>(length), 0);
            if (sdk::strcmp(device, 0x001BAC38u) == 0)
            {
                const int count = sdk::strlen(colon + 1u);
                setWord(file, 0, 1);
                for (int i = 0; i < count; ++i)
                {
                    const uint32_t at = colon + 1u + static_cast<uint32_t>(i);
                    if (port::load<int8_t>(at) == '/')
                        port::store<uint8_t>(at, 0x5C);
                    port::store<uint8_t>(at, static_cast<uint8_t>(sdk::toupper(port::load<int8_t>(at))));
                }
                const uint32_t suffix = sdk::index(static_cast<uint32_t>(name), ';') != 0 ? 0x001BAC40u : 0x001BAC48u;
                sdk::sprintf(path, 0x001BAC50u, colon + 1u, suffix);
            }
            else
            {
                setWord(file, 0, 0);
                sdk::sprintf(path, 0x001BAC58u, device, colon + 1u);
            }
        }
        sdk::printf(0x001BAC68u, path);
        if (word(file, 0) == 0)
        {
            setWord(file, 0x30, static_cast<uint32_t>(sdk::sceOpen(path, 1)));
            if (static_cast<int32_t>(word(file, 0x30)) < 0)
            {
                sdk::printf(0x001BACB0u, path);
                return 0;
            }
            setWord(file, 4, static_cast<uint32_t>(sdk::sceLseek(static_cast<int>(word(file, 0x30)), 0, 2)));
            if (static_cast<int32_t>(word(file, 4)) < 0)
            {
                sdk::printf(0x001BACD0u, path);
                sdk::sceClose(static_cast<int>(word(file, 0x30)));
                return 0;
            }
            if (sdk::sceLseek(static_cast<int>(word(file, 0x30)), 0, 0) < 0)
            {
                sdk::printf(0x001BACF0u, path);
                sdk::sceClose(static_cast<int>(word(file, 0x30)));
                return 0;
            }
        }
        else
        {
            constexpr uint32_t kDiscReady = 0x001BDDD8u;
            if (port::load<int32_t>(kDiscReady) == 0)
            {
                sdk::sceCdInit(0);
                sdk::sceCdMmode(1);
                sdk::sceCdDiskReady(0);
                port::store<int32_t>(kDiscReady, 1);
            }
            setWord(file, 0x2C, sdk::sceSifAllocSysMemory(0, 0x28010));
            sdk::sceCdStInit(0x50, 5, (word(file, 0x2C) + 0xFu) & 0xFFFFFFF0u);
            if (sdk::sceCdSearchFile(static_cast<uint32_t>(file) + 8u, path) == 0)
            {
                sdk::printf(0x001BAC80u, path);
                return 0;
            }
            setWord(file, 4, word(file, 0xC));
            port::store<uint8_t>(mode, 0);
            port::store<uint8_t>(mode + 1u, 0);
            port::store<uint8_t>(mode + 2u, 0);
            sdk::sceCdStStart(word(file, 8), mode);
        }
        sdk::printf(0x001BAD10u);
        return 1;
    }

    PORT_FN(Movie_StrFileOpen);

    // ---- The decoder object's wrappers and the MPEG library's callbacks over the input ring (plan P6 batch 2) ----

    namespace
    {
        // The ring the MPEG callbacks serve (the movie player has one decoder at a time).
        constexpr uint32_t kMovieRing = 0x00717CC8u;
        constexpr uint32_t kMovieAudio = 0x00717D40u;
    }

    // The decoder's ring starts at +0x48.
    void Movie_VideoDecEndPut(int videoDec, int size) { Movie_ViBufEndPut(videoDec + 0x48, size); }

    void Movie_VideoDecBeginPut(int videoDec, GuestPtr<int32_t> ptr0, GuestPtr<int32_t> len0, GuestPtr<int32_t> ptr1, GuestPtr<int32_t> len1)
    {
        Movie_ViBufBeginPut(GuestPtr<int32_t>{static_cast<uint32_t>(videoDec) + 0x48u}, ptr0, len0, ptr1, len1);
    }

    // Records a timestamp pair (64 bits each) for the data starting at `start` (an address, kept relative to the ring's buffer)
    // and `length` bytes long; 1 when the list had room.
    int Movie_VideoDecPutTs(int videoDec, int64_t pts, int64_t dts, int start, int length)
    {
        port::GuestFrame<0x30> frame;
        const uint32_t record = frame.ptr().address + 0x10u;
        port::store<uint64_t>(record, static_cast<uint64_t>(pts));
        port::store<uint64_t>(record + 8u, static_cast<uint64_t>(dts));
        port::store<int32_t>(record + 0x10u, start - port::load<int32_t>(static_cast<uint32_t>(videoDec) + 0x48u));
        port::store<int32_t>(record + 0x14u, length);
        return Movie_ViBufPutTs(static_cast<int>(kMovieRing), static_cast<int>(record));
    }

    // The library asks for the next timestamp: taken from the ring and copied into the callback data (+8, +0x10).
    int Movie_CbTimeStamp(int, int cbdata)
    {
        port::GuestFrame<0x40> frame;
        const uint32_t ts = frame.ptr().address + 0x20u;
        Movie_ViBufGetTs(GuestPtr<int32_t>{kMovieRing}, GuestPtr<uint64_t>{ts});
        port::store<uint64_t>(static_cast<uint32_t>(cbdata) + 8u, port::load<uint64_t>(ts));
        port::store<uint64_t>(static_cast<uint32_t>(cbdata) + 0x10u, port::load<uint64_t>(ts + 8u));
        return 1;
    }

    int Movie_CbRestartDma()
    {
        Movie_ViBufRestartDma(GuestPtr<uint32_t>{kMovieRing});
        return 1;
    }

    int Movie_CbStopDma()
    {
        Movie_ViBufStopDma(static_cast<int>(kMovieRing));
        return 1;
    }

    // Done when the ring is empty and the decoder holds no reference picture.
    int Movie_VideoDecIsDone(int videoDec)
    {
        if (Movie_ViBufCount(videoDec + 0x48) != 0)
            return 0;
        return sdk::sceMpegIsRefBuffEmpty(videoDec) != 0 ? 1 : 0;
    }

    // After a pause: when the player is up (0x1BDDA0) and was paused (0x1BDDA8), the display goes on and, with sound on
    // (0x1BD9E8), so does the audio stream.
    void Movie_ResumeAll()
    {
        constexpr uint32_t kPlayerUp = 0x001BDDA0u, kPaused = 0x001BDDA8u, kSoundOn = 0x001BD9E8u;
        if (port::load<int32_t>(kPlayerUp) != 0 && port::load<int32_t>(kPaused) != 0)
        {
            Movie_DisplayStart(1);
            if (port::load<int32_t>(kSoundOn) != 0)
                Movie_AudioDecResume(GuestPtr<uint32_t>{kMovieAudio});
            port::store<int32_t>(kPaused, 0);
        }
    }

    PORT_FN(Movie_VideoDecEndPut);
    PORT_FN(Movie_VideoDecBeginPut);
    PORT_FN(Movie_VideoDecPutTs);
    PORT_FN(Movie_CbTimeStamp);
    PORT_FN(Movie_CbRestartDma);
    PORT_FN(Movie_CbStopDma);
    PORT_FN(Movie_VideoDecIsDone);
    PORT_FN(Movie_ResumeAll);

    // The MPEG library has no data left to read: let the other threads run, then feed the input ring again.
    int Movie_CbNoData()
    {
        Movie_SwitchThread();
        Movie_ViBufAddDma(GuestPtr<int32_t>{0x00717CC8u});
        return 1;
    }

    PORT_FN(Movie_CbNoData);

    // One step of the movie player: nothing when no movie is playing (0x1BDDA0); while it is paused (0x1BDDA8) the state is
    // returned as it is; otherwise the movie ends (the player's flags cleared and everything torn down) when fewer than five
    // steps have run (0x1BDDB0), the decoder says it has finished (state 3), or the demuxer has nothing more to read. The result
    // is the playing flag plus twice the paused flag.
    int Movie_Update()
    {
        constexpr uint32_t kPlaying = 0x001BDDA0u, kPaused = 0x001BDDA8u, kSteps = 0x001BDDB0u;
        if (port::load<int32_t>(kPlaying) == 0)
            return 0;
        if (port::load<int32_t>(kPaused) != 0)
            return static_cast<int>(port::load<uint32_t>(kPlaying) + (port::load<uint32_t>(kPaused) << 1));
        if (port::load<int32_t>(kSteps) < 5 || Movie_VideoDecGetState(0x00717C80) == 3 || Movie_ReadAndDemux() == 0)
        {
            port::store<int32_t>(kPlaying, 0);
            port::store<int32_t>(kPaused, 0);
            Movie_TermAll();
        }
        return static_cast<int>(port::load<uint32_t>(kPlaying) + (port::load<uint32_t>(kPaused) << 1));
    }

    PORT_FN(Movie_Update);

    // ---- the decode thread, the stream callbacks and the shutdown (plan P6 batch 22)

    namespace
    {
        constexpr uint32_t kVideoDec = 0x00717C80u, kVideoRing = 0x00717CC8u, kAudioDec = 0x00717D40u, kReadRing = 0x00634C00u, kVideoOut = 0x00717DA0u;
        constexpr uint32_t kSoundOn = 0x001BD9E8u, kReadError = 0x001BDDCCu;

        // A packet of the read ring (`first` bytes at `src`, then `wrapped` bytes from the start of the ring) copied into the two areas
        // (`len0` bytes at `area0`, then `len1` at `area1`) a decoder's input ring offers; 0 when they do not hold it all.
        int copyPacket(uint32_t ring, uint32_t src, int first, int wrapped, uint32_t area0, int len0, uint32_t area1, int len1)
        {
            const int total = first + wrapped;
            if (len0 + len1 < total)
                return 0;
            if (!(first < len0))
            {
                sdk::memcpy(area0, src, len0);
                sdk::memcpy(area1, src + static_cast<uint32_t>(len0), first - len0);
                sdk::memcpy(area1 + static_cast<uint32_t>(first - len0), ring, wrapped);
                return total;
            }
            const int room = len0 - first;
            sdk::memcpy(area0, src, first);
            if (wrapped < room)
                sdk::memcpy(area0 + static_cast<uint32_t>(first), ring, wrapped);
            else
            {
                sdk::memcpy(area0 + static_cast<uint32_t>(first), ring, room);
                sdk::memcpy(area1, ring + static_cast<uint32_t>(room), wrapped - room);
            }
            return total;
        }

        // The uncached view of a RAM address (the decoder's DMA sees what the CPU wrote).
        uint32_t uncached(uint32_t address) { return (address & 0x0FFFFFFFu) | 0x20000000u; }
    }

    // The decode thread's entry: both rings emptied, the pictures decoded until the stream ends, then (the vblank handler showing
    // the last ones meanwhile) the output queue is waited on and the decoder is marked done (state 3).
    void Movie_VideoDecMain(int videoDec)
    {
        Movie_ViBufReset(GuestPtr<int32_t>{static_cast<uint32_t>(videoDec) + 0x48u});
        Movie_VoBufReset(static_cast<int>(kVideoOut));
        Movie_DecodeLoop(videoDec);
        while (port::load<int32_t>(kVideoOut + 0xCu) != 0)
            port::checkpoint(); // the original spins here (calling atanf); the checkpoint gives the other threads the processor
        port::store<int32_t>(static_cast<uint32_t>(videoDec) + 0xA8u, 3);
    }

    // Puts the sequence end code (the word at 0x1BD9F0) into the decoder's input ring once there is room for its four bytes, across
    // the two areas the ring offers; 0 until then. The first time the decoder (state 0) goes to state 2.
    int Movie_VideoDecPutTerminator(int videoDec)
    {
        port::GuestFrame<0x70> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t term = sp + 0x64u;
        port::store<uint32_t>(term, port::load<uint32_t>(0x001BD9F0u));
        Movie_ViBufBeginPut(GuestPtr<int32_t>{static_cast<uint32_t>(videoDec) + 0x48u}, GuestPtr<int32_t>{sp + 0x5Cu}, GuestPtr<int32_t>{sp + 0x68u},
                            GuestPtr<int32_t>{sp + 0x60u}, GuestPtr<int32_t>{sp + 0x6Cu});
        const int32_t first = port::load<int32_t>(sp + 0x68u), second = port::load<int32_t>(sp + 0x6Cu);
        if (first + second < 4)
            return 0;
        const uint32_t area0 = uncached(port::load<uint32_t>(sp + 0x5Cu)), area1 = uncached(port::load<uint32_t>(sp + 0x60u));
        if (first < 5)
        {
            sdk::memcpy(area0, term, first);
            sdk::memcpy(area1, term + static_cast<uint32_t>(first), 4 - first);
            sdk::memcpy(area1 + 4u - static_cast<uint32_t>(first), 0u, 0);
        }
        else
        {
            sdk::memcpy(area0, term, 4);
            sdk::memcpy(area0 + 4u, 0u, 0);
        }
        Movie_ViBufEndPut(static_cast<int>(kVideoRing), 4);
        Movie_ViBufFlush(videoDec + 0x48);
        if (port::load<int32_t>(static_cast<uint32_t>(videoDec) + 0xA8u) == 0)
            port::store<int32_t>(static_cast<uint32_t>(videoDec) + 0xA8u, 2);
        return 1;
    }

    // The stream callback for audio: the packet (the data at +8 of the callback record, +0xC bytes, the first four of them the
    // sub-stream header) in the read ring `anyData` goes to the audio decoder's PCM ring, whichever way either wraps. 1 when it fitted.
    int Movie_PcmCallback(int, int cbdata, int anyData)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t ring = static_cast<uint32_t>(anyData), record = static_cast<uint32_t>(cbdata);
        const int32_t ringSize = port::load<int32_t>(ring + 0x50008u);
        const uint32_t ringEnd = ring + static_cast<uint32_t>(ringSize);
        uint32_t src = port::load<uint32_t>(record + 8u) + 4u;
        if (!(src < ringEnd))
            src -= static_cast<uint32_t>(ringSize);
        const int32_t payload = port::load<int32_t>(record + 0xCu) - 4;
        int32_t first = static_cast<int32_t>(ringEnd - src);
        if (payload < first)
            first = payload;
        const int32_t wrapped = payload - first;
        Movie_AudioDecBeginPut(GuestPtr<int32_t>{kAudioDec}, GuestPtr<int32_t>{sp + 0xA0u}, GuestPtr<int32_t>{sp + 0xA8u}, GuestPtr<int32_t>{sp + 0xA4u},
                               GuestPtr<int32_t>{sp + 0xACu});
        const int total = copyPacket(ring, src, first, wrapped, port::load<uint32_t>(sp + 0xA0u), port::load<int32_t>(sp + 0xA8u), port::load<uint32_t>(sp + 0xA4u),
                                     port::load<int32_t>(sp + 0xACu));
        Movie_AudioDecEndPut(GuestPtr<int32_t>{kAudioDec}, static_cast<uint32_t>(total));
        return total > 0 ? 1 : 0;
    }

    // The stream callback for video: the same for the packet in the read ring `anyData` and the decoder's input ring (through the
    // uncached view), then the packet's time stamps are recorded for where it starts ("pts buffer overflow" when the table is full).
    int Movie_VideoCallback(int, int cbdata, int anyData)
    {
        port::GuestFrame<0xC0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t ring = static_cast<uint32_t>(anyData), record = static_cast<uint32_t>(cbdata);
        const int32_t ringSize = port::load<int32_t>(ring + 0x50008u);
        const uint32_t src = port::load<uint32_t>(record + 8u);
        const uint32_t length = port::load<uint32_t>(record + 0xCu);
        uint32_t first = ring + static_cast<uint32_t>(ringSize) - src;
        if (length < first)
            first = length;
        const int32_t wrapped = static_cast<int32_t>(length - first);
        Movie_VideoDecBeginPut(static_cast<int>(kVideoDec), GuestPtr<int32_t>{sp + 0xB0u}, GuestPtr<int32_t>{sp + 0xB8u}, GuestPtr<int32_t>{sp + 0xB4u},
                               GuestPtr<int32_t>{sp + 0xBCu});
        const int total = copyPacket(ring, src, static_cast<int32_t>(first), wrapped, uncached(port::load<uint32_t>(sp + 0xB0u)), port::load<int32_t>(sp + 0xB8u),
                                     uncached(port::load<uint32_t>(sp + 0xB4u)), port::load<int32_t>(sp + 0xBCu));
        if (total > 0 && Movie_VideoDecPutTs(static_cast<int>(kVideoDec), port::load<int64_t>(record + 0x10u), port::load<int64_t>(record + 0x18u),
                                             static_cast<int>(port::load<uint32_t>(sp + 0xB0u)), total) == 0)
            Movie_Error(0x001BAC20);
        Movie_VideoDecEndPut(static_cast<int>(kVideoDec), total);
        return total > 0 ? 1 : 0;
    }

    PORT_FN(Movie_VideoDecMain);
    PORT_FN(Movie_VideoDecPutTerminator);
    PORT_FN(Movie_PcmCallback);
    PORT_FN(Movie_VideoCallback);

    // Ends the movie: the decoder is given the sequence end code and, unless the read failed, waited on until it has finished
    // (the other threads run meanwhile); then the display and the audio stop, the read and output rings go, the two threads are
    // ended and deleted, the vblank and DMA handlers removed, and the decoder, audio decoder, stream file and subtitle box freed.
    void Movie_TermAll()
    {
        while (Movie_VideoDecPutTerminator(static_cast<int>(kVideoDec)) == 0)
            sdk::RotateThreadReadyQueue(1);
        if (port::load<int32_t>(kReadError) == 0)
            while (Movie_VideoDecIsDone(static_cast<int>(kVideoDec)) == 0 && Movie_VideoDecGetState(static_cast<int>(kVideoDec)) != 3)
                sdk::RotateThreadReadyQueue(1);
        Movie_DisplayStop();
        if (port::load<int32_t>(kSoundOn) != 0)
            Movie_AudioDecReset(GuestPtr<uint32_t>{kAudioDec});
        Movie_ReadBufDelete();
        Movie_VoBufDelete();
        sdk::TerminateThread(port::load<int32_t>(0x001BDD94u));
        sdk::DeleteThread(port::load<int32_t>(0x001BDD94u));
        sdk::TerminateThread(port::load<int32_t>(0x001BDD98u));
        sdk::DeleteThread(port::load<int32_t>(0x001BDD98u));
        sdk::DisableDmac(2);
        sdk::RemoveDmacHandler(2, port::load<int32_t>(0x00717D30u));
        sdk::DisableIntc(2);
        sdk::RemoveIntcHandler(2, port::load<int32_t>(0x00717D34u));
        Movie_VideoDecDelete(static_cast<int>(kVideoDec));
        if (port::load<int32_t>(kSoundOn) != 0)
            Movie_AudioDecDelete(static_cast<int>(kAudioDec));
        Movie_StrFileClose(GuestPtr<int32_t>{0x00717C40u});
        if (port::load<uint32_t>(0x001BDDD0u) != 0)
        {
            const uint32_t box = port::load<uint32_t>(0x001BDDD0u);
            port::store<uint32_t>(0x001BDDD0u, 0);
            TextBox_dtor(GuestPtr<TextBox>{box}, 1);
        }
    }

    // One step of reading and demuxing: unless the file read failed (0), while the file has bytes left (0x1BDDAC) and the read ring has
    // room for 64 KB, the next 64 KB are read into it; the other threads then run; whatever the ring holds is demuxed into the stream
    // callbacks; the audio goes to the IOP; and once the picture queue is full (and the audio is preset) the display and audio start.
    // 1 to go on.
    int Movie_ReadAndDemux()
    {
        constexpr uint32_t kBytesLeft = 0x001BDDACu, kDemuxLeft = 0x001BDDB0u, kStarted = 0x001BDDA4u;
        port::GuestFrame<0x30> frame;
        const uint32_t sp = frame.ptr().address;
        if (port::load<int32_t>(kReadError) != 0)
            return 0;
        const int room = Movie_ReadBufBeginPut(static_cast<int>(kReadRing), GuestPtr<int32_t>{sp + 0x28u});
        if (port::load<int32_t>(kBytesLeft) > 0 && !(room < 0x10000))
        {
            const int got = Movie_StrFileRead(GuestPtr<int32_t>{0x00717C40u}, port::load<int32_t>(sp + 0x28u), 0x10000);
            if (port::load<int32_t>(kReadError) != 0)
                return 0;
            Movie_ReadBufEndPut(static_cast<int>(kReadRing), got);
            port::store<int32_t>(kBytesLeft, port::load<int32_t>(kBytesLeft) - got);
        }
        sdk::RotateThreadReadyQueue(1);
        const int ready = Movie_ReadBufBeginGet(static_cast<int>(kReadRing), GuestPtr<int32_t>{sp + 0x2Cu});
        if (ready > 0)
        {
            const int used = sdk::sceMpegDemuxPssRing(static_cast<int>(kVideoDec), port::load<int32_t>(sp + 0x2Cu), ready, static_cast<int>(kReadRing),
                                                     port::load<int32_t>(kReadRing + 0x50008u));
            Movie_ReadBufEndGet(static_cast<int>(kReadRing), used);
            port::store<int32_t>(kDemuxLeft, port::load<int32_t>(kDemuxLeft) - used);
        }
        if (port::load<int32_t>(kSoundOn) != 0)
            Movie_AudioDecSendToIop(GuestPtr<int32_t>{kAudioDec}, 0, 0, 0);
        if (port::load<int32_t>(kStarted) != 0)
            return 1;
        if (Movie_VoBufIsFull(static_cast<int>(kVideoOut)) == 0)
            return 1;
        if (port::load<int32_t>(kSoundOn) != 0 && Movie_AudioDecIsPreset(static_cast<int>(kAudioDec)) == 0)
            return 1;
        Movie_DisplayStart(1);
        if (port::load<int32_t>(kSoundOn) != 0)
            Movie_AudioDecStart(GuestPtr<uint32_t>{kAudioDec});
        port::store<int32_t>(kStarted, 1);
        return 1;
    }

    PORT_FN(Movie_TermAll);
    PORT_FN(Movie_ReadAndDemux);

    // The decode thread's loop, until the library reports the end of the stream (1) or the decoder is stopped (state 1: -1, with a
    // message): a free picture slot of the output queue (the other threads run until there is one), the fade speed (0x1BDDC8, or
    // 0x4000 when that is 0) started at the frame set in 0x1BDDC4, a picture decoded into the slot (unless the read failed), on the first frame the image packets of every
    // slot built, every 30th frame the next subtitle laid out when its time has come, and the slot counted in. The decoder is reset
    // at the end.
    int Movie_DecodeLoop(int videoDec)
    {
        constexpr uint32_t kQueueData = 0x00717DA0u, kQueueTags = 0x00717DA4u, kQueueSlots = 0x00717DB0u;
        constexpr uint32_t kFadeFrame = 0x001BDDC4u, kFadeStepParam = 0x001BDDC8u, kSubtitleNext = 0x001BD9ECu, kSubtitles = 0x001BDDD4u;
        constexpr uint32_t kWidth = 0x001BDDB4u, kHeight = 0x001BDDB8u;
        const uint32_t dec = static_cast<uint32_t>(videoDec);
        int result = 1;
        int64_t ended = sdk::sceMpegIsEnd(videoDec);
        while (ended == 0)
        {
            if (port::load<int32_t>(dec + 0xA8u) == 1)
            {
                result = -1;
                sdk::printf(0x001BAD50u);
                break;
            }
            int slot = Movie_VoBufGetData(GuestPtr<int32_t>{kQueueData});
            while (slot == 0)
            {
                Movie_SwitchThread();
                slot = Movie_VoBufGetData(GuestPtr<int32_t>{kQueueData});
            }
            const int32_t shown = port::load<int32_t>(kFadeFrame);
            if (shown != 0 && shown == port::load<int32_t>(dec + 8u))
            {
                const int16_t state = port::load<int16_t>(kFadeStepParam);
                Movie_SetSubtitleState(state == 0 ? 0x4000 : static_cast<uint16_t>(state));
            }
            if (port::load<int32_t>(kReadError) == 0)
            {
                const int32_t columns = port::load<int16_t>(kWidth) / 16, rows = port::load<int16_t>(kHeight);
                sdk::sceMpegGetPicture(videoDec, slot, columns * rows / 16);
            }
            if (port::load<int32_t>(dec + 8u) == 0)
            {
                const uint32_t width = port::load<uint32_t>(dec), height = port::load<uint32_t>(dec + 4u);
                uint32_t image = 0, tag = 0;
                for (int i = 0; i < port::load<int32_t>(kQueueSlots); ++i)
                {
                    Movie_BuildImagePacket(port::load<uint32_t>(kQueueTags) + tag + 0x40u, port::load<uint32_t>(kQueueData) + image, 0, static_cast<int>(width), static_cast<int>(height));
                    Movie_BuildImagePacket(port::load<uint32_t>(kQueueTags) + tag + 0x1C940u, port::load<uint32_t>(kQueueData) + image, 1, static_cast<int>(width), static_cast<int>(height));
                    image += 0x12C000u;
                    tag += 0x39240u;
                }
            }
            const int32_t next = port::load<int32_t>(kSubtitleNext);
            const int32_t frame = port::load<int32_t>(dec + 8u);
            if (next >= 0 && frame % 30 == 0 && port::load<uint32_t>(kSubtitles) != 0)
            {
                const uint32_t entry = port::load<uint32_t>(kSubtitles) + 8u * static_cast<uint32_t>(next);
                if (frame / 30 == port::load<int32_t>(entry))
                {
                    Movie_LayoutSubtitle(GuestPtr<char>{port::load<uint32_t>(kSubtitles) + port::load<uint32_t>(entry + 4u)});
                    port::store<int32_t>(kSubtitleNext, port::load<int32_t>(kSubtitleNext) + 1);
                }
            }
            Movie_VoBufIncCount(static_cast<int>(kQueueData));
            Movie_SwitchThread();
            ended = sdk::sceMpegIsEnd(videoDec);
        }
        sdk::sceMpegReset(videoDec);
        return result;
    }

    PORT_FN(Movie_DecodeLoop);

    // Queues the ring data written since the last call on the IPU's input DMA chain (an error message and 0 when the DMA has been
    // stopped, +0x44 is 0). The channel is stopped to read how far it got: the slots it has consumed leave the queue (+0x10 counts
    // them, +0xC is the first), the bytes waiting (+0x14) go in whole 2 KB slots, each given a tag (the last tag of the old chain is
    // changed from an end tag to a call tag first), and the channel is started again with the chain mode refreshed when tags were added.
    int Movie_ViBufAddDma(GuestPtr<int32_t> vibufPtr)
    {
        const int vibuf = static_cast<int>(vibufPtr.address);
        sdk::WaitSema(static_cast<int>(word(vibuf, 0x40)));
        if (word(vibuf, 0x44) == 0)
        {
            Movie_Error(0x001BAD30);
            sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
            return 0;
        }
        stopIpuDma();
        uint32_t chcr = port::readHardware32(kIpuToChcr);
        const int32_t count = static_cast<int32_t>(word(vibuf, 8));
        const uint32_t madr = port::readHardware32(kIpuToMadr);
        uint32_t reached = 0;
        if (madr != ((word(vibuf, 4) + static_cast<uint32_t>(count + 1) * 0x10u) & 0x0FFFFFFFu))
            reached = (madr - word(vibuf, 0)) >> 11;
        const int32_t head = static_cast<int32_t>(word(vibuf, 0xC));
        const int32_t consumed = remainder(static_cast<int32_t>(reached + static_cast<uint32_t>(count) - static_cast<uint32_t>(head)), count);
        setWord(vibuf, 0xC, static_cast<uint32_t>(remainder(head + consumed, count)));
        setWord(vibuf, 0x10, word(vibuf, 0x10) - static_cast<uint32_t>(consumed));
        const int32_t pending = static_cast<int32_t>(word(vibuf, 0x14));
        int32_t tag = remainder(static_cast<int32_t>(word(vibuf, 0xC) + word(vibuf, 0x10)), static_cast<int32_t>(word(vibuf, 8)));
        const int32_t slots = (pending < 0 ? pending + 0x7FF : pending) >> 11;
        int32_t left = pending & 0x7FF;
        if (pending < 0 && left != 0)
            left -= 0x800;
        setWord(vibuf, 0x14, static_cast<uint32_t>(left));
        bool added = false;
        if (slots > 0)
        {
            const int32_t last = remainder(static_cast<int32_t>(word(vibuf, 8) + (word(vibuf, 0xC) + word(vibuf, 0x10) - 1u)), static_cast<int32_t>(word(vibuf, 8)));
            const uint64_t address = word(vibuf, 0) + (static_cast<uint32_t>(last) << 11);
            port::store<uint64_t>(word(vibuf, 4) + (static_cast<uint32_t>(last) << 4), (address << 32) | 0x30000080ull);
            added = true;
        }
        for (int32_t k = 0; k < slots; ++k)
        {
            const uint64_t address = word(vibuf, 0) + (static_cast<uint32_t>(tag) << 11);
            const uint64_t kind = k == slots - 1 ? 0 : 3;
            port::store<uint64_t>(word(vibuf, 4) + (static_cast<uint32_t>(tag) << 4), (address << 32) | (kind << 28) | 0x80ull);
            tag = remainder(tag + 1, static_cast<int32_t>(word(vibuf, 8)));
        }
        setWord(vibuf, 0x10, word(vibuf, 0x10) + static_cast<uint32_t>(slots));
        if (word(vibuf, 0x10) != 0)
        {
            if (added)
                chcr = (chcr & 0x0FFFFFFFu) | 0x30000000u;
            sdk::DIntr();
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) | 0x10000u);
            port::writeHardware32(kIpuToChcr, chcr | 0x100u);
            port::writeHardware32(kDmacEnablew, port::readHardware32(kDmacEnabler) & 0xFFFEFFFFu);
            sdk::EIntr();
        }
        sdk::SignalSema(static_cast<int>(word(vibuf, 0x40)));
        return 1;
    }

    PORT_FN(Movie_ViBufAddDma);

    // ---- the player's set-up, the vblank handler and the entry points (plan P6 batch 22)

    namespace
    {
        // The GS display and draw environments of the movie (sceGsDBuffDc at 0x717DC0) and the half-offset environments of the two fields.
        constexpr uint32_t kDisplayBuffers = 0x00717DC0u;
        // The picture queue's record states: 2 decoded, 1 first field shown, 0 shown.
        uint32_t halfOffsetFor(bool odd, uint32_t whenOdd, uint32_t whenEven) { return ((odd ? whenOdd : whenEven) & 0x0FFFFFFFu) | 0x20000000u; }
    }

    // Clears both display buffers to black: a black sprite over the picture area is drawn into each of the two buffers in turn (the
    // pad is read between, as the original does), then the display is swapped.
    void Movie_ClearScreen()
    {
        port::GuestFrame<0x80> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t pk = sp + 0x60u, tag = sp + 0x70u;
        port::store<uint64_t>(tag, port::load<uint64_t>(0x001B38A0u));
        port::store<uint64_t>(tag + 8u, port::load<uint64_t>(0x001B38A8u));
        const int channel = sdk::sceDmaGetChan(2);
        sdk::sceGifPkInit(pk, (0x007180F0u & 0x0FFFFFFFu) | 0x20000000u);
        sdk::sceGifPkReset(pk);
        sdk::sceGifPkEnd(pk);
        const int32_t width = port::load<int16_t>(0x001BDDBCu);
        const int32_t left = (0x800 - width / 2) * 16;
        const int32_t halfHeight = port::load<int16_t>(0x001BDDC0u) / 2;
        const int32_t top = (0x800 - halfHeight / 2) * 16;
        sdk::sceGifPkOpenGifTag(pk, port::Quad{port::load<uint64_t>(tag), port::load<uint64_t>(tag + 8u)});
        sdk::sceGifPkAddGsAD(pk, 0, 6);
        sdk::sceGifPkAddGsAD(pk, 1, 0x100000000ull);
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left)) | (static_cast<uint64_t>(static_cast<int64_t>(top)) << 16));
        sdk::sceGifPkAddGsAD(pk, 5, static_cast<uint64_t>(static_cast<int64_t>(left + width * 16)) |
                                        (static_cast<uint64_t>(static_cast<int64_t>(top + halfHeight * 16)) << 16));
        sdk::sceGifPkCloseGifTag(pk);
        sdk::sceGifPkTerminate(pk);
        for (int field = 0; field < 2; ++field)
        {
            Pad_Poll();
            Pad_GetData(0);
            sdk::sceGsSyncV(0);
            sdk::sceGsSetHalfOffset(field == 0 ? 0x20717F90u : 0x20717E20u, 0x800, 0x800, field);
            sdk::sceGsSetHalfOffset2(field == 0 ? 0x20718010u : 0x20717EA0u, 0x800, 0x800, field);
            sdk::sceGsSwapDBuffDc(kDisplayBuffers, 0);
            sdk::sceGsSyncPath(0, 0);
            sdk::sceDmaSend(channel, 0x007180F0u);
        }
    }

    // The vertical blank interrupt of the player. The GS field (CSR bit 13) is noted (0x1BDD64). While the display runs (0x1BDD68) and
    // the GIF path is idle (sceGsSyncPath(1)): with no picture decoded the miss is counted (0x1BDD9C); on the first field's turn a
    // picture decoded (record state 2) has its field-0 packet sent, to the display environment of the field, and goes to state 1;
    // on the second field's turn a picture in state 1 has its field-1 packet sent and goes to state 0, which tells the DMA end
    // handler (0x1BDD74) to release it. After each packet the fade overlay (Movie_SendFrame) and, while it is -1 with subtitles on
    // (0x1BD9EC not -1) and laid out (the byte at 0x215F00), the subtitle packet are sent. While the display is off, the
    // state-2 handoff (0x1BDD78) of the last frame is drawn once with the packet of the field's half. Interrupts are re-enabled at the end.
    int Movie_VblankHandler()
    {
        constexpr uint32_t kField = 0x001BDD64u, kDisplayOn = 0x001BDD68u, kTicks = 0x001BDD6Cu, kPathBusy = 0x001BDD70u, kSending = 0x001BDD74u,
                           kHandoff = 0x001BDD78u, kMisses = 0x001BDD9Cu, kSubtitleNext = 0x001BD9ECu, kSubtitleReady = 0x00215F00u;
        constexpr uint32_t kQueue = 0x00717DA0u;
        const auto subtitles = [&](int channel) {
            const int16_t step = static_cast<int16_t>(Movie_SendFrame(channel));
            if (step == -1 && port::load<int32_t>(kSubtitleNext) != -1 && channel != 0 && static_cast<int8_t>(port::load<uint8_t>(kSubtitleReady)) != 0)
                sdk::sceDmaSend(channel, 0x0020C1A0u);
        };
        const auto interruptsOn = [] { port::g_call.ctx->cop0_status |= 0x10000; };
        int channel = sdk::sceDmaGetChan(2);
        port::store<int32_t>(kField, static_cast<int32_t>((port::readHardware64(0x12001000u) >> 13) & 1u));
        const uint32_t field = static_cast<uint32_t>(port::load<int32_t>(kField));
        if (port::load<int32_t>(kDisplayOn) != 0)
        {
            port::store<int32_t>(kTicks, port::load<int32_t>(kTicks) + 1);
            port::store<int32_t>(kPathBusy, sdk::sceGsSyncPath(1, 0));
            if (port::load<int32_t>(kPathBusy) == 0)
            {
                const uint32_t picture = static_cast<uint32_t>(Movie_VoBufGetTag(static_cast<int>(kQueue)));
                if (picture == 0)
                {
                    port::store<int32_t>(kMisses, port::load<int32_t>(kMisses) + 1);
                    interruptsOn();
                    return 0;
                }
                if (field == 0)
                {
                    if (port::load<int32_t>(picture) == 2)
                    {
                        sdk::sceGsSetHalfOffset(halfOffsetFor(false, 0x00717F90u, 0x00717E20u), 0x800, 0x800, static_cast<int>(field ^ 1u));
                        sdk::sceGsSetHalfOffset2(halfOffsetFor(false, 0x00718010u, 0x00717EA0u), 0x800, 0x800, static_cast<int>(field ^ 1u));
                        sdk::sceGsSwapDBuffDc(kDisplayBuffers, 0);
                        sdk::sceGsSyncPath(0, 0);
                        sdk::sceDmaSend(channel, picture + 0x40u);
                        port::store<int32_t>(picture, 1);
                        subtitles(channel);
                    }
                }
                else if (field == 1)
                {
                    if (port::load<int32_t>(picture) == 1)
                    {
                        sdk::sceGsSetHalfOffset(halfOffsetFor(true, 0x00717F90u, 0x00717E20u), 0x800, 0x800, static_cast<int>(field ^ 1u));
                        sdk::sceGsSetHalfOffset2(halfOffsetFor(true, 0x00718010u, 0x00717EA0u), 0x800, 0x800, static_cast<int>(field ^ 1u));
                        sdk::sceGsSwapDBuffDc(kDisplayBuffers, 1);
                        sdk::sceGsSyncPath(0, 0);
                        sdk::sceDmaSend(channel, picture + 0x1C940u);
                        port::store<int32_t>(picture, 0);
                        subtitles(channel);
                        port::store<int32_t>(kSending, 1);
                    }
                }
            }
        }
        else if (port::load<int32_t>(kHandoff) == 2)
        {
            channel = sdk::sceDmaGetChan(2);
            port::store<int32_t>(kPathBusy, sdk::sceGsSyncPath(1, 0));
            if (port::load<int32_t>(kPathBusy) == 0)
            {
                const uint32_t picture = static_cast<uint32_t>(Movie_VoBufGetTag(static_cast<int>(kQueue)));
                if (picture == 0)
                {
                    port::store<int32_t>(kMisses, port::load<int32_t>(kMisses) + 1);
                    interruptsOn();
                    return 0;
                }
                const bool odd = (field & 1u) != 0;
                sdk::sceGsSetHalfOffset(halfOffsetFor(odd, 0x00717F90u, 0x00717E20u), 0x800, 0x800, static_cast<int16_t>(field ^ 1u));
                sdk::sceGsSetHalfOffset2(halfOffsetFor(odd, 0x00718010u, 0x00717EA0u), 0x800, 0x800, static_cast<int16_t>(field ^ 1u));
                sdk::sceGsSwapDBuffDc(kDisplayBuffers, static_cast<int>(field));
                sdk::sceGsSyncPath(0, 0);
                sdk::sceDmaSend(channel, picture + (odd ? 0x1C900u : 0u) + 0x40u);
                subtitles(channel);
            }
        }
        interruptsOn();
        port::store<int32_t>(kHandoff, 1);
        return 0;
    }

    // ---- playing a PSS file

    // Plays the movie `name` (a path under cdrom0: built from three pieces and printed) until it ends. After the first six passes one
    // of `skipButtons` pressed starts the fade out at `fadeStep` (the speed at 0x1BDD80, which the decode loop also sets at frame
    // `stack0`), the SPU master volume going down by 0x100 a pass with `fadeAudio`; when the fade has run (0x7F0) the decoder is
    // told to finish. a1, a2, width and height are the picture's size parameters for the player, and a non-zero `stack8` clears the
    // screen afterwards. Nothing happens when a movie is already playing.
    void Movie_PlayPss(GuestPtr<char> name, int16_t a1, int16_t a2, int width, int16_t height, int fadeAudio, uint16_t skipButtons, int16_t fadeStep, int stack0,
                       int stack8)
    {
        constexpr uint32_t kPlaying = 0x001BDDA0u, kPaused = 0x001BDDA8u, kStarted = 0x001BDDA4u, kBytesLeft = 0x001BDDACu, kDemuxLeft = 0x001BDDB0u;
        constexpr uint32_t kFadeSpeed = 0x001BDD80u, kFadeStep = 0x001BDD7Cu, kFadeColour = 0x001BDD84u, kFadeStepParam = 0x001BDDC8u, kFadeFrame = 0x001BDDC4u;
        port::GuestFrame<0x1B0> frame;
        const uint32_t sp = frame.ptr().address;
        const uint32_t path = sp + 0xB0u;
        port::store<int16_t>(sp + 0xAEu, height);
        port::store<int16_t>(sp + 0xACu, fadeStep);
        uint32_t passes = 0;
        bool finished = false;
        int16_t volume = 0x3FFF;
        sdk::strcpy(path, 0x001BAB70u);
        sdk::strcat(path, name.address);
        sdk::strcat(path, 0x001BAB80u);
        port::store<uint8_t>(kFadeColour, 0);
        port::store<uint8_t>(kFadeColour + 1u, 0);
        port::store<int16_t>(kFadeStep, 0);
        port::store<int16_t>(kFadeStepParam, port::load<int16_t>(sp + 0xACu));
        port::store<int32_t>(kFadeFrame, stack0);
        port::store<uint8_t>(kFadeColour + 2u, 0);
        sdk::printf(0x001BAB88u, path);
        int result = 0;
        if (port::load<int32_t>(kPlaying) == 0)
        {
            port::store<int32_t>(0x001BD9E8u, fadeAudio);
            port::store<int16_t>(kFadeSpeed, 0);
            if (Movie_StrFileOpen(GuestPtr<int32_t>{0x00717C40u}, static_cast<int>(path)) == 0)
            {
                Movie_StrFileClose(GuestPtr<int32_t>{0x00717C40u});
                result = -1;
            }
            else
            {
                Movie_InitAll(a1, a2, width, port::load<int16_t>(sp + 0xAEu));
                result = 1;
                port::store<int32_t>(kStarted, 0);
                port::store<int32_t>(kPaused, 0);
                port::store<int32_t>(kPlaying, 1);
                port::store<int32_t>(kBytesLeft, port::load<int32_t>(0x00717C44u));
                port::store<int32_t>(kDemuxLeft, port::load<int32_t>(0x00717C44u));
            }
        }
        if (result != 1)
            return;
        for (;;)
        {
            Pad_Poll();
            const uint32_t pad = static_cast<uint32_t>(Pad_GetData(0));
            uint32_t buttons = 0;
            if (pad != 0)
                buttons = ((static_cast<uint32_t>(port::load<uint8_t>(pad + 2u)) << 8) | port::load<uint8_t>(pad + 3u)) ^ 0xFFFFu;
            ++passes;
            if (Movie_Update() == 0)
                break;
            if (passes < 6 || finished)
                continue;
            if (port::load<int16_t>(kFadeSpeed) == 0)
            {
                if ((buttons & skipButtons) != 0)
                    port::store<int16_t>(kFadeSpeed, port::load<int16_t>(kFadeStepParam));
            }
            else if (fadeAudio != 0)
            {
                volume = static_cast<int16_t>(volume - 0x100);
                if (volume < 0)
                    volume = 0;
                Movie_SetSpuMasterVolume(volume);
            }
            if (port::load<int16_t>(kFadeStep) == 0x7F0)
            {
                if (port::load<int32_t>(kPlaying) != 0)
                {
                    Movie_ResumeAll();
                    Movie_VideoDecSetStateStart(0x00717C80);
                    Movie_ReadAndDemux();
                }
                finished = true;
            }
        }
        if (fadeAudio != 0)
            Movie_SetSpuMasterVolume(0x3FFF);
        if (stack8 != 0)
            Movie_ClearScreen();
    }

    // Sets the player up for a movie of the given picture size: the thread priority (1), the size words (0x1BDDB4 to 0x1BDDC0), the display
    // and draw buffers for the half height, the DMA controller, the read ring, the MPEG library and decoder (and the audio decoder
    // with sound), the stream callbacks for video (stream 0) and audio (stream 2), the picture queue (two pictures), the read thread
    // (0x19E040, stack 0x634400) and decode thread (Movie_VideoDecMain, stack 0x710C40) with their ids at 0x1BDD98 and 0x1BDD94,
    // and the vblank (INTC 2) and DMA end (DMAC 2) handlers with their ids at 0x717D34 and 0x717D30.
    void Movie_InitAll(int16_t a0, int16_t a1, int width, int16_t height)
    {
        port::GuestFrame<0xA0> frame;
        const uint32_t sp = frame.ptr().address;
        sdk::ChangeThreadPriority(sdk::GetThreadId(), 1);
        port::store<int16_t>(0x001BDDB4u, a0);
        port::store<int16_t>(0x001BDDB8u, a1);
        port::store<int16_t>(0x001BDDBCu, static_cast<int16_t>(width));
        port::store<int16_t>(0x001BDDC0u, height);
        port::store<int32_t>(0x001BDDCCu, 0);
        sdk::sceGsSetDefDBuffDc(kDisplayBuffers, 0, width, static_cast<int16_t>(height / 2), 0, 0, 1);
        port::store<uint64_t>(0x00717F40u, 0x3F80000000000000ull);
        port::store<uint64_t>(0x007180B0u, 0x3F80000000000000ull);
        sdk::FlushCache(0);
        port::writeHardware32(0x1000E000u, port::readHardware32(0x1000E000u) | 3u);
        port::writeHardware32(0x1000E010u, 4u);
        Movie_ReadBufCreate(static_cast<int>(kReadRing));
        sdk::sceMpegInit();
        const int32_t pixels = port::load<int16_t>(0x001BDDBCu) * port::load<int16_t>(0x001BDDC0u) * 9;
        Movie_VideoDecCreate(static_cast<int>(kVideoDec), 0x004E1480, pixels / 2 + 0x1768, 0x00690C40, 0x004E0440, 0x100, 0x00714C40, 0x200);
        if (port::load<int32_t>(kSoundOn) != 0)
        {
            sdk::sceSdRemoteInit();
            sdk::sceSdRemote(1, 0x8000, 0);
            Movie_AudioDecCreate(GuestPtr<uint32_t>{kAudioDec}, 0x00684C40, 0xC000, 0x6000);
        }
        Movie_VideoDecAddStrCallback(static_cast<int>(kVideoDec), 0, 0, 0x0019E930, static_cast<int>(kReadRing));
        if (port::load<int32_t>(kSoundOn) != 0)
            Movie_VideoDecAddStrCallback(static_cast<int>(kVideoDec), 2, 0, 0x0019E780, static_cast<int>(kReadRing));
        Movie_VoBufCreate(GuestPtr<uint32_t>{kVideoOut}, static_cast<int>((0x00215FC0u & 0x0FFFFFFFu) | 0x20000000u), 0x0046DFC0, 2);
        const uint32_t param = sp + 0x70u;
        const uint32_t gpValue = static_cast<uint32_t>(port::gpr64(port::g_call.ctx, 28));
        port::store<uint32_t>(sp + 0x74u, addr::Movie_IdleThread);
        port::store<uint32_t>(sp + 0x78u, 0x00634400u);
        port::store<uint32_t>(sp + 0x7Cu, 0x800);
        port::store<uint32_t>(sp + 0x84u, 1);
        port::store<uint32_t>(sp + 0x80u, gpValue);
        port::store<uint32_t>(sp + 0x90u, 0);
        port::store<int32_t>(0x001BDD98u, sdk::CreateThread(param));
        sdk::StartThread(port::load<int32_t>(0x001BDD98u), 0);
        port::store<uint32_t>(sp + 0x74u, 0x001A0530u);
        port::store<uint32_t>(sp + 0x78u, 0x00710C40u);
        port::store<uint32_t>(sp + 0x7Cu, 0x4000);
        port::store<uint32_t>(sp + 0x84u, 1);
        port::store<uint32_t>(sp + 0x80u, gpValue);
        port::store<uint32_t>(sp + 0x90u, 0);
        port::store<int32_t>(0x001BDD94u, sdk::CreateThread(param));
        sdk::StartThread(port::load<int32_t>(0x001BDD94u), static_cast<int>(kVideoDec));
        port::store<int32_t>(0x00717D34u, sdk::AddIntcHandler(2, 0x0019C4C0u, 0));
        sdk::EnableIntc(2);
        port::store<int32_t>(0x00717D30u, sdk::AddDmacHandler(2, 0x0019C480u, 0));
        sdk::EnableDmac(2);
    }

    // The mission movies: nothing (0) while the sound driver is busy or reports itself busy. Missions 1 to 5 play
    // DATA\MPEG\MISSION<n>.PSS (the name is built in the frame) at 640 x 448, skippable with Start/Cross (0x840), the sound faded
    // out at the end; mission 1 first loads its subtitles (MISSION1.BIN, the table at 0x1BDDD4 and the subtitle packet built), and the
    // subtitle index (0x1BD9EC) starts at -1 and is 0 only for mission 1. Mission 6 plays ENDING.PSS with no skipping, its subtitles from
    // frame 0x6B4. Returns 1 once the movie has played.
    int PlayMissionMovie(char mission)
    {
        port::GuestFrame<0x240> frame;
        const uint32_t sp = frame.ptr().address;
        if (Sound_IsBusy() != 0)
            return 0;
        if (Sound_IsDriverBusy() != 0)
            return 0;
        port::store<int32_t>(0x001BD9ECu, -1);
        const uint32_t number = static_cast<uint8_t>(mission);
        if (number == 6)
        {
            if (Sound_IsBusy() != 0)
                return 0;
            if (Sound_IsDriverBusy() != 0)
                return 0;
            Movie_PlayPss(GuestPtr<char>{0x001BABD0u}, 0x280, 0x1C0, 0x280, 0x1C0, 1, 0, 0, 0x6B4, 0);
            return 1;
        }
        const uint32_t digits = sp + 0x23Cu, subtitlePath = sp + 0x30u, moviePath = sp + 0x130u;
        sdk::sprintf(digits, 0x001BAB98u, number);
        sdk::strcpy(subtitlePath, 0x001BABA0u);
        sdk::strcat(subtitlePath, digits);
        sdk::strcpy(moviePath, subtitlePath);
        sdk::strcat(subtitlePath, 0x001BABB8u);
        sdk::strcat(moviePath, 0x001BABC0u);
        if (number == 1)
        {
            const GuestPtr<GameFile> file{sp + 0x230u};
            File_ctor(file, 0);
            port::store<uint32_t>(0x001BDDD4u, static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{subtitlePath}, GuestPtr<int32_t>{0})));
            port::store<int32_t>(0x001BD9ECu, 0);
            Movie_BuildSubtitlePacket();
            File_dtor(file, -1);
        }
        Movie_PlayPss(GuestPtr<char>{moviePath}, 0x280, 0x1C0, 0x280, 0x1C0, 1, 0x840, 0x20, 0, 1);
        return 1;
    }

    // The opening movie, DATA\MPEG\OPENING.PSS at 640 x 448 (skippable with Start/Cross), once the sound driver is idle (0 otherwise).
    int PlayOpeningMovie()
    {
        if (Sound_IsBusy() != 0)
            return 0;
        if (Sound_IsDriverBusy() != 0)
            return 0;
        Movie_PlayPss(GuestPtr<char>{0x001BABF0u}, 0x280, 0x1C0, 0x280, 0x1C0, 1, 0x840, 0x20, 0, 1);
        return 1;
    }

    // The movie's idle thread (the second thread Movie_InitAll starts, stack 0x634400): it gives the processor away for ever. The
    // recompiled loop counted 32 cycles at its back branch.
    void Movie_IdleThread()
    {
        for (;;)
        {
            sdk::RotateThreadReadyQueue(1);
            port::checkpoint();
        }
    }

    PORT_FN(Movie_ClearScreen);
    PORT_FN(Movie_IdleThread);
    PORT_FN(Movie_VblankHandler);
    PORT_FN(Movie_PlayPss);
    PORT_FN(Movie_InitAll);
    PORT_FN(PlayMissionMovie);
    PORT_FN(PlayOpeningMovie);
}
