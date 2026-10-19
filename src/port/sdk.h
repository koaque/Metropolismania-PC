// Sony SDK routines that game code calls (libc, libvu0). Ported code reaches them through the recompiled SDK
// code, so they behave exactly as in the original (overlapping copies, VU0 registers) until plan step P7
// ports the SDK. Addresses are for SLUS_203.77 (AGENTS.md rule 4); names come from the SDK signature scan.

#pragma once

#include "abi.h"

namespace mm::sdk
{
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
        inline constexpr uint32_t strcspn = 0x00123B80u;
        inline constexpr uint32_t strlen = 0x00123C00u;
        inline constexpr uint32_t d_utod = 0x001000F0u;
        inline constexpr uint32_t pow = 0x0011A290u;
        inline constexpr uint32_t dptoli = 0x00128440u;
        inline constexpr uint32_t sceVu0MulMatrix = 0x00104B68u;
        inline constexpr uint32_t sceVu0UnitMatrix = 0x00104E60u;
        inline constexpr uint32_t sceVu0InversMatrix = 0x00104C80u;
        inline constexpr uint32_t sceVu0RotMatrixZ = 0x00104F00u;
        inline constexpr uint32_t sceVu0RotMatrixY = 0x00105050u;
        inline constexpr uint32_t sceVu0RotMatrixX = 0x00104FA8u;
        inline constexpr uint32_t sceVu0CameraMatrix = 0x00105170u;
        inline constexpr uint32_t sceVu0NormalLightMatrix = 0x00105220u;
        inline constexpr uint32_t sceVu0LightColorMatrix = 0x001052E0u;
    }

    // libm.
    inline ps2f acosf(ps2f x) { return port::callGuest<ps2f>(addr::acosf, x); }
    inline ps2f atan2f(ps2f y, ps2f x) { return port::callGuest<ps2f>(addr::atan2f, y, x); }
    inline ps2f fmodf(ps2f x, ps2f y) { return port::callGuest<ps2f>(addr::fmodf, x, y); }
    inline ps2f cosf(ps2f x) { return port::callGuest<ps2f>(addr::cosf, x); }
    inline ps2f sinf(ps2f x) { return port::callGuest<ps2f>(addr::sinf, x); }
    inline ps2f sqrtf(ps2f x) { return port::callGuest<ps2f>(addr::sqrtf, x); }

    // Float to int conversion helper of the compiler runtime.
    inline int fptosi(ps2f value) { return port::callGuest<int>(addr::fptosi, value); }
    // Doubles are soft-float, each in one 64-bit register: unsigned to double, pow, double to int.
    inline uint64_t d_utod(uint32_t value) { return port::callGuest<uint64_t>(addr::d_utod, value); }
    inline uint64_t pow(uint64_t x, uint64_t y) { return port::callGuest<uint64_t>(addr::pow, x, y); }
    inline int dptoli(uint64_t value) { return port::callGuest<int>(addr::dptoli, value); }
    inline uint32_t fptoui(ps2f value) { return port::callGuest<uint32_t>(addr::fptoui, value); }
    // The compiler runtime's soft doubles: float to double, absolute value, and "less than" (1 or 0).
    inline uint64_t fptodp(ps2f value) { return port::callGuest<uint64_t>(addr::fptodp, value); }
    inline uint64_t fabs(uint64_t value) { return port::callGuest<uint64_t>(addr::fabs, value); }
    inline int64_t fpLtdf2(uint64_t a, uint64_t b) { return port::callGuest<int64_t>(addr::fpLtdf2, a, b); }
    // Double addition and multiplication, and double to float (the result in f0).
    inline uint64_t dpadd(uint64_t a, uint64_t b) { return port::callGuest<uint64_t>(addr::dpadd, a, b); }
    inline uint64_t dpmul(uint64_t a, uint64_t b) { return port::callGuest<uint64_t>(addr::dpmul, a, b); }
    inline ps2f dptofp(uint64_t value) { return port::callGuest<ps2f>(addr::dptofp, value); }

    // Writes back and invalidates the caches (0: the data cache), which a DMA out of main memory needs first.
    inline void FlushCache(int mode) { port::callGuest<void>(addr::FlushCache, mode); }

    // The GIF packet builder of libgraph (`pk` is the packet structure's address): the game calls Cnt and End with three zero
    // arguments after the packet, Ref with a count of quadwords and three zeros, and gives OpenGifTag the whole 16-byte tag.
    inline void sceGifPkInit(uint32_t pk, uint32_t address) { port::callGuest<void>(addr::sceGifPkInit, pk, address); }
    inline void sceGifPkReset(uint32_t pk) { port::callGuest<void>(addr::sceGifPkReset, pk); }
    inline void sceGifPkTerminate(uint32_t pk) { port::callGuest<void>(addr::sceGifPkTerminate, pk); }
    inline void sceGifPkCnt(uint32_t pk) { port::callGuest<void>(addr::sceGifPkCnt, pk, 0, 0, 0); }
    inline void sceGifPkRef(uint32_t pk, uint32_t address, int qwc) { port::callGuest<void>(addr::sceGifPkRef, pk, address, qwc, 0, 0, 0); }
    inline void sceGifPkEnd(uint32_t pk) { port::callGuest<void>(addr::sceGifPkEnd, pk, 0, 0, 0); }
    inline uint32_t sceGifPkReserve(uint32_t pk, int qwc) { return port::callGuest<uint32_t>(addr::sceGifPkReserve, pk, qwc); }
    inline void sceGifPkOpenGifTag(uint32_t pk, port::Quad tag) { port::callGuest<void>(addr::sceGifPkOpenGifTag, pk, tag); }
    inline void sceGifPkCloseGifTag(uint32_t pk) { port::callGuest<void>(addr::sceGifPkCloseGifTag, pk); }
    inline void sceGifPkAddGsAD(uint32_t pk, int reg, uint64_t data) { port::callGuest<void>(addr::sceGifPkAddGsAD, pk, reg, data); }

    // Kernel: interrupts off and on, and semaphores (the parameter block's address; the id).
    inline int DIntr() { return port::callGuest<int>(addr::DIntr); }
    inline int EIntr() { return port::callGuest<int>(addr::EIntr); }
    inline int CreateSema(uint32_t param) { return port::callGuest<int>(addr::CreateSema, param); }
    inline int DeleteSema(int sema) { return port::callGuest<int>(addr::DeleteSema, sema); }
    // The SIF DMA to the IOP: the transfer descriptors' address and count give an id, whose status is negative once done.
    inline uint32_t sceSifSetDma(uint32_t descriptors, int count) { return port::callGuest<uint32_t>(addr::sceSifSetDma, descriptors, count); }
    inline int64_t sceSifDmaStat(uint32_t id) { return port::callGuest<int64_t>(addr::sceSifDmaStat, id); }
    // The MPEG library.
    inline int sceMpegAddStrCallback(int a0, int a1, int a2, int a3) { return port::callGuest<int>(addr::sceMpegAddStrCallback, a0, a1, a2, a3); }
    inline int sceMpegDelete(int mpeg) { return port::callGuest<int>(addr::sceMpegDelete, mpeg); }

    // libc: the guest addresses of the buffers.
    inline uint32_t memcpy(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memcpy, dst, src, size); }
    inline uint32_t memmove(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memmove, dst, src, size); }
    inline uint32_t memset(uint32_t dst, int value, int size) { return port::callGuest<uint32_t>(addr::memset, dst, value, size); }
    inline int strcmp(uint32_t a, uint32_t b) { return port::callGuest<int>(addr::strcmp, a, b); }
    inline int abs(int value) { return port::callGuest<int>(addr::abs, value); }
    inline uint32_t strcpy(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcpy, dst, src); }
    inline uint32_t strcat(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcat, dst, src); }
    inline ps2f atanf(ps2f x) { return port::callGuest<ps2f>(addr::atanf, x); }
    inline uint32_t malloc(uint32_t size) { return port::callGuest<uint32_t>(addr::malloc, size); }
    inline void free(uint32_t block) { port::callGuest<void>(addr::free, block); }
    inline void operator_delete(uint32_t object) { port::callGuest<void>(addr::operator_delete, object); }
    // sprintf with one integer argument (all this game's callers have at most one so far).
    inline int sprintf(uint32_t dst, uint32_t format, int value) { return port::callGuest<int>(addr::sprintf, dst, format, value); }
    // The kernel's file calls (the runtime's file I/O; descriptors are small non-negative ints, errors are negative).
    inline int sceOpen(uint32_t path, int flags) { return port::callGuest<int>(addr::sceOpen, path, flags); }
    inline int sceClose(int fd) { return port::callGuest<int>(addr::sceClose, fd); }
    inline int sceLseek(int fd, int offset, int whence) { return port::callGuest<int>(addr::sceLseek, fd, offset, whence); }
    inline int sceRead(int fd, uint32_t buffer, int bytes) { return port::callGuest<int>(addr::sceRead, fd, buffer, bytes); }
    inline int sceIoctl(int fd, int command, uint32_t argument) { return port::callGuest<int>(addr::sceIoctl, fd, command, argument); }
    inline int64_t sceCdDiskReady(int mode) { return port::callGuest<int64_t>(addr::sceCdDiskReady, mode); }
    inline int strlen(uint32_t s) { return port::callGuest<int>(addr::strlen, s); }
    inline int strcspn(uint32_t s, uint32_t reject) { return port::callGuest<int>(addr::strcspn, s, reject); }

    // libvu0 (matrices are 4 Vec4 rows).
    inline void sceVu0MulMatrix(uint32_t out, uint32_t a, uint32_t b) { port::callGuest<void>(addr::sceVu0MulMatrix, out, a, b); }
    inline void sceVu0UnitMatrix(uint32_t out) { port::callGuest<void>(addr::sceVu0UnitMatrix, out); }
    inline void sceVu0InversMatrix(uint32_t out, uint32_t in) { port::callGuest<void>(addr::sceVu0InversMatrix, out, in); }
    inline void sceVu0RotMatrixZ(uint32_t out, uint32_t in, ps2f angle) { port::callGuest<void>(addr::sceVu0RotMatrixZ, out, in, angle); }
    inline void sceVu0RotMatrixY(uint32_t out, uint32_t in, ps2f angle) { port::callGuest<void>(addr::sceVu0RotMatrixY, out, in, angle); }
    inline void sceVu0RotMatrixX(uint32_t out, uint32_t in, ps2f angle) { port::callGuest<void>(addr::sceVu0RotMatrixX, out, in, angle); }
    inline void sceVu0CameraMatrix(uint32_t out, uint32_t eye, uint32_t direction, uint32_t up)
    {
        port::callGuest<void>(addr::sceVu0CameraMatrix, out, eye, direction, up);
    }
    inline void sceVu0NormalLightMatrix(uint32_t out, uint32_t light0, uint32_t light1, uint32_t light2)
    {
        port::callGuest<void>(addr::sceVu0NormalLightMatrix, out, light0, light1, light2);
    }
    inline void sceVu0LightColorMatrix(uint32_t out, uint32_t colour0, uint32_t colour1, uint32_t colour2, uint32_t ambient)
    {
        port::callGuest<void>(addr::sceVu0LightColorMatrix, out, colour0, colour1, colour2, ambient);
    }
}
