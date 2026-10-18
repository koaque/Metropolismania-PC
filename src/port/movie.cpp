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

    // Hands its own arguments on to the library (the original has none of its own).
    int Movie_VideoDecAddStrCallback(int a0, int a1, int a2, int a3)
    {
        sdk::sceMpegAddStrCallback(a0, a1, a2, a3);
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
}
