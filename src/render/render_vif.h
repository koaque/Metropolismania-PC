#pragma once

// The renderer's VIF1 (plan N4): the VIF codes of a transfer, in order. DIRECT payloads go to the GS; UNPACK fills the
// renderer's copy of VU1 data memory with the hardware's write cycle, mask and row modes; MSCAL and MSCNT run the native
// version of the loaded VU1 program (render_vu1.h) with the double buffer's TOP as the VIF sets it.

#include "render_gs.h"
#include "render_vu1.h"

#include <cstdint>
#include <map>
#include <vector>

namespace mm::render
{
    // The chain walker marks a DMA CALL into a microcode upload packet with two NOPs that carry the packet's address (VIF
    // NOP ignores its NUM and IMMEDIATE fields), so the VIF knows which program the next MPG loads.
    constexpr uint32_t kProgramMarkLow = 0xA5, kProgramMarkHigh = 0xA6;
    // And a tag the port marked (port::enhance::markFrameList, widened frames only) with one carrying the span: the HUD or a
    // backdrop starts.
    constexpr uint32_t kSpanMark = 0xA7;

    class Vif1
    {
    public:
        explicit Vif1(Gs &gs) : m_gs(gs) {}

        void run(const uint8_t *data, size_t size);

        // Per frame: program runs that had no native version (upload packet -> runs), for the stats.
        std::map<uint32_t, uint32_t> &missingRuns() { return m_missing; }
        VuMemory &memory() { return m_memory; }

    private:
        size_t unpack(uint32_t code, const uint8_t *data, size_t available);
        void start(int32_t entry);

        Gs &m_gs;
        VuMemory m_memory;
        uint32_t m_cl = 1, m_wl = 1, m_mode = 0, m_mask = 0;
        uint32_t m_row[4] = {}, m_col[4] = {};
        uint32_t m_base = 0, m_ofst = 0, m_tops = 0, m_itops = 0;
        bool m_dbf = false;
        uint32_t m_markLow = 0, m_pending = 0, m_program = 0;
        std::vector<VuPacket> m_packets;
        std::map<uint32_t, uint32_t> m_missing;
    };
}
