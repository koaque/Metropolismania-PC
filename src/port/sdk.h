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
        inline constexpr uint32_t acosf = 0x0011A2A8u;
        inline constexpr uint32_t atan2f = 0x0011A2C0u;
        inline constexpr uint32_t fmodf = 0x0011A2D8u;
        inline constexpr uint32_t fptosi = 0x00127828u;
        inline constexpr uint32_t fptoui = 0x00127918u;
        inline constexpr uint32_t sinf = 0x0011A1A8u;
        inline constexpr uint32_t memcpy = 0x00121B90u;
        inline constexpr uint32_t memmove = 0x00121C40u;
        inline constexpr uint32_t memset = 0x00121D48u;
        inline constexpr uint32_t strcat = 0x00123660u;
        inline constexpr uint32_t strcmp = 0x00123920u;
        inline constexpr uint32_t strcpy = 0x00123A68u;
        inline constexpr uint32_t strcspn = 0x00123B80u;
        inline constexpr uint32_t strlen = 0x00123C00u;
        inline constexpr uint32_t sceVu0MulMatrix = 0x00104B68u;
        inline constexpr uint32_t sceVu0UnitMatrix = 0x00104E60u;
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
    inline ps2f sinf(ps2f x) { return port::callGuest<ps2f>(addr::sinf, x); }

    // Float to int conversion helper of the compiler runtime.
    inline int fptosi(ps2f value) { return port::callGuest<int>(addr::fptosi, value); }
    inline uint32_t fptoui(ps2f value) { return port::callGuest<uint32_t>(addr::fptoui, value); }

    // libc: the guest addresses of the buffers.
    inline uint32_t memcpy(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memcpy, dst, src, size); }
    inline uint32_t memmove(uint32_t dst, uint32_t src, int size) { return port::callGuest<uint32_t>(addr::memmove, dst, src, size); }
    inline uint32_t memset(uint32_t dst, int value, int size) { return port::callGuest<uint32_t>(addr::memset, dst, value, size); }
    inline int strcmp(uint32_t a, uint32_t b) { return port::callGuest<int>(addr::strcmp, a, b); }
    inline int abs(int value) { return port::callGuest<int>(addr::abs, value); }
    inline uint32_t strcpy(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcpy, dst, src); }
    inline uint32_t strcat(uint32_t dst, uint32_t src) { return port::callGuest<uint32_t>(addr::strcat, dst, src); }
    inline int strlen(uint32_t s) { return port::callGuest<int>(addr::strlen, s); }
    inline int strcspn(uint32_t s, uint32_t reject) { return port::callGuest<int>(addr::strcspn, s, reject); }

    // libvu0 (matrices are 4 Vec4 rows).
    inline void sceVu0MulMatrix(uint32_t out, uint32_t a, uint32_t b) { port::callGuest<void>(addr::sceVu0MulMatrix, out, a, b); }
    inline void sceVu0UnitMatrix(uint32_t out) { port::callGuest<void>(addr::sceVu0UnitMatrix, out); }
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
