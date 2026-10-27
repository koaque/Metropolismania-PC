// Sony SDK routines that game code calls (libc, libm, libvu0, the kernel and the I/O libraries). The ones written natively in
// src/port/platform/ (plan P7) are called directly by these wrappers; the others are handlers of the runtime library, which ported
// code reaches through port::callGuest at the original function's address. Addresses are for SLUS_203.77 (AGENTS.md rule 4);
// names come from the SDK signature scan.

#pragma once

#include "abi.h"

#include <functional>
#include <type_traits>
#include "game_functions.h" // the platform layer's functions (sdk_<name>, config/platform_signatures.txt)

namespace mm::sdk
{
    // Calls the native platform function `Native` (at `Address` in the game), or, when MMPC_SDK_ORIGINAL names it, the recompiled
    // function it replaces: a way to compare the two in a live run when a native function is suspected.
    template <auto Native, uint32_t Address, typename... Args>
    inline auto route(Args... args)
    {
        using R = std::invoke_result_t<decltype(Native), Args...>;
        if (port::sdkOriginalRequested(Address))
            return port::callOriginal<R>(Address, args...);
        return Native(args...);
    }

    namespace addr
    {
        inline constexpr uint32_t abs = 0x0011E810u;
        inline constexpr uint32_t FlushCache = 0x0010DFE0u;
        inline constexpr uint32_t sceGifPkInit = 0x00104838u;
        inline constexpr uint32_t sceGifPkReset = 0x00104848u;
        inline constexpr uint32_t sceGifPkTerminate = 0x00104858u;
        inline constexpr uint32_t sceGifPkCnt = 0x001048B0u;
        inline constexpr uint32_t sceGifPkRef = 0x00104928u;
        inline constexpr uint32_t sceGifPkEnd = 0x001049C0u;
        inline constexpr uint32_t sceGifPkReserve = 0x00104A38u;
        inline constexpr uint32_t sceGifPkOpenGifTag = 0x00104A50u;
        inline constexpr uint32_t sceGifPkCloseGifTag = 0x00104A68u;
        inline constexpr uint32_t sceGifPkAddGsAD = 0x00104B10u;
        inline constexpr uint32_t DIntr = 0x00116860u;
        inline constexpr uint32_t EIntr = 0x001168A8u;
        inline constexpr uint32_t CreateSema = 0x0010DD60u;
        inline constexpr uint32_t DeleteSema = 0x0010DD70u;
        inline constexpr uint32_t sceSifDmaStat = 0x0010E100u;
        inline constexpr uint32_t sceSifSetDma = 0x0010E120u;
        inline constexpr uint32_t sceMpegAddStrCallback = 0x0010B450u;
        inline constexpr uint32_t sceMpegDelete = 0x0010BF60u;
        inline constexpr uint32_t acosf = 0x0011A2A8u;
        inline constexpr uint32_t atan2f = 0x0011A2C0u;
        inline constexpr uint32_t fmodf = 0x0011A2D8u;
        inline constexpr uint32_t fptosi = 0x00127828u;
        inline constexpr uint32_t fptoui = 0x00127918u;
        inline constexpr uint32_t fptodp = 0x001278D8u;
        inline constexpr uint32_t fabs = 0x00119990u;
        inline constexpr uint32_t fpLtdf2 = 0x001000D0u;
        inline constexpr uint32_t dpadd = 0x00127D90u;
        inline constexpr uint32_t dpmul = 0x00127E50u;
        inline constexpr uint32_t dptofp = 0x00128510u;
        inline constexpr uint32_t cosf = 0x00119F08u;
        inline constexpr uint32_t sinf = 0x0011A1A8u;
        inline constexpr uint32_t sqrtf = 0x0011A2F0u;
        inline constexpr uint32_t memcpy = 0x00121B90u;
        inline constexpr uint32_t memmove = 0x00121C40u;
        inline constexpr uint32_t memset = 0x00121D48u;
        inline constexpr uint32_t atanf = 0x00119C38u;
        inline constexpr uint32_t malloc = 0x00120FC0u;
        inline constexpr uint32_t free = 0x00121018u;
        inline constexpr uint32_t operator_delete = 0x00100420u;
        inline constexpr uint32_t sprintf = 0x00123460u;
        inline constexpr uint32_t sceOpen = 0x00111F18u;
        inline constexpr uint32_t sceClose = 0x001121A0u;
        inline constexpr uint32_t sceLseek = 0x00112320u;
        inline constexpr uint32_t sceRead = 0x00112558u;
        inline constexpr uint32_t sceIoctl = 0x00112A88u;
        inline constexpr uint32_t sceCdDiskReady = 0x0011B360u;
        inline constexpr uint32_t strcat = 0x00123660u;
        inline constexpr uint32_t strcmp = 0x00123920u;
        inline constexpr uint32_t strcpy = 0x00123A68u;
        inline constexpr uint32_t strchr = 0x00123790u;
        inline constexpr uint32_t operator_new = 0x001005B0u;
        inline constexpr uint32_t mw_NewArray = 0x001004C0u;
        inline constexpr uint32_t mw_DeleteArray = 0x00100400u;
        inline constexpr uint32_t construct_new_array = 0x001002B0u;
        inline constexpr uint32_t sceGsSyncPath = 0x00103418u;
        inline constexpr uint32_t sceMcInit = 0x0011CF90u;
        inline constexpr uint32_t memalign = 0x00120D90u;
        inline constexpr uint32_t sceDmaGetChan = 0x00103DA0u;
        inline constexpr uint32_t sceGsSetDefDrawEnv = 0x00102D10u;
        inline constexpr uint32_t sceGsPutDispEnv = 0x00103000u;
        inline constexpr uint32_t sceGsSetDefDBuff = 0x001030E8u;
        inline constexpr uint32_t sceSifInitRpc = 0x001106C0u;
        inline constexpr uint32_t sceScfGetLocalTimefromRTC = 0x001A3C10u;
        inline constexpr uint32_t sceSifBindRpc = 0x00110D30u;
        inline constexpr uint32_t sceSifCheckStatRpc = 0x001110F0u;
        inline constexpr uint32_t sceSifCallRpc = 0x00110F00u;
        inline constexpr uint32_t printf = 0x00122F50u;
        inline constexpr uint32_t strcspn = 0x00123B80u;
        inline constexpr uint32_t strlen = 0x00123C00u;
        inline constexpr uint32_t strncmp = 0x00123D38u;
        inline constexpr uint32_t d_utod = 0x001000F0u;
        inline constexpr uint32_t pow = 0x0011A290u;
        inline constexpr uint32_t dptoli = 0x00128440u;
        inline constexpr uint32_t sceVu0ApplyMatrix = 0x00104B38u;
        inline constexpr uint32_t sceVu0MulMatrix = 0x00104B68u;
        inline constexpr uint32_t sceVu0UnitMatrix = 0x00104E60u;
        inline constexpr uint32_t sceVu0InversMatrix = 0x00104C80u;
        inline constexpr uint32_t sceVu0RotMatrixZ = 0x00104F00u;
        inline constexpr uint32_t sceVu0RotMatrixY = 0x00105050u;
        inline constexpr uint32_t sceVu0RotMatrixX = 0x00104FA8u;
        inline constexpr uint32_t sceVu0CameraMatrix = 0x00105170u;
        inline constexpr uint32_t sceVu0NormalLightMatrix = 0x00105220u;
        inline constexpr uint32_t sceVu0LightColorMatrix = 0x001052E0u;
        inline constexpr uint32_t scePadInit = 0x0011BD10u;
        inline constexpr uint32_t scePadPortOpen = 0x0011BF70u;
        inline constexpr uint32_t scePadRead = 0x0011C2C0u;
        inline constexpr uint32_t scePadGetState = 0x0011C340u;
        inline constexpr uint32_t scePadGetReqState = 0x0011C458u;
        inline constexpr uint32_t scePadInfoMode = 0x0011C720u;
        inline constexpr uint32_t scePadSetMainMode = 0x0011C858u;
        inline constexpr uint32_t scePadSetActAlign = 0x0011C9D0u;
        inline constexpr uint32_t scePadInfoPressMode = 0x0011CC10u;
        inline constexpr uint32_t scePadEnterPressMode = 0x0011CC70u;
        inline constexpr uint32_t WaitSema = 0x0010DDA0u;
        inline constexpr uint32_t SignalSema = 0x0010DD80u;
        inline constexpr uint32_t RotateThreadReadyQueue = 0x0010DC10u;
        inline constexpr uint32_t GetThreadId = 0x0010DC50u;
        inline constexpr uint32_t ChangeThreadPriority = 0x0010DBF0u;
        inline constexpr uint32_t CreateThread = 0x0010DB60u;
        inline constexpr uint32_t StartThread = 0x0010DB80u;
        inline constexpr uint32_t TerminateThread = 0x0010DBB0u;
        inline constexpr uint32_t DeleteThread = 0x0010DB70u;
        inline constexpr uint32_t AddIntcHandler = 0x0010DA40u;
        inline constexpr uint32_t RemoveIntcHandler = 0x0010DA60u;
        inline constexpr uint32_t AddDmacHandler = 0x0010DA70u;
        inline constexpr uint32_t RemoveDmacHandler = 0x0010DA90u;
        inline constexpr uint32_t EnableIntc = 0x0010E8A8u;
        inline constexpr uint32_t DisableIntc = 0x0010E840u;
        inline constexpr uint32_t EnableDmac = 0x0010E978u;
        inline constexpr uint32_t DisableDmac = 0x0010E910u;
        inline constexpr uint32_t sceMpegInit = 0x0010BCB8u;
        inline constexpr uint32_t sceMpegDemuxPssRing = 0x0010B138u;
        inline constexpr uint32_t sceMpegGetPicture = 0x0010BF90u;
        inline constexpr uint32_t sceMpegIsEnd = 0x0010C0A8u;
        inline constexpr uint32_t sceMpegReset = 0x0010C0C8u;
        inline constexpr uint32_t sceSdRemoteInit = 0x00116C90u;
        inline constexpr uint32_t sceGsSetDefDBuffDc = 0x00103A20u;
        inline constexpr uint32_t sceGsSetHalfOffset = 0x00103730u;
        inline constexpr uint32_t sceGsSetHalfOffset2 = 0x00103998u;
        inline constexpr uint32_t sceGsSwapDBuffDc = 0x00103D08u;
        inline constexpr uint32_t sceSdRemote = 0x00116DD0u;
        inline constexpr uint32_t sceSifAllocSysMemory = 0x00114DB0u;
        inline constexpr uint32_t sceSifFreeSysMemory = 0x00114EA8u;
        inline constexpr uint32_t sceSifAllocIopHeap = 0x00114D40u;
        inline constexpr uint32_t sceGsSyncV = 0x00103380u;
        inline constexpr uint32_t sceCdStStop = 0x0011B7E0u;
        inline constexpr uint32_t sceCdStRead = 0x0011B818u;
        inline constexpr uint32_t sceDmaSend = 0x001040F0u;
        inline constexpr uint32_t mw_register_global_object = 0x001009E0u;
        inline constexpr uint32_t sceCdReadClock = 0x0011B620u;
        inline constexpr uint32_t sceSifLoadModule = 0x001159B8u;
        inline constexpr uint32_t sceMcOpen = 0x0011D330u;
        inline constexpr uint32_t sceMcMkdir = 0x0011D468u;
        inline constexpr uint32_t sceMcClose = 0x0011D4A0u;
        inline constexpr uint32_t sceMcSeek = 0x0011D558u;
        inline constexpr uint32_t sceMcRead = 0x0011D6C0u;
        inline constexpr uint32_t sceMcWrite = 0x0011D7E0u;
        inline constexpr uint32_t sceMcSync = 0x0011D9D0u;
        inline constexpr uint32_t sceMcGetInfo = 0x0011DB08u;
        inline constexpr uint32_t sceMcGetDir = 0x0011DD00u;
        inline constexpr uint32_t sceMcChdir = 0x0011DEE0u;
        inline constexpr uint32_t sceMcFormat = 0x0011E030u;
        inline constexpr uint32_t sceMcDelete = 0x0011E0F8u;
        inline constexpr uint32_t sceMcUnformat = 0x0011E618u;
        inline constexpr uint32_t index = 0x00120AD8u;
        inline constexpr uint32_t strncpy = 0x00123EF0u;
        inline constexpr uint32_t toupper = 0x001240B0u;
        inline constexpr uint32_t sceCdInit = 0x0011B080u;
        inline constexpr uint32_t sceCdMmode = 0x0011B558u;
        inline constexpr uint32_t sceCdStInit = 0x0011B718u;
        inline constexpr uint32_t sceCdStStart = 0x0011B748u;
        inline constexpr uint32_t sceCdSearchFile = 0x0011A8F8u;
        inline constexpr uint32_t sceMpegCreate = 0x0010BD58u;
        inline constexpr uint32_t sceMpegAddCallback = 0x0010C170u;
        inline constexpr uint32_t sceMpegIsRefBuffEmpty = 0x0010C0B8u;
        inline constexpr uint32_t sceSifRebootIop = 0x00115EB8u;
        inline constexpr uint32_t sceSifSyncIop = 0x00115E80u;
        inline constexpr uint32_t sceSifInitIopHeap = 0x00114CB8u;
        inline constexpr uint32_t sceFsReset = 0x00111EE0u;
        inline constexpr uint32_t sceGsResetPath = 0x001028B0u;
        inline constexpr uint32_t sceDmaReset = 0x00103DC8u;
        inline constexpr uint32_t sceGsResetGraph = 0x00102710u;
        inline constexpr uint32_t crt0_InitCpp = 0x00100150u;
        inline constexpr uint32_t destroy_global_chain = 0x00100140u;
    }

