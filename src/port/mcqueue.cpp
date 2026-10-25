// The memory card request queue: 64 slots of 0x24 bytes, filled at the tail and issued one at a time by
// mm_McQueue_Pump (sceMcOpen ... sceMcSeek). Each slot: +0 command, +4 state, +0xC where to put the result
// (0: the slot's own word at +0xC), arguments from +0x10. The queue functions return the new tail.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    // Starts the memory card library and clears the first word of each of the 64 request slots (0x24 bytes apart).
    void McQueue_Init()
    {
        sdk::sceMcInit();
        for (uint32_t slot = 0; slot < 64u; ++slot)
            port::store<uint32_t>(0x001F5CF0u + 0x24u * slot, 0u);
        port::store<uint32_t>(0x001BDCD4u, 0u);
        port::store<uint32_t>(0x001BDCD8u, 0u);
    }

    PORT_FN(McQueue_Init);

    // Issues the request at the head of the queue (once: the call is accepted when the SDK returns 0, which sets the slot's
    // state to 1) and, while it is in flight, polls sceMcSync for its end; on the end the slot is freed and the head moves on.
    void McQueue_Pump()
    {
        const int32_t head = globals::mcQueueHead.get();
        if (head == globals::mcQueueTail.get())
            return;
        const uint32_t slot = globals::kMcRequests + 0x24u * static_cast<uint32_t>(head);
        const int32_t command = port::load<int32_t>(slot);
        if (command == 0)
            return;
        const auto arg = [&](uint32_t index) { return port::load<int32_t>(slot + 0x10u + 4u * index); };
        if (port::load<int32_t>(slot + 4u) == 0)
        {
            int64_t result = -1;
            switch (static_cast<uint32_t>(command))
            {
            case 1:
                result = sdk::sceMcGetInfo(arg(0), 0, arg(1), arg(2), arg(3));
                break;
            case 2:
                result = sdk::sceMcGetDir(arg(0), 0, arg(1), arg(2), arg(3), arg(4));
                break;
            case 3:
                result = sdk::sceMcFormat(arg(0), 0);
                break;
            case 4:
                result = sdk::sceMcMkdir(arg(0), 0, arg(1));
                break;
            case 5:
                result = sdk::sceMcChdir(arg(0), 0, arg(1), arg(2));
                break;
            case 6:
                result = sdk::sceMcOpen(arg(0), 0, arg(1), arg(2));
                break;
            case 7:
                result = sdk::sceMcClose(arg(0));
                break;
            case 8:
                result = sdk::sceMcDelete(arg(0), 0, arg(1));
                break;
            case 9:
                result = sdk::sceMcRead(arg(0), arg(1), arg(2));
                break;
            case 10:
                result = sdk::sceMcWrite(arg(0), arg(1), arg(2));
                break;
            case 11:
                result = sdk::sceMcUnformat(arg(0), 0);
                break;
            case 12:
                result = sdk::sceMcSeek(arg(0), arg(1), arg(2));
                break;
            default:
                break;
            }
            if (result == 0)
                port::store<int32_t>(slot + 4u, 1);
            return;
        }
        uint32_t resultAt = port::load<uint32_t>(slot + 0xCu);
        if (resultAt == 0)
            resultAt = slot + 0xCu;
        if (sdk::sceMcSync(1, slot + 8u, resultAt) != 0)
        {
            port::store<int32_t>(slot + 4u, 0);
            port::store<int32_t>(slot, 0);
            globals::mcQueueHead.set(globals::mcQueueHead.get() + 1);
            if (!(globals::mcQueueHead.get() < 0x40))
                globals::mcQueueHead.set(0);
        }
    }

    PORT_FN(McQueue_Pump);
}
