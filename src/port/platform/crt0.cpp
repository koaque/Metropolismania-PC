// The platform layer, program start: what the SDK's crt0 does before main (clears the BSS, sets up the main thread and the heap,
// starts the kernel library, enables interrupts), written from what it does and checked against the recompiled functions (plan P7).
// Prototypes are in config/platform_signatures.txt. The kernel library's start-up is a series of system calls the runtime
// answers itself, with tables from the game's data: they are made in the original's order with the original's arguments, and
// the helpers the runtime calls back as syscall handlers (kCopy, kFindAddress) are here too.

#include "game_functions.h"
#include "sdk.h"

#include <cstring>

namespace mm
{
    namespace
    {
        // System call numbers (the number the SDK's wrapper puts in v1).
        constexpr uint32_t kSysExit = 4;
        constexpr uint32_t kSysSetupThread = 0x3C;
        constexpr uint32_t kSysSetupHeap = 0x3D;
        constexpr uint32_t kSysKCopy = 0x5A;
        constexpr uint32_t kSysFindAddress = 0x5B; // the SDK calls it RFU091
        constexpr uint32_t kSysSetSyscall = 0x74;
        constexpr uint32_t kSysGetSyscall = 0x83;

        constexpr uint32_t kBssStart = 0x001BDA00u;
        constexpr uint32_t kBssEnd = 0x00718280u;       // also where the heap starts
        constexpr uint32_t kGlobalPointer = 0x001C5970u;
        constexpr uint32_t kThreadArguments = 0x001BDE80u; // argc, then argv
        constexpr uint32_t kThreadRootFunction = 0x001000C0u; // ExitThread: where a thread that returns ends up
        constexpr uint32_t kStackSize = 0x100000u;

        constexpr uint32_t kInitAlarm = 0x0010EAC8u; // runtime handlers, reached through their guest wrappers
        constexpr uint32_t kInitThread = 0x0010EC78u;
        constexpr uint32_t kSetOsdConfigParam = 0x0010DE00u;
        constexpr uint32_t kGetOsdConfigParam = 0x0010DE10u;

        constexpr uint32_t kSemaphoreIds = 0x001AC494u;   // two ids, set by supplement_crt0
        constexpr uint32_t kSyscallTable = 0x001AC488u;   // {syscall, handler} pairs the kernel library installs
        constexpr uint32_t kExecPatchTable = 0x001ACC48u;
        constexpr uint32_t kExecPatchCode = 0x001AC4A0u;  // 0x7A8 bytes copied into kernel memory
        constexpr uint32_t kTlbTable = 0x001AC1B8u;
        constexpr uint32_t kTlbPatchCode = 0x001ABE48u;   // 0x328 bytes copied into kernel memory

        uint64_t setSyscall(uint32_t index, uint32_t handler) { return port::syscall(kSysSetSyscall, index, handler); }
        uint32_t kernelCopy(uint32_t destination, uint32_t source, uint32_t bytes)
        {
            return static_cast<uint32_t>(port::syscall(kSysKCopy, destination, source, bytes));
        }
        uint32_t findAddress(uint32_t value) { return static_cast<uint32_t>(port::syscall(kSysFindAddress, value)); }
        uint32_t word(uint32_t address) { return port::load<uint32_t>(address); }
    }

    // The kernel's own word copy, which the kernel library installs as the handler of a system call (two copies of it exist in the
    // game; the runtime calls them back): copies whole words, a byte count rounded down to words. Returns 0.
    int sdk_kCopyA(uint32_t destination, uint32_t source, uint32_t bytes)
    {
        for (uint32_t i = 0; i < bytes / 4u; ++i)
            port::store<uint32_t>(destination + 4u * i, port::load<uint32_t>(source + 4u * i));
        return 0;
    }
    int sdk_kCopyB(uint32_t destination, uint32_t source, uint32_t bytes) { return sdk_kCopyA(destination, source, bytes); }

    // Another handler the runtime calls back: the address of the first word of kernel memory (from 0x80000000) that holds `value`.
    uint32_t sdk_kFindAddress(uint32_t value)
    {
        uint32_t at = 0x80000000u;
        while (port::load<uint32_t>(at) != value)
        {
            at += 4u;
            port::checkpoint();
        }
        return at;
    }

    // Two semaphores of one token each, made for the C library; their ids are kept at 0x1AC494 and 0x1AC498. The parameter blocks
    // are in the function's frame (maximum at +4 and +0x24, initial count at +8 and +0x28; the rest is what the stack held).
    void sdk_supplement_crt0()
    {
        port::GuestFrame<0x50> frame;
        const uint32_t sp = frame.ptr().address;
        port::store<int32_t>(sp + 0x28u, 1);
        port::store<int32_t>(sp + 4u, 1);
        port::store<int32_t>(sp + 8u, 1);
        port::store<int32_t>(sp + 0x24u, 1);
        port::store<int32_t>(kSemaphoreIds, sdk::CreateSema(sp));
        port::store<int32_t>(kSemaphoreIds + 4u, sdk::CreateSema(sp + 0x20u));
    }