    // libm.
    inline ps2f acosf(ps2f x) { return route<&sdk_acosf, mm::addr::sdk_acosf>(x); }
    inline ps2f atan2f(ps2f y, ps2f x) { return route<&sdk_atan2f, mm::addr::sdk_atan2f>(y, x); }
    inline ps2f fmodf(ps2f x, ps2f y) { return route<&sdk_fmodf, mm::addr::sdk_fmodf>(x, y); }
    inline ps2f cosf(ps2f x) { return route<&sdk_cosf, mm::addr::sdk_cosf>(x); }
    inline ps2f sinf(ps2f x) { return route<&sdk_sinf, mm::addr::sdk_sinf>(x); }
    inline ps2f sqrtf(ps2f x) { return route<&sdk_sqrtf, mm::addr::sdk_sqrtf>(x); }

    // Float to int conversion helper of the compiler runtime.
    inline int fptosi(ps2f value) { return route<&sdk_fptosi, mm::addr::sdk_fptosi>(value); }
    // Doubles are soft-float, each in one 64-bit register: unsigned to double, pow, double to int.
    inline uint64_t d_utod(uint32_t value) { return route<&sdk_d_utod, mm::addr::sdk_d_utod>(value); }
    inline uint64_t pow(uint64_t x, uint64_t y) { return route<&sdk_pow, mm::addr::sdk_pow>(x, y); }
    inline int dptoli(uint64_t value) { return route<&sdk_dptoli, mm::addr::sdk_dptoli>(value); }
    inline uint32_t fptoui(ps2f value) { return static_cast<uint32_t>(route<&sdk_fptoui, mm::addr::sdk_fptoui>(value)); }
    // The compiler runtime's soft doubles: float to double, absolute value, and "less than" (1 or 0).
    inline uint64_t fptodp(ps2f value) { return route<&sdk_fptodp, mm::addr::sdk_fptodp>(value); }
    // The runtime's stand-in for the double fabs (0x119990) reads f12 as a float and leaves v0 alone, so on a soft double it returns the
    // value the conversion before it left in v0: the argument itself, not its absolute value. Kept as it is, so that the game does what
    // it does today (docs/PLATFORM_FINDINGS.md); a correct fabs would clear the sign bit.
    inline uint64_t fabs(uint64_t value) { return value; }
    inline int64_t fpLtdf2(uint64_t a, uint64_t b) { return route<&sdk_fp_ltdf2, mm::addr::sdk_fp_ltdf2>(a, b); }
    // Double addition, multiplication, division and comparison, and double to float (the result in f0).
    inline uint64_t dpadd(uint64_t a, uint64_t b) { return route<&sdk_dpadd, mm::addr::sdk_dpadd>(a, b); }
    inline uint64_t dpmul(uint64_t a, uint64_t b) { return route<&sdk_dpmul, mm::addr::sdk_dpmul>(a, b); }
    inline uint64_t dpdiv(uint64_t a, uint64_t b) { return route<&sdk_dpdiv, mm::addr::sdk_dpdiv>(a, b); }
    inline int dpcmp(uint64_t a, uint64_t b) { return route<&sdk_dpcmp, mm::addr::sdk_dpcmp>(a, b); }
    inline ps2f dptofp(uint64_t value) { return route<&sdk_dptofp, mm::addr::sdk_dptofp>(value); }

