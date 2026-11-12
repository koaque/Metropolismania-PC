#pragma once

// The game's VU1 programs as native code (plan N4). The renderer keeps its own copy of VU1 data memory, filled from the VIF1
// stream as the hardware fills it (render_vif.cpp); when the stream starts a program (MSCAL) or continues it (MSCNT), the
// native version of the loaded program reads that memory and hands its primitives to the renderer's GS with float
// precision, where the VU1 would have sent fixed-point GIF packets (XGKICK). Each program is written from what the
// microcode does (docs/RENDER_PIPELINE.md) and checked against captured runs of the real one (render_vu1_test.cpp).

#include <cstdint>
#include <cstring>
#include <vector>

namespace mm::render
{
    constexpr uint32_t kVuQwords = 1024; // VU1 data memory, 16 KB

    struct VuMemory
    {
        alignas(16) uint32_t words[kVuQwords * 4] = {};

        uint32_t *qword(uint32_t address) { return words + (address & (kVuQwords - 1)) * 4; }
        const uint32_t *qword(uint32_t address) const { return words + (address & (kVuQwords - 1)) * 4; }
        uint32_t word(uint32_t address, int lane) const { return qword(address)[lane]; }
        int32_t integer(uint32_t address, int lane) const { return static_cast<int32_t>(qword(address)[lane]); }
        float real(uint32_t address, int lane) const
        {
            float f;
            std::memcpy(&f, qword(address) + lane, 4);
            return f;
        }
        void setReal(uint32_t address, int lane, float f) { std::memcpy(qword(address) + lane, &f, 4); }
    };

    // A register write a program puts in its packet before a vertex (the terrain passes' TEX0_1 and PRIM). `stale`: the
    // program leaves that slot of its output buffer as it was (a quad it skips), so it is not sent here.
    struct VuRegister
    {
        uint32_t reg = 0;
        uint64_t value = 0;
        bool stale = false;
    };

    // One vertex as a program sends it: what the GS would get, before the conversion to fixed point.
    struct VuVertex
    {
        VuRegister before[2]; // register writes ahead of this vertex, in packet order
        uint8_t beforeCount = 0;
        bool stale = false;   // a slot the program does not write but for its ADC bit (a skipped quad): not drawn
        bool hasSt = true;    // the packet gives this vertex an ST (the rain's lines do not)
        bool hasRgba = true;  // and an RGBAQ (the snow's sprites do not: their colour is the packet before's)
        float x = 0, y = 0;   // screen position in GS units (pixels, with the GS's 2048 offset)
        float z = 0;          // depth in GS Z units
        float fog = 0;        // 0..255, the GS's F
        float s = 0, t = 0, q = 1;
        uint8_t rgba[4] = {};
        bool adc = false;     // drawing kick off: the vertex only moves the strip on
    };

    // What one program run sends: GIF packets of a tag, its A+D writes, and vertices. `tag` is the GIF tag as the program
    // writes it (NLOOP filled in); the packet's registers per vertex are the tag's (ST, RGBAQ, XYZF2 in every program here).
    struct VuPacket
    {
        uint64_t tagLo = 0, tagHi = 0;
        std::vector<std::pair<uint32_t, uint64_t>> registers; // A+D writes (register, value), in order, before the vertices
        std::vector<VuVertex> vertices;
    };

    class VuProgram
    {
    public:
        virtual ~VuProgram() = default;
        // MSCAL `entry` (a byte address in micro memory) and MSCNT (`entry` = -1): one run, from TOP and ITOP as the VIF set
        // them. Primitives go to `out`; the program may also write VU1 data memory, as the microcode does.
        virtual void run(VuMemory &memory, int32_t entry, uint32_t top, uint32_t itop, std::vector<VuPacket> &out) = 0;
        // For the record test: what the program's setup left in the VU's registers, taken from a captured run's state (the
        // setup's input in data memory may already be the next instance's or the next pass's). False when not needed.
        virtual bool takeRegisters(const float (&vf)[32][4], const int32_t (&vi)[16]) { return false; }
    };

    // The native version of the program a microcode upload packet (its address in the ELF) loads, or null.
    VuProgram *nativeProgram(uint32_t uploadPacket);

    // Runs a program as the VU does its arithmetic: rounding toward zero (as the runtime's VU1 does).
    void runVuProgram(VuProgram &program, VuMemory &memory, int32_t entry, uint32_t top, uint32_t itop,
                      std::vector<VuPacket> &out);

    // MMPC_RENDER_VU1TEST=<folder of .vu1r records>: every record's run through the native program, its packets compared
    // with the ones the real program sent. Needs the ELF in RDRAM (the programs are told apart by its upload packets).
    // Returns the records that were different or could not be read (1 when the folder has none): the exit status.
    int runVu1RecordTest(const char *folder, const uint8_t *rdram);

    // The fixed-point values a vertex becomes in a packed GIF packet (for comparisons with the real programs).
    uint32_t vuFtoi4(float f);
    uint32_t vuFtoi0(float f);
}