    // Installs the first syscall override from the kernel table and asks for the address of kFindAddress's syscall entry.
    int sdk_GetSystemCallTableEntry()
    {
        port::GuestFrame<0x10> frame;
        setSyscall(word(kSyscallTable), word(kSyscallTable + 4u));
        const uint32_t entry = static_cast<uint32_t>(port::syscall(kSysGetSyscall, addr::sdk_kFindAddress));
        return static_cast<int>(entry) - 0x20C;
    }

    // Whether the console's configuration cannot hold the extended language setting (an old BIOS needs the kernel patch): sets the
    // field at bits 13 to 15 of the configuration word to 1, reads it back and puts the original back.
    int sdk_PatchIsNeeded()
    {
        port::GuestFrame<0x30> frame;
        const uint32_t sp = frame.ptr().address;
        port::callGuest<int>(kGetOsdConfigParam, sp);
        port::store<uint32_t>(sp + 4u, (port::load<uint32_t>(sp) & 0xFFFF1FFFu) | 0x2000u);
        port::callGuest<int>(kSetOsdConfigParam, sp + 4u);
        port::callGuest<int>(kGetOsdConfigParam, sp + 4u);
        port::callGuest<int>(kSetOsdConfigParam, sp);
        return ((port::load<uint32_t>(sp + 4u) >> 13) & 7u) < 1u ? 1 : 0;
    }

    // The kernel patch for old BIOS versions: only when PatchIsNeeded says so. Two syscall overrides, the patch code copied into
    // kernel memory (at 0x80074000), the caches flushed, two more overrides, then one override whose handler the kernel is asked for.
    void sdk_InitExecPS2()
    {
        port::GuestFrame<0x40> frame;
        if (!sdk_PatchIsNeeded())
            return;
        setSyscall(word(kExecPatchTable), word(kExecPatchTable + 4u));
        kernelCopy(0x80074000u, kExecPatchCode, 0x7A8u);
        sdk::FlushCache(0);
        sdk::FlushCache(2);
        setSyscall(word(kExecPatchTable + 8u), word(kExecPatchTable + 0xCu));
        const uint32_t index = word(kExecPatchTable + 0x10u);
        setSyscall(index, findAddress(index));
    }

    // The kernel library's TLB functions: three syscall overrides around a copy of patch code into kernel memory (at 0x80075000),
    // then five overrides whose handlers the kernel is asked for.
    void sdk_InitTLBFunctions()
    {
        port::GuestFrame<0x40> frame;
        setSyscall(word(kTlbTable), word(kTlbTable + 4u));
        kernelCopy(0x80075000u, kTlbPatchCode, 0x328u);
        sdk::FlushCache(0);
        sdk::FlushCache(2);
        setSyscall(word(kTlbTable + 8u), word(kTlbTable + 0xCu));
        setSyscall(word(kTlbTable + 0x10u), word(kTlbTable + 0x14u));
        for (uint32_t entry = kTlbTable + 0x18u, i = 3; i < 8; ++i, entry += 8u)
            setSyscall(word(entry), findAddress(word(entry)));
    }

    // The kernel library's start-up.
    void sdk_InitSys()
    {
        port::GuestFrame<0x10> frame;
        sdk_supplement_crt0();
        sdk_GetSystemCallTableEntry();
        port::callGuest<int>(kInitAlarm);
        port::callGuest<int>(kInitThread);
        sdk_InitExecPS2();
        sdk_InitTLBFunctions();
    }

    // The program's entry point, the root of the main thread. The BSS is cleared, the main thread gets its stack (the top of a 1 MB stack at the end of RAM) and the heap starts where the BSS
    // ends, the kernel library starts, the caches are flushed, interrupts are enabled and main runs with argc and argv from the
    // thread's argument block. A return from main ends the program through the SDK's exit.
    void sdk_start()
    {
        R5900Context *ctx = port::g_call.ctx;
        // The loop stores a quadword and then tests the address against the end, so it clears one quadword more than the BSS (the first
        // 16 bytes of the heap), and takes a back branch (32 cycles) for every store but the last.
        const uint32_t clearBytes = kBssEnd - kBssStart + 16u;
        std::memset(port::host(kBssStart), 0, clearBytes);
        port::accountCycles((clearBytes / 16u - 1u) * 32u);
        port::setGpr64(ctx, 28, kGlobalPointer);
        port::setGpr64(ctx, 29, port::syscall(kSysSetupThread, kGlobalPointer, ~0ull, kStackSize, kThreadArguments, kThreadRootFunction));
        port::syscall(kSysSetupHeap, kBssEnd, ~0ull);
        sdk_InitSys();
        sdk::FlushCache(0);
        ctx->cop0_status |= 0x10000; // ei
        const int status = Main();
        port::callGuest<void>(0x00116C30u, status); // the SDK's exit: leaves the program
    }

    PORT_FN(sdk_kCopyA);
    PORT_FN(sdk_kCopyB);
    PORT_FN(sdk_kFindAddress);
    PORT_FN(sdk_supplement_crt0);
    PORT_FN(sdk_GetSystemCallTableEntry);
    PORT_FN(sdk_PatchIsNeeded);
    PORT_FN(sdk_InitExecPS2);
    PORT_FN(sdk_InitTLBFunctions);
    PORT_FN(sdk_InitSys);
    PORT_FN(sdk_start);
}