    // Writes back and invalidates the caches (0: the data cache), which a DMA out of main memory needs first.
    inline void FlushCache(int mode) { port::callGuest<void>(addr::FlushCache, mode); }

    // The GIF packet builder of libgraph (`pk` is the packet structure's address): the game calls Cnt and End with three zero
    // arguments after the packet, Ref with a count of quadwords and three zeros, and gives OpenGifTag the whole 16-byte tag.
    inline void sceGifPkInit(uint32_t pk, uint32_t address) { route<&sdk_sceGifPkInit, mm::addr::sdk_sceGifPkInit>(GuestPtr<GifPacket>{pk}, GuestPtr<uint32_t>{address}); }
    inline void sceGifPkReset(uint32_t pk) { route<&sdk_sceGifPkReset, mm::addr::sdk_sceGifPkReset>(GuestPtr<GifPacket>{pk}); }
    inline void sceGifPkTerminate(uint32_t pk) { route<&sdk_sceGifPkTerminate, mm::addr::sdk_sceGifPkTerminate>(GuestPtr<GifPacket>{pk}); }
    inline void sceGifPkCnt(uint32_t pk) { route<&sdk_sceGifPkCnt, mm::addr::sdk_sceGifPkCnt>(GuestPtr<GifPacket>{pk}, 0, 0, 0); }
    inline void sceGifPkRef(uint32_t pk, uint32_t address, int qwc) { route<&sdk_sceGifPkRef, mm::addr::sdk_sceGifPkRef>(GuestPtr<GifPacket>{pk}, address, static_cast<uint32_t>(qwc), 0, 0, 0); }
    inline void sceGifPkEnd(uint32_t pk) { route<&sdk_sceGifPkEnd, mm::addr::sdk_sceGifPkEnd>(GuestPtr<GifPacket>{pk}, 0, 0, 0); }
    inline uint32_t sceGifPkReserve(uint32_t pk, int qwc) { return route<&sdk_sceGifPkReserve, mm::addr::sdk_sceGifPkReserve>(GuestPtr<GifPacket>{pk}, qwc).address; }
    inline void sceGifPkOpenGifTag(uint32_t pk, port::Quad tag) { route<&sdk_sceGifPkOpenGifTag, mm::addr::sdk_sceGifPkOpenGifTag>(GuestPtr<GifPacket>{pk}, tag); }
    inline void sceGifPkCloseGifTag(uint32_t pk) { port::callGuest<void>(addr::sceGifPkCloseGifTag, pk); }
    inline void sceGifPkAddGsAD(uint32_t pk, int reg, uint64_t data) { port::callGuest<void>(addr::sceGifPkAddGsAD, pk, reg, data); }

