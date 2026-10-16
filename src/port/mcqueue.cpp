// The memory card request queue: 64 slots of 0x24 bytes, filled at the tail and issued one at a time by
// mm_McQueue_Pump (sceMcOpen ... sceMcSeek). Each slot: +0 command, +4 state, +0xC where to put the result
// (0: the slot's own word at +0xC), arguments from +0x10. The queue functions return the new tail.

#include "game_functions.h"
#include "game_globals.h"

#include <initializer_list>

namespace mm
{
    namespace
    {
        int queueRequest(int32_t command, uint32_t result, std::initializer_list<int32_t> args)
        {
            const int32_t tail = globals::mcQueueTail.get();
            const uint32_t slot = globals::kMcRequests + 0x24u * static_cast<uint32_t>(tail);
            port::store<int32_t>(slot + 0x0, command);
            port::store<int32_t>(slot + 0x4, 0);
            uint32_t offset = 0x10;
            for (int32_t arg : args)
            {
                port::store<int32_t>(slot + offset, arg);
                offset += 4;
            }
            port::store<uint32_t>(slot + 0xC, result);
            const int32_t next = tail + 1;
            globals::mcQueueTail.set(next > 63 ? 0 : next);
            return globals::mcQueueTail.get();
        }
    }

    int McQueue_GetInfo(int, int port, int type, int free, int format, GuestPtr<int32_t> result)
    {
        return queueRequest(1, result.address, {port, type, free, format});
    }

    int McQueue_GetDir(int, int port, int name, int mode, int maxEntries, int table, GuestPtr<int32_t> result)
    {
        return queueRequest(2, result.address, {port, name, mode, maxEntries, table});
    }

    int McQueue_Format(int, int port, GuestPtr<int32_t> result) { return queueRequest(3, result.address, {port}); }
    int McQueue_Mkdir(int, int port, int name) { return queueRequest(4, 0, {port, name}); }

    int McQueue_Chdir(int, int port, int newDir, int oldDir, GuestPtr<int32_t> result)
    {
        return queueRequest(5, result.address, {port, newDir, oldDir});
    }

    int McQueue_Open(int, int port, int name, int mode, GuestPtr<int32_t> result)
    {
        return queueRequest(6, result.address, {port, name, mode});
    }

    int McQueue_Close(int, int fd, GuestPtr<int32_t> result) { return queueRequest(7, result.address, {fd}); }
    int McQueue_Delete(int, int port, int name) { return queueRequest(8, 0, {port, name}); }

    int McQueue_Read(int, int fd, int buffer, int size, GuestPtr<int32_t> result)
    {
        return queueRequest(9, result.address, {fd, buffer, size});
    }

    int McQueue_Write(int, int fd, int buffer, int size, GuestPtr<int32_t> result)
    {
        return queueRequest(10, result.address, {fd, buffer, size});
    }

    int McQueue_Seek(int, int fd, int offset, int mode) { return queueRequest(12, 0, {fd, offset, mode}); }

    PORT_FN(McQueue_GetInfo);
    PORT_FN(McQueue_GetDir);
    PORT_FN(McQueue_Format);
    PORT_FN(McQueue_Mkdir);
    PORT_FN(McQueue_Chdir);
    PORT_FN(McQueue_Open);
    PORT_FN(McQueue_Close);
    PORT_FN(McQueue_Delete);
    PORT_FN(McQueue_Read);
    PORT_FN(McQueue_Write);
    PORT_FN(McQueue_Seek);
}
