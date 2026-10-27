// The platform layer, the SIF library's IOP heap calls (plan P7), written from what they do and proven against the recompiled
// functions by the difftest. Prototypes are in config/platform_signatures.txt.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kHeapBound = 0x001ABE38u;   // negative until the IOP heap's RPC server is bound
        constexpr uint32_t kHeapClient = 0x001C1D00u;
        constexpr uint32_t kHeapSend = 0x001C1D80u;
        constexpr uint32_t kHeapReceive = 0x001C1D40u;
        constexpr int kFreeHeapFunction = 2;
    }

    // Frees a block of the IOP heap through its RPC server: 0 when the server is not bound, -1 when the call fails, else what the
    // server answered.
    int sdk_sceSifFreeIopHeap(uint32_t address)
    {
        port::GuestFrame<0x30> frame;
        if (port::load<int32_t>(kHeapBound) < 0)
            return 0;
        port::store<uint32_t>(kHeapSend, address);
        if (sdk::sceSifCallRpc(kHeapClient, kFreeHeapFunction, 0, kHeapSend, 4, kHeapReceive, 4, 0, 0) < 0)
            return -1;
        return port::load<int32_t>(kHeapReceive);
    }

    // Frees IOP system memory: the same call.
    int sdk_sceSifFreeSysMemory(uint32_t block) { return sdk_sceSifFreeIopHeap(block); }

    PORT_FN(sdk_sceSifFreeIopHeap);
    PORT_FN(sdk_sceSifFreeSysMemory);
}
