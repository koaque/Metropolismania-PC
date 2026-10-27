// The platform layer, the sound library's remote calls to the IOP's sound driver (plan P7), written from what they do and proven
// against the recompiled functions by the difftest. Prototypes are in config/platform_signatures.txt.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kArguments = 0x001C3500u;    // the argument buffer: word 0 holds its own address, words 1 to 6 the call's arguments
        constexpr uint32_t kClient = 0x001C3540u;       // the RPC client bound to the sound driver's server
        constexpr uint32_t kEndFunction = 0x001ACC70u;  // what an asynchronous call (mode 0) runs when the RPC completes
        constexpr uint32_t kServer = 0x80000701u;       // the sound driver's RPC number
        constexpr uint32_t kBindFailed = 0x001B45E0u;   // the message printed when the bind is refused

        // The library keeps what the last calls of two commands set (0x8160 and 0x8170), for its own later use.
        constexpr uint32_t kCoreCommandA[2] = {0x001ACC84u, 0x001ACC90u}; // [0]: argument 1, [1]: argument 2 (first argument 0)
        constexpr uint32_t kCoreCommandB[2] = {0x001ACC88u, 0x001ACC94u}; // the same when the first argument is not 0
        constexpr uint32_t kQueryA = 0x001ACC8Cu;
        constexpr uint32_t kQueryB = 0x001ACC98u;
    }

    // Binds the RPC client to the sound driver: until the server is there, the bind is repeated after a pause of 0x2711 turns (which
    // charges the cycles the IOP needs to start the server). A refused bind prints a message and gives -1.
    int sdk_sceSdRemoteInit()
    {
        port::GuestFrame<0x40> frame;
        sdk::sceSifInitRpc(0);
        for (;;)
        {
            if (sdk::sceSifBindRpc(kClient, static_cast<int>(kServer), 0) < 0)
            {
                sdk::printf(kBindFailed);
                return -1;
            }
            port::accountCycles(0x2710u * 32u);
            if (port::load<int32_t>(kClient + 0x24u) != 0)
                break;
            port::checkpoint();
        }
        sdk::FlushCache(0);
        return 0;
    }

    // A remote call. `mode` 0 makes the RPC asynchronous (with the end function at 0x1ACC70), any other value waits for it. The six
    // arguments are copied into the argument buffer; most commands send the buffer (or the area its first two arguments name, for the
    // commands from 0x9000 to 0x90F0) and read the answer back into it, and the call gives the buffer's first word, which holds the
    // server's answer. 0x8130 adds the first argument to the command and sends the area the second argument names; 0x8140 sends the
    // buffer and receives into the area the second argument names, and gives whatever the caller's s1 held. 0x8160 and 0x8170 also
    // note their arguments for the library.
    int64_t sdk_sceSdRemote(int mode, int command, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5)
    {
        port::GuestFrame<0xD0> frame;
        const uint64_t callerS1 = port::gpr64(port::g_call.ctx, 17);
        const uint32_t arguments[6] = {arg0, arg1, arg2, arg3, arg4, arg5};
        port::store<uint32_t>(kArguments, kArguments);
        for (uint32_t i = 0; i < 6; ++i)
            port::store<uint32_t>(kArguments + 4u + 4u * i, arguments[i]);
        const bool asynchronous = mode == 0;
        const uint32_t endFunction = asynchronous ? port::load<uint32_t>(kEndFunction) : 0u;
        const uint32_t cmd = static_cast<uint32_t>(command);
        if (cmd == 0x8160u)
        {
            const uint32_t *slots = arg0 == 0 ? kCoreCommandA : kCoreCommandB;
            port::store<uint32_t>(slots[1], arg2);
            port::store<uint32_t>(slots[0], arg1);
        }
        else if (cmd == 0x8170u)
        {
            port::store<uint32_t>(kQueryB, arg1);
            port::store<uint32_t>(kQueryA, arg0);
        }
        if (cmd == 0x8130u)
            sdk::sceSifCallRpc(kClient, static_cast<int>(arg0 | 0x8130u), asynchronous, arg1, 0x40, 0, 0, endFunction, kArguments);
        else if (cmd == 0x8140u)
        {
            sdk::sceSifCallRpc(kClient, static_cast<int>(arg0 | 0x8140u), asynchronous, kArguments, 0x40, arg1, 0x40, endFunction, arg1);
            return static_cast<int64_t>(callerS1);
        }
        else if (cmd - 0x9000u <= 0xF0u)
            sdk::sceSifCallRpc(kClient, command, asynchronous, arg0, static_cast<int>(arg1), kArguments, 0x10, endFunction, kArguments);
        else
            sdk::sceSifCallRpc(kClient, command, asynchronous, kArguments, 0x40, kArguments, 0x10, endFunction, kArguments);
        return port::load<int32_t>(kArguments);
    }

    PORT_FN(sdk_sceSdRemoteInit);
    PORT_FN(sdk_sceSdRemote);
}