    // Kernel: interrupts off and on, and semaphores (the parameter block's address; the id).
    inline int DIntr() { return route<&sdk_DIntr, mm::addr::sdk_DIntr>(); }
    inline int EIntr() { return route<&sdk_EIntr, mm::addr::sdk_EIntr>(); }
    inline int CreateSema(uint32_t param) { return port::callGuest<int>(addr::CreateSema, param); }
    inline int DeleteSema(int sema) { return port::callGuest<int>(addr::DeleteSema, sema); }
    // The SIF DMA to the IOP: the transfer descriptors' address and count give an id, whose status is negative once done.
    inline uint32_t sceSifSetDma(uint32_t descriptors, int count) { return port::callGuest<uint32_t>(addr::sceSifSetDma, descriptors, count); }
    inline int64_t sceSifDmaStat(uint32_t id) { return port::callGuest<int64_t>(addr::sceSifDmaStat, id); }
    // The MPEG library.
    inline int sceMpegAddStrCallback(int mpeg, int stream, int id, int function, int argument)
    {
        return port::callGuest<int>(addr::sceMpegAddStrCallback, mpeg, stream, id, function, argument);
    }
    inline int sceMpegDelete(int mpeg) { return route<&sdk_sceMpegDelete, mm::addr::sdk_sceMpegDelete>(mpeg); }

