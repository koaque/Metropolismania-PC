// The movie player's buffers and small state accessors: the read ring (0x50000 bytes of the PSS stream, then its write
// position +0x50000, fill +0x50004 and size +0x50008), the video output ring (frames of 0x12C000 bytes tagged with a
// 0x39240-byte header each), the video input ring's timestamp list and the audio ring. The parts that talk to the
// hardware or wait on semaphores are not here (they belong to the threading step).

#include "game_functions.h"
#include "game_globals.h"

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
}
