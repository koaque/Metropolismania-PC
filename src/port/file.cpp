// Disc file access and the functions that wait for it (plan P6 pilot): the file handle calls, the request queue's
// pump and waits, the font loader, the heap probe and the base destructor. All of them can reach a thread switch (the
// SDK calls under them are the recompiled libc and kernel code), so they run on fibers.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        GuestPtr<FileRequest> requestAt(int32_t index)
        {
            return GuestPtr<FileRequest>{globals::kFileRequests + 0x1Cu * static_cast<uint32_t>(index)};
        }

        // Moves the tail on past a request just filled in, wrapping at 64, and counts it.
        int32_t finishRequest()
        {
            int32_t tail = globals::fileQueueTail.get() + 1;
            globals::fileQueueTail.set(tail);
            if (tail >= 0x40)
            {
                tail = 0;
                globals::fileQueueTail.set(0);
            }
            globals::fileQueueCount.set(globals::fileQueueCount.get() + 1);
            return tail;
        }

        // What the synchronous calls start with: pump the queue until it is empty, telling the sound driver the disc is busy
        // while it does. The original calls atanf(1.0) between pumps, as a pause.
        void drainQueue()
        {
            bool busy = false;
            while (globals::fileQueueHead.get() != globals::fileQueueTail.get())
            {
                if (!busy)
                {
                    SoundCmd_SetDiscBusy(1);
                    SoundCmd_Flush();
                    busy = true;
                }
                FileQueue_Pump();
                sdk::atanf(ps2f(1.0f));
            }
            if (busy)
                SoundCmd_SetDiscBusy(0);
        }
    }

    // Seeks (synchronously, after the queue is empty) or queues a seek; returns the position, or the queue tail.
    int File_Seek(GuestPtr<GameFile> file, int offset, int whence, uint16_t t0)
    {
        if (file->fd < 0)
            return 0;
        if (file->async == 0)
        {
            drainQueue();
            int result;
            do
            {
                result = sdk::sceLseek(file->fd, offset, whence);
            } while (result == -1);
            return result;
        }
        const GuestPtr<FileRequest> request = requestAt(globals::fileQueueTail.get());
        request->op = 3;
        request->state = 0;
        request->t0 = t0;
        request->fd = file->fd;
        request->offset = offset;
        request->whence = whence;
        return finishRequest();
    }

    int File_Read(GuestPtr<GameFile> file, int buffer, int count, int size, uint16_t t0)
    {
        if (file->fd < 0)
            return 0;
        if (file->async == 0)
        {
            drainQueue();
            int result;
            do
            {
                result = sdk::sceRead(file->fd, static_cast<uint32_t>(buffer), count * size);
            } while (result < 0);
            return result;
        }
        const GuestPtr<FileRequest> request = requestAt(globals::fileQueueTail.get());
        request->op = 4;
        request->state = 0;
        request->t0 = t0;
        request->fd = file->fd;
        request->buffer = buffer;
        request->bytes = count * size;
        return finishRequest();
    }

    int File_Close(GuestPtr<GameFile> file)
    {
        if (file->async == 0)
        {
            drainQueue();
            if (file->fd >= 0)
                sdk::sceClose(file->fd);
            file->fd = -1;
            return 0;
        }
        if (file->fd < 0)
            return 0;
        const GuestPtr<FileRequest> request = requestAt(globals::fileQueueTail.get());
        request->op = 2;
        request->state = 0;
        request->t0 = 0;
        request->fd = file->fd;
        return finishRequest();
    }

    // Waits for the disc, builds "cdrom0:\" + path + ";1" in its frame and opens it; an asynchronous file also queues a
    // request that polls the open.
    int File_Open(GuestPtr<GameFile> file, GuestPtr<char> path)
    {
        port::GuestFrame<0xB0> frame;
        const uint32_t buffer = frame.ptr().address + 0x30u;
        while (sdk::sceCdDiskReady(0) != 2)
        {
        }
        sdk::strcpy(buffer, 0x001B9AE8u);
        sdk::strcat(buffer, path.address);
        sdk::strcat(buffer, 0x001B9AF8u);
        if (file->async == 0)
            drainQueue();
        file->fd = sdk::sceOpen(buffer, file->async | 1);
        if (file->async != 0)
        {
            const GuestPtr<FileRequest> request = requestAt(globals::fileQueueTail.get());
            request->op = 1;
            request->state = 1;
            request->t0 = 0;
            request->fd = file->fd;
            finishRequest();
        }
        return file->fd;
    }

    // Opens a1, reads the whole file into a new block and closes it; the size goes to *a2 when given. Returns the block.
    int File_LoadWhole(GuestPtr<GameFile> file, GuestPtr<char> path, GuestPtr<int32_t> size)
    {
        file->fd = File_Open(file, path);
        if (file->fd < 0)
            return 0;
        const int length = sdk::sceLseek(file->fd, 0, 2);
        const uint32_t block = sdk::malloc(static_cast<uint32_t>(length));
        sdk::sceLseek(file->fd, 0, 0);
        sdk::sceRead(file->fd, block, length);
        sdk::sceClose(file->fd);
        if (size.address != 0u)
            *size = length;
        return static_cast<int>(block);
    }

    // Pumps the queue until request a1 is no longer in it (a2 nonzero: without telling the sound driver). No caller uses
    // a result; the original leaves whatever its last call did in v0.
    void FileQueue_WaitFor(int, int request, int quiet)
    {
        bool busy = false;
        for (;;)
        {
            // FileQueue_IsDone's walk: empty, or the request is between the head and the tail.
            bool done = globals::fileQueueHead.get() == globals::fileQueueTail.get();
            for (int32_t at = globals::fileQueueHead.get(); !done && at != globals::fileQueueTail.get();)
            {
                if (at == request)
                {
                    done = true;
                    break;
                }
                if (--at < 0)
                    at = 0x3F;
            }
            if (done)
                break;
            if (!busy && quiet == 0)
            {
                SoundCmd_SetDiscBusy(1);
                SoundCmd_Flush();
                busy = true;
            }
            FileQueue_Pump();
            sdk::atanf(ps2f(1.0f));
        }
        if (busy)
            SoundCmd_SetDiscBusy(0);
    }

    // Pumps the queue until it is empty (a0 nonzero: without telling the sound driver). No caller uses a result.
    void FileQueue_WaitAll(int quiet)
    {
        bool busy = false;
        while (globals::fileQueueHead.get() != globals::fileQueueTail.get())
        {
            if (!busy && quiet == 0)
            {
                SoundCmd_SetDiscBusy(1);
                SoundCmd_Flush();
                busy = true;
            }
            FileQueue_Pump();
            sdk::atanf(ps2f(1.0f));
        }
        if (busy)
            SoundCmd_SetDiscBusy(0);
    }

    // Runs the request at the head of the queue one step: when the disc is ready, issues it (read, seek or close; an open
    // was issued by File_Open); on the step after, polls it with sceIoctl and removes it from the queue when it is done.
    int FileQueue_Pump()
    {
        int32_t v0 = port::guestV0(); // the paths that call nothing leave v0 as it was
        const int32_t head = globals::fileQueueHead.get();
        if (head == globals::fileQueueTail.get())
            return v0;
        const GuestPtr<FileRequest> request = requestAt(head);
        if (request->op == 0)
            return v0;
        if (request->state == 0)
        {
            const int64_t ready = sdk::sceCdDiskReady(request->t0);
            if (ready != 2)
                return static_cast<int32_t>(ready);
            v0 = 2;
            const int32_t op = request->op;
            if (op == 4)
            {
                do
                {
                    v0 = sdk::sceRead(request->fd, static_cast<uint32_t>(request->buffer), request->bytes);
                } while (v0 < 0);
            }
            else if (op == 3)
            {
                do
                {
                    v0 = sdk::sceLseek(request->fd, request->offset, request->whence);
                } while (v0 < 0);
            }
            else if (op == 2)
            {
                int attempts = 10;
                bool failed = true;
                while (failed && attempts != 0)
                {
                    v0 = sdk::sceClose(request->fd);
                    failed = v0 < 0;
                    --attempts;
                }
            }
            request->state = 1;
            return v0;
        }
        v0 = sdk::sceIoctl(request->fd, 1, 0x001BDC94u);
        if (request->op == 2 || v0 >= 0)
        {
            if (globals::fileQueueIoctl.get() == 0)
            {
                request->state = 0;
                request->op = 0;
                int32_t next = globals::fileQueueHead.get() + 1;
                globals::fileQueueHead.set(next);
                if (next >= 0x40)
                    globals::fileQueueHead.set(0);
                globals::fileQueueCount.set(globals::fileQueueCount.get() - 1);
            }
        }
        else
        {
            request->state = 0;
        }
        return v0;
    }

    // Closes a synchronous open file; deletes the object when the flag is positive.
    GuestPtr<GameFile> File_dtor(GuestPtr<GameFile> file, int16_t deleteFlag)
    {
        if (file.address != 0u)
        {
            if (file->async == 0 && file->fd != -1)
                File_Close(file);
            if (deleteFlag > 0)
                sdk::operator_delete(file.address);
        }
        return file;
    }

    int BaseObject_dtor(int object, int16_t deleteFlag)
    {
        if (object != 0 && deleteFlag > 0)
            sdk::operator_delete(static_cast<uint32_t>(object));
        return object;
    }

    // Frees the loaded font. The one caller ignores the result.
    void Font_Free()
    {
        if (globals::fontData.get() != 0u)
        {
            sdk::free(globals::fontData.get());
            globals::fontData.set(0u);
        }
    }

    // Loads DATA\FONT1222.GF: the path is "DATA\FONT" + sprintf("%d", 11222) without its first digit + ".GF".
    int Font_Load()
    {
        port::GuestFrame<0xC0> frame;
        const uint32_t base = frame.ptr().address;
        const GuestPtr<GameFile> file{base + 0xB8u};
        File_ctor(file, 0);
        sdk::sprintf(base + 0x90u, 0x001B9B00u, 0x2BD6);
        sdk::strcpy(base + 0x10u, 0x001B9B08u);
        sdk::strcat(base + 0x10u, base + 0x91u);
        sdk::strcat(base + 0x10u, 0x001B9B18u);
        globals::fontData.set(static_cast<uint32_t>(File_LoadWhole(file, GuestPtr<char>{base + 0x10u}, GuestPtr<int32_t>{0u})));
        File_dtor(file, -1);
        return 1;
    }

    // Checks the heap can hold the game's largest allocation: allocates and frees 0x17E5D70 bytes.
    int Heap_Probe()
    {
        sdk::free(sdk::malloc(0x17E5D70u));
        return 0x17E5D70;
    }

    PORT_FN(File_Seek);
    PORT_FN(File_Read);
    PORT_FN(File_Close);
    PORT_FN(File_Open);
    PORT_FN(File_LoadWhole);
    PORT_FN(FileQueue_WaitFor);
    PORT_FN(FileQueue_WaitAll);
    PORT_FN(FileQueue_Pump);
    PORT_FN(File_dtor);
    PORT_FN(BaseObject_dtor);
    PORT_FN(Font_Free);
    PORT_FN(Font_Load);
    PORT_FN(Heap_Probe);
}