    // libc: the guest addresses of the buffers.
    inline uint32_t memcpy(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memcpy, dst, src, size); }
    inline uint32_t memmove(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memmove, dst, src, size); }
    inline uint32_t memset(uint32_t dst, int value, int size) { return port::callGuest<uint32_t>(addr::memset, dst, value, size); }
    inline int strcmp(uint32_t a, uint32_t b) { return port::callGuest<int>(addr::strcmp, a, b); }
    inline int abs(int value) { return route<&sdk_abs, mm::addr::sdk_abs>(value); }
    inline uint32_t strcpy(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcpy, dst, src); }
    inline uint32_t strcat(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcat, dst, src); }
    inline ps2f atanf(ps2f x) { return route<&sdk_atanf, mm::addr::sdk_atanf>(x); }
    inline uint32_t malloc(uint32_t size) { return port::callGuest<uint32_t>(addr::malloc, size); }
    inline void free(uint32_t block) { port::callGuest<void>(addr::free, block); }
    inline void operator_delete(uint32_t object) { route<&sdk_operator_delete, mm::addr::sdk_operator_delete>(GuestPtr<void>{object}); }
    // operator new (0 when the allocation fails is not expected), and the compiler runtime's array new and delete.
    inline uint32_t operator_new(uint32_t size) { return route<&sdk_operator_new, mm::addr::sdk_operator_new>(size).address; }
    inline uint32_t mw_NewArray(uint32_t size) { return route<&sdk_mw_NewArray, mm::addr::sdk_mw_NewArray>(size).address; }
    inline void mw_DeleteArray(uint32_t block) { route<&sdk_mw_DeleteArray, mm::addr::sdk_mw_DeleteArray>(GuestPtr<void>{block}); }
    // Constructs `count` elements of `elementSize` bytes in the block with the constructor at `ctor` (and registers `dtor`).
    inline uint32_t construct_new_array(uint32_t block, uint32_t ctor, uint32_t dtor, uint32_t elementSize, uint32_t count)
    {
        return route<&sdk_construct_new_array, mm::addr::sdk_construct_new_array>(GuestPtr<void>{block}, GuestPtr<void>{ctor}, GuestPtr<void>{dtor}, elementSize, count).address;
    }
    // Waits for the GS paths (mode 0, timeout 0: until the transfers are done).
    inline int sceGsSyncPath(int mode, int timeout) { return port::callGuest<int>(addr::sceGsSyncPath, mode, timeout); }
    // The memory card library's start-up call (the result is not used).
    inline int sceMcInit() { return port::callGuest<int>(addr::sceMcInit); }
    // The console's clock in local time: `clock` is the 8 bytes of status, second, minute, hour, padding, day, month and year (each a BCD
    // byte, the year without its century), which the call converts from the BIOS clock's time zone.
    inline int sceScfGetLocalTimefromRTC(uint32_t clock)
    {
        route<&sdk_sceScfGetLocalTimefromRTC, mm::addr::sdk_sceScfGetLocalTimefromRTC>(GuestPtr<uint8_t>{clock});
        return 0;
    }
    // Starts the RPC layer (mode 0), and binds the client at `client` to the IOP server `number` (negative when the server is not there yet).
    inline void sceSifInitRpc(int mode) { port::callGuest<void>(addr::sceSifInitRpc, mode); }
    inline int sceSifBindRpc(uint32_t client, int number, int mode) { return port::callGuest<int>(addr::sceSifBindRpc, client, number, mode); }
    // libc's aligned allocation, and the channel, display and draw environment calls of libdma and libgraph.
    inline uint32_t memalign(int alignment, int size) { return route<&sdk_memalign, mm::addr::sdk_memalign>(alignment, size).address; }
    inline int sceDmaGetChan(int channel) { return port::callGuest<int>(addr::sceDmaGetChan, channel); }
    inline void sceGsSetDefDrawEnv(uint32_t env, int psm, int width, int height, int ztest, int zpsm)
    {
        port::callGuest<void>(addr::sceGsSetDefDrawEnv, env, psm, width, height, ztest, zpsm);
    }
    inline void sceGsPutDispEnv(uint32_t env) { port::callGuest<void>(addr::sceGsPutDispEnv, env); }
    inline void sceGsSetDefDBuff(uint32_t buffer, int psm, int width, int height, int ztest, int zpsm, int clear)
    {
        port::callGuest<void>(addr::sceGsSetDefDBuff, buffer, psm, width, height, ztest, zpsm, clear);
    }
    // Non-zero while the client's last asynchronous call is still running.
    inline int sceSifCheckStatRpc(uint32_t client) { return port::callGuest<int>(addr::sceSifCheckStatRpc, client); }
    // An RPC to the IOP: client, function number, mode (0 = wait for the reply), send buffer and size, receive buffer and
    // size, end function and its argument.
    inline int sceSifCallRpc(uint32_t client, int number, int mode, uint32_t send, int sendSize, uint32_t receive, int receiveSize,
                             uint32_t endFunction, uint32_t endArgument)
    {
        return port::callGuest<int>(addr::sceSifCallRpc, client, number, mode, send, sendSize, receive, receiveSize, endFunction, endArgument);
    }
    // printf with a format string and integer or pointer arguments (the game's debug messages; the guest addresses of strings).
    template <typename... Args>
    inline int printf(uint32_t format, Args... args)
    {
        return port::callGuest<int>(addr::printf, format, args...);
    }
    // sprintf with integer or pointer arguments after the format (the guest addresses of the buffers).
    template <typename... Args>
    inline int sprintf(uint32_t dst, uint32_t format, Args... args)
    {
        static_assert(sizeof...(Args) <= 8, "sprintf: the native takes eight variadic arguments at most");
        uint64_t slots[8] = {};
        size_t next = 0;
        ((slots[next++] = port::toGuestInt(args)), ...);
        return route<&sdk_sprintf, mm::addr::sdk_sprintf>(GuestPtr<char>{dst}, GuestPtr<char>{format}, slots[0], slots[1], slots[2], slots[3], slots[4], slots[5], slots[6], slots[7]);
    }
    // The kernel's file calls (the runtime's file I/O; descriptors are small non-negative ints, errors are negative).
    inline int sceOpen(uint32_t path, int flags) { return port::callGuest<int>(addr::sceOpen, path, flags); }
    inline int sceClose(int fd) { return port::callGuest<int>(addr::sceClose, fd); }
    inline int sceLseek(int fd, int offset, int whence) { return port::callGuest<int>(addr::sceLseek, fd, offset, whence); }
    inline int sceRead(int fd, uint32_t buffer, int bytes) { return port::callGuest<int>(addr::sceRead, fd, buffer, bytes); }
    inline int sceIoctl(int fd, int command, uint32_t argument) { return port::callGuest<int>(addr::sceIoctl, fd, command, argument); }
    inline int64_t sceCdDiskReady(int mode) { return port::callGuest<int64_t>(addr::sceCdDiskReady, mode); }
    inline int strlen(uint32_t s) { return port::callGuest<int>(addr::strlen, s); }
    inline int strncmp(uint32_t a, uint32_t b, int count) { return port::callGuest<int>(addr::strncmp, a, b, count); }
    inline int strcspn(uint32_t s, uint32_t reject) { return static_cast<int>(route<&sdk_strcspn, mm::addr::sdk_strcspn>(GuestPtr<char>{s}, GuestPtr<char>{reject})); }

    // Kernel calls through the SDK's syscall stubs: semaphores (the id the game keeps), and a yield of the thread's priority level.
    inline int WaitSema(int sema) { return port::callGuest<int>(addr::WaitSema, sema); }
    inline int SignalSema(int sema) { return port::callGuest<int>(addr::SignalSema, sema); }
    inline int RotateThreadReadyQueue(int priority) { return port::callGuest<int>(addr::RotateThreadReadyQueue, priority); }
    // Threads and interrupt handlers (the movie player makes and removes its own): the ids are the kernel's.
    inline int GetThreadId() { return port::callGuest<int>(addr::GetThreadId); }
    inline int ChangeThreadPriority(int thread, int priority) { return port::callGuest<int>(addr::ChangeThreadPriority, thread, priority); }
    inline int CreateThread(uint32_t param) { return port::callGuest<int>(addr::CreateThread, param); }
    inline int StartThread(int thread, int argument) { return port::callGuest<int>(addr::StartThread, thread, argument); }
    inline int TerminateThread(int thread) { return port::callGuest<int>(addr::TerminateThread, thread); }
    inline int DeleteThread(int thread) { return port::callGuest<int>(addr::DeleteThread, thread); }
    inline int AddIntcHandler(int cause, uint32_t handler, int next) { return port::callGuest<int>(addr::AddIntcHandler, cause, handler, next); }
    inline int RemoveIntcHandler(int cause, int id) { return port::callGuest<int>(addr::RemoveIntcHandler, cause, id); }
    inline int AddDmacHandler(int channel, uint32_t handler, int next) { return port::callGuest<int>(addr::AddDmacHandler, channel, handler, next); }
    inline int RemoveDmacHandler(int channel, int id) { return port::callGuest<int>(addr::RemoveDmacHandler, channel, id); }
    inline int EnableIntc(int cause) { return route<&sdk_EnableIntc, mm::addr::sdk_EnableIntc>(cause); }
    inline int DisableIntc(int cause) { return route<&sdk_DisableIntc, mm::addr::sdk_DisableIntc>(cause); }
    inline int EnableDmac(int channel) { return route<&sdk_EnableDmac, mm::addr::sdk_EnableDmac>(channel); }
    inline int DisableDmac(int channel) { return route<&sdk_DisableDmac, mm::addr::sdk_DisableDmac>(channel); }
    // The MPEG library: set-up, demux of a PSS buffer into the stream callbacks (the ring is the one the data lies in), decoding of one
    // picture, the end test and the reset.
    inline int sceMpegInit() { return port::callGuest<int>(addr::sceMpegInit); }
    inline int sceMpegDemuxPssRing(int mpeg, int data, int size, int ring, int ringSize) { return port::callGuest<int>(addr::sceMpegDemuxPssRing, mpeg, data, size, ring, ringSize); }
    inline int sceMpegGetPicture(int mpeg, int destination, int macroblocks) { return port::callGuest<int>(addr::sceMpegGetPicture, mpeg, destination, macroblocks); }
    inline int64_t sceMpegIsEnd(int mpeg) { return route<&sdk_sceMpegIsEnd, mm::addr::sdk_sceMpegIsEnd>(mpeg); }
    inline int sceMpegReset(int mpeg) { return port::callGuest<int>(addr::sceMpegReset, mpeg); }
    inline int sceSdRemoteInit() { return route<&sdk_sceSdRemoteInit, mm::addr::sdk_sceSdRemoteInit>(); }
    // The double-buffered display of the movie player's graphics environments.
    inline void sceGsSetDefDBuffDc(uint32_t buffer, int psm, int width, int height, int ztest, int zpsm, int clear)
    {
        port::callGuest<void>(addr::sceGsSetDefDBuffDc, buffer, psm, width, height, ztest, zpsm, clear);
    }
    inline void sceGsSetHalfOffset(uint32_t environment, int centreX, int centreY, int field) { route<&sdk_sceGsSetHalfOffset, mm::addr::sdk_sceGsSetHalfOffset>(GuestPtr<GsDrawEnv>{environment}, centreX, centreY, field); }
    inline void sceGsSetHalfOffset2(uint32_t environment, int centreX, int centreY, int field) { route<&sdk_sceGsSetHalfOffset2, mm::addr::sdk_sceGsSetHalfOffset2>(GuestPtr<GsDrawEnv>{environment}, centreX, centreY, field); }
    inline void sceGsSwapDBuffDc(uint32_t buffer, int field) { port::callGuest<void>(addr::sceGsSwapDBuffDc, buffer, field); }
    // The sound driver's remote call on the IOP: a command and up to five more words; the 64-bit result is the driver's.
    template <typename... Args>
    inline int64_t sceSdRemote(int mode, int command, Args... args)
    {
        static_assert(sizeof...(Args) <= 6, "the library copies six argument words");
        const uint32_t words[6] = {static_cast<uint32_t>(args)...};
        return route<&sdk_sceSdRemote, mm::addr::sdk_sceSdRemote>(mode, command, words[0], words[1], words[2], words[3], words[4], words[5]);
    }
    // IOP memory through the SIF: allocation returns the IOP address (0 or negative on failure).
    inline uint32_t sceSifAllocSysMemory(int mode, int size) { return port::callGuest<uint32_t>(addr::sceSifAllocSysMemory, mode, size); }
    inline int sceSifFreeSysMemory(uint32_t block) { return route<&sdk_sceSifFreeSysMemory, mm::addr::sdk_sceSifFreeSysMemory>(block); }
    inline int sceSifAllocIopHeap(int size) { return port::callGuest<int>(addr::sceSifAllocIopHeap, size); }
    // Waits for the next vertical sync of the given kind and returns the field now shown.
    inline int64_t sceGsSyncV(int mode) { return port::callGuest<int64_t>(addr::sceGsSyncV, mode); }
    // libc: the first occurrence of a character in a string (the address, 0 when absent), bounded copy, and upper case.
    inline uint32_t index(uint32_t string, int character) { return route<&sdk_index, mm::addr::sdk_index>(GuestPtr<char>{string}, character).address; }
    inline uint32_t strchr(uint32_t string, int character) { return port::callGuest<uint32_t>(addr::strchr, string, character); }
    inline uint32_t strncpy(uint32_t dst, uint32_t src, int count) { return port::callGuest<uint32_t>(addr::strncpy, dst, src, count); }
    inline int toupper(int character) { return route<&sdk_toupper, mm::addr::sdk_toupper>(character); }
    // The disc library: initialise, set the media mode, wait for the disc; start the stream (buffer size and count, then the
    // buffer), search a file (the record is filled at `record`; 0 when absent) and start streaming from its sector.
    inline int sceCdInit(int mode) { return port::callGuest<int>(addr::sceCdInit, mode); }
    inline int sceCdMmode(int mode) { return port::callGuest<int>(addr::sceCdMmode, mode); }
    inline int sceCdStInit(int bufferMax, int bankMax, uint32_t buffer) { return port::callGuest<int>(addr::sceCdStInit, bufferMax, bankMax, buffer); }
    inline int sceCdStStart(uint32_t sector, uint32_t mode) { return port::callGuest<int>(addr::sceCdStStart, sector, mode); }
    inline int64_t sceCdSearchFile(uint32_t record, uint32_t path) { return port::callGuest<int64_t>(addr::sceCdSearchFile, record, path); }
    // Loads an IOP module from a path (the IRX id, negative on failure).
    inline int64_t sceSifLoadModule(uint32_t path, int argc, uint32_t args) { return port::callGuest<int64_t>(addr::sceSifLoadModule, path, argc, args); }
    // Restarts the IOP with the image at `path` (0 until the request is accepted), then waits for it (0 until it is up again);
    // the IOP heap's RPC server is bound for the allocation calls; the file system library's state is cleared.
    inline int64_t sceSifRebootIop(uint32_t image) { return port::callGuest<int64_t>(addr::sceSifRebootIop, image); }
    inline int64_t sceSifSyncIop() { return port::callGuest<int64_t>(addr::sceSifSyncIop); }
    inline int sceSifInitIopHeap() { return port::callGuest<int>(addr::sceSifInitIopHeap); }
    inline int sceFsReset() { return port::callGuest<int>(addr::sceFsReset); }
    // The graphics and DMA hardware: the VIF1 and GIF paths reset, every DMA channel reset (mode 1 also enables the controller),
    // the GS reset (mode, interlace, video mode, frame or field).
    inline void sceGsResetPath() { port::callGuest<void>(addr::sceGsResetPath); }
    inline int sceDmaReset(int enable) { return port::callGuest<int>(addr::sceDmaReset, enable); }
    inline void sceGsResetGraph(int mode, int interlace, int videoMode, int frameMode) { port::callGuest<void>(addr::sceGsResetGraph, mode, interlace, videoMode, frameMode); }
    // The C++ runtime: the static constructors run before main and the global destructor chain after it.
    inline void crt0_InitCpp() { route<&sdk_crt0_InitCpp, mm::addr::sdk_crt0_InitCpp>(); }
    inline void destroy_global_chain() { route<&sdk_destroy_global_chain_stub, mm::addr::sdk_destroy_global_chain_stub>(); }
    // The program's end: the SDK's exit (resets the TLB, then the Exit system call), which the runtime treats as ExitThread.
    inline void kernel_Exit(int status) { route<&sdk_kernel_Exit, mm::addr::sdk_kernel_Exit>(status); }
    // The memory card library: every call is started and returns 0 when accepted (the 64-bit result of the SDK), then
    // sceMcSync reports its end. Port and slot come first; addresses are guest addresses of the game's buffers.
    inline int64_t sceMcGetInfo(int portNumber, int slot, uint32_t type, uint32_t freeBlocks, uint32_t format)
    {
        return port::callGuest<int64_t>(addr::sceMcGetInfo, portNumber, slot, type, freeBlocks, format);
    }
    inline int64_t sceMcGetDir(int portNumber, int slot, uint32_t name, int mode, int maxEntries, uint32_t table)
    {
        return port::callGuest<int64_t>(addr::sceMcGetDir, portNumber, slot, name, mode, maxEntries, table);
    }
    inline int64_t sceMcFormat(int portNumber, int slot) { return port::callGuest<int64_t>(addr::sceMcFormat, portNumber, slot); }
    inline int64_t sceMcUnformat(int portNumber, int slot) { return port::callGuest<int64_t>(addr::sceMcUnformat, portNumber, slot); }
    inline int64_t sceMcMkdir(int portNumber, int slot, uint32_t name) { return port::callGuest<int64_t>(addr::sceMcMkdir, portNumber, slot, name); }
    inline int64_t sceMcDelete(int portNumber, int slot, uint32_t name) { return port::callGuest<int64_t>(addr::sceMcDelete, portNumber, slot, name); }
    inline int64_t sceMcChdir(int portNumber, int slot, uint32_t newDir, uint32_t oldDir)
    {
        return port::callGuest<int64_t>(addr::sceMcChdir, portNumber, slot, newDir, oldDir);
    }
    inline int64_t sceMcOpen(int portNumber, int slot, uint32_t name, int mode) { return port::callGuest<int64_t>(addr::sceMcOpen, portNumber, slot, name, mode); }
    inline int64_t sceMcClose(int fd) { return port::callGuest<int64_t>(addr::sceMcClose, fd); }
    inline int64_t sceMcRead(int fd, uint32_t buffer, int size) { return port::callGuest<int64_t>(addr::sceMcRead, fd, buffer, size); }
    inline int64_t sceMcWrite(int fd, uint32_t buffer, int size) { return port::callGuest<int64_t>(addr::sceMcWrite, fd, buffer, size); }
    inline int64_t sceMcSeek(int fd, int offset, int mode) { return port::callGuest<int64_t>(addr::sceMcSeek, fd, offset, mode); }
    inline int64_t sceMcSync(int mode, uint32_t command, uint32_t result) { return port::callGuest<int64_t>(addr::sceMcSync, mode, command, result); }
    // Registers a static object with the runtime so its destructor (`dtor`, run on `record`'s turn) runs at exit.
    inline void mw_register_global_object(uint32_t object, uint32_t dtor, uint32_t record)
    {
        route<&sdk_mw_register_global_object, mm::addr::sdk_mw_register_global_object>(GuestPtr<void>{object}, GuestPtr<void>{dtor}, GuestPtr<void>{record});
    }
    // Reads the console clock into 8 bytes at `buffer` (status, then BCD seconds, minutes, hours, a pad byte, day, month, year); 0 when it fails.
    inline int64_t sceCdReadClock(uint32_t buffer) { return port::callGuest<int64_t>(addr::sceCdReadClock, buffer); }
    // The disc stream: stop it, and read sectors into a buffer (the error word is written to `errorAddress`).
    inline int sceCdStStop() { return port::callGuest<int>(addr::sceCdStStop); }
    inline int sceCdStRead(int sectors, uint32_t buffer, int mode, uint32_t errorAddress)
    {
        return port::callGuest<int>(addr::sceCdStRead, sectors, buffer, mode, errorAddress);
    }
    // The MPEG library: create a decoder over a work area, and register a callback of a kind.
    inline int sceMpegCreate(int mpeg, int work, int workSize) { return port::callGuest<int>(addr::sceMpegCreate, mpeg, work, workSize); }
    inline int sceMpegAddCallback(int mpeg, int kind, uint32_t function, int argument)
    {
        return port::callGuest<int>(addr::sceMpegAddCallback, mpeg, kind, function, argument);
    }
    inline int sceMpegIsRefBuffEmpty(int mpeg) { return route<&sdk_sceMpegIsRefBuffEmpty, mm::addr::sdk_sceMpegIsRefBuffEmpty>(mpeg); }

    // libdma: starts a packet (at the guest address) on a DMA channel's descriptor (`channel`).
    inline int sceDmaSend(int channel, uint32_t packet) { return port::callGuest<int>(addr::sceDmaSend, channel, packet); }

    // libpad: the SDK's state calls return a 64-bit value that the game compares as a whole (the port and slot are 0 and 0 in
    // practice). Pad data is read into the buffer at `buffer`.
    inline void scePadInit(int mode) { port::callGuest<void>(addr::scePadInit, mode); }
    inline int64_t scePadPortOpen(int portNumber, int slot, uint32_t buffer) { return port::callGuest<int64_t>(addr::scePadPortOpen, portNumber, slot, buffer); }
    inline int64_t scePadRead(int portNumber, int slot, uint32_t buffer) { return port::callGuest<int64_t>(addr::scePadRead, portNumber, slot, buffer); }
    inline int64_t scePadGetState(int portNumber, int slot) { return port::callGuest<int64_t>(addr::scePadGetState, portNumber, slot); }
    inline int64_t scePadGetReqState(int portNumber, int slot) { return port::callGuest<int64_t>(addr::scePadGetReqState, portNumber, slot); }
    inline int64_t scePadInfoMode(int portNumber, int slot, int term, int offset) { return port::callGuest<int64_t>(addr::scePadInfoMode, portNumber, slot, term, offset); }
    inline int64_t scePadSetMainMode(int portNumber, int slot, int mode, int lock) { return port::callGuest<int64_t>(addr::scePadSetMainMode, portNumber, slot, mode, lock); }
    inline int64_t scePadSetActAlign(int portNumber, int slot, uint32_t data) { return port::callGuest<int64_t>(addr::scePadSetActAlign, portNumber, slot, data); }
    inline int64_t scePadInfoPressMode(int portNumber, int slot) { return port::callGuest<int64_t>(addr::scePadInfoPressMode, portNumber, slot); }
    inline int64_t scePadEnterPressMode(int portNumber, int slot) { return port::callGuest<int64_t>(addr::scePadEnterPressMode, portNumber, slot); }

    // libvu0 (matrices are 4 Vec4 rows).
    inline GuestPtr<Vec4> vec(uint32_t address) { return GuestPtr<Vec4>{address}; }
    inline void sceVu0ApplyMatrix(uint32_t out, uint32_t matrix, uint32_t in) { route<&sdk_sceVu0ApplyMatrix, mm::addr::sdk_sceVu0ApplyMatrix>(vec(out), vec(matrix), vec(in)); }
    inline void sceVu0MulMatrix(uint32_t out, uint32_t a, uint32_t b) { route<&sdk_sceVu0MulMatrix, mm::addr::sdk_sceVu0MulMatrix>(vec(out), vec(a), vec(b)); }
    inline void sceVu0UnitMatrix(uint32_t out) { route<&sdk_sceVu0UnitMatrix, mm::addr::sdk_sceVu0UnitMatrix>(vec(out)); }
    inline void sceVu0InversMatrix(uint32_t out, uint32_t in) { route<&sdk_sceVu0InversMatrix, mm::addr::sdk_sceVu0InversMatrix>(vec(out), vec(in)); }
    inline void sceVu0RotMatrixZ(uint32_t out, uint32_t in, ps2f angle) { route<&sdk_sceVu0RotMatrixZ, mm::addr::sdk_sceVu0RotMatrixZ>(vec(out), vec(in), angle); }
    inline void sceVu0RotMatrixY(uint32_t out, uint32_t in, ps2f angle) { route<&sdk_sceVu0RotMatrixY, mm::addr::sdk_sceVu0RotMatrixY>(vec(out), vec(in), angle); }
    inline void sceVu0RotMatrixX(uint32_t out, uint32_t in, ps2f angle) { route<&sdk_sceVu0RotMatrixX, mm::addr::sdk_sceVu0RotMatrixX>(vec(out), vec(in), angle); }
    inline void sceVu0CameraMatrix(uint32_t out, uint32_t eye, uint32_t direction, uint32_t up)
    {
        route<&sdk_sceVu0CameraMatrix, mm::addr::sdk_sceVu0CameraMatrix>(vec(out), vec(eye), vec(direction), vec(up));
    }
    inline void sceVu0NormalLightMatrix(uint32_t out, uint32_t light0, uint32_t light1, uint32_t light2)
    {
        route<&sdk_sceVu0NormalLightMatrix, mm::addr::sdk_sceVu0NormalLightMatrix>(vec(out), vec(light0), vec(light1), vec(light2));
    }
    inline void sceVu0LightColorMatrix(uint32_t out, uint32_t colour0, uint32_t colour1, uint32_t colour2, uint32_t ambient)
    {
        route<&sdk_sceVu0LightColorMatrix, mm::addr::sdk_sceVu0LightColorMatrix>(vec(out), vec(colour0), vec(colour1), vec(colour2), vec(ambient));
    }
}
