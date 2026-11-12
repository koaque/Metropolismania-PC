// The native VU1 programs' registry and their test against captured runs. See render_vu1.h.

#include "render_vu1.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace mm::render
{
    VuProgram *nativeModelProgram0();
    VuProgram *nativeModelProgram1();
    VuProgram *nativeUnlitModelProgram();
    VuProgram *nativeTerrainProgram(uint32_t uploadPacket);
    VuProgram *nativeWeatherProgram(uint32_t uploadPacket);

    VuProgram *nativeProgram(uint32_t uploadPacket)
    {
        switch (uploadPacket)
        {
        case 0x001A3C80u:
            return nativeModelProgram0();
        case 0x001A4780u:
            return nativeModelProgram1();
        case 0x001A8330u:
            return nativeUnlitModelProgram();
        default:
            if (VuProgram *program = nativeTerrainProgram(uploadPacket))
                return program;
            return nativeWeatherProgram(uploadPacket);
        }
    }

    void runVuProgram(VuProgram &program, VuMemory &memory, int32_t entry, uint32_t top, uint32_t itop, std::vector<VuPacket> &out)
    {
        const int rounding = std::fegetround();
        std::fesetround(FE_TOWARDZERO);
        program.run(memory, entry, top, itop, out);
        std::fesetround(rounding);
    }

    // The VU's FTOI4 and FTOI0: truncation toward zero, saturating at the int32 range.
    uint32_t vuFtoi4(float f)
    {
        const double v = std::trunc(static_cast<double>(f) * 16.0);
        return static_cast<uint32_t>(static_cast<int32_t>(std::clamp(v, -2147483648.0, 2147483647.0)));
    }

    uint32_t vuFtoi0(float f)
    {
        const double v = std::trunc(static_cast<double>(f));
        return static_cast<uint32_t>(static_cast<int32_t>(std::clamp(v, -2147483648.0, 2147483647.0)));
    }

    namespace
    {
        // A .vu1r record (written by the runtime's VU1 capture, read by tools/vu1_inspect.py).
        struct Record
        {
            uint32_t kind = 0, start = 0, top = 0, itop = 0;
            float vf[32][4] = {}; // the float registers before the run
            int32_t vi[16] = {};  // and the integer ones
            std::vector<uint8_t> code, dataBefore;
            std::vector<std::vector<uint8_t>> packets;
        };

        bool loadRecord(const std::filesystem::path &path, Record &r)
        {
            std::ifstream in(path, std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (d.size() < 60 || std::memcmp(d.data(), "VU1R", 4) != 0)
                return false;
            uint32_t h[10];
            std::memcpy(h, d.data() + 4, sizeof(h));
            const uint32_t version = h[0], codeSize = h[6], dataSize = h[7], stateSize = h[8], schedSize = h[9];
            if (version != 2)
                return false;
            r.kind = h[1], r.start = h[2], r.top = h[3], r.itop = h[4];
            size_t pos = 44 + 16; // header, cycles before and after
            if (stateSize >= sizeof(r.vf) + sizeof(r.vi) && pos + sizeof(r.vf) + sizeof(r.vi) <= d.size())
            {
                std::memcpy(r.vf, d.data() + pos, sizeof(r.vf)); // VU1State starts with vf00-vf31, then vi00-vi15
                std::memcpy(r.vi, d.data() + pos + sizeof(r.vf), sizeof(r.vi));
            }
            pos += stateSize + schedSize;
            if (pos + codeSize + 2 * static_cast<size_t>(dataSize) + stateSize + 4 > d.size())
                return false;
            r.code.assign(d.begin() + pos, d.begin() + pos + codeSize);
            pos += codeSize;
            r.dataBefore.assign(d.begin() + pos, d.begin() + pos + dataSize);
            pos += dataSize + stateSize + dataSize; // the state and data after
            uint32_t count;
            std::memcpy(&count, d.data() + pos, 4);
            pos += 4;
            for (uint32_t i = 0; i < count && pos + 4 <= d.size(); ++i)
            {
                uint32_t size;
                std::memcpy(&size, d.data() + pos, 4);
                pos += 4;
                if (pos + size > d.size())
                    return false;
                r.packets.emplace_back(d.begin() + pos, d.begin() + pos + size);
                pos += size;
            }
            return true;
        }

        // Which upload packet's microcode the record's code memory holds: each packet (a DMA RET tag, then VIF codes with
        // MPGs) is read from the ELF in RDRAM and its uploads compared.
        uint32_t programOf(const Record &r, const uint8_t *rdram)
        {
            static const uint32_t kPackets[] = {0x1A3C80, 0x1A4780, 0x1A5EB0, 0x1A6440, 0x1A76F0, 0x1A7B30, 0x1A8330,
                                                0x1A8A00, 0x1A8E20, 0x1A91C0, 0x1A9A90, 0x1A9F10, 0x1AA340};
            for (uint32_t packet : kPackets)
            {
                uint64_t tag;
                std::memcpy(&tag, rdram + packet, 8);
                const size_t end = packet + 16 + (tag & 0xFFFF) * 16u;
                size_t pos = packet + 8; // the tag's upper half is VIF codes too
                bool any = false, all = true;
                while (pos + 4 <= end)
                {
                    uint32_t code;
                    std::memcpy(&code, rdram + pos, 4);
                    pos += 4;
                    if (((code >> 24) & 0x7F) != 0x4A)
                        continue;
                    const uint32_t num = ((code >> 16) & 0xFF) != 0 ? (code >> 16) & 0xFF : 256;
                    const size_t at = (code & 0xFFFF) * 8u, bytes = num * 8u;
                    any = true;
                    if (at + bytes > r.code.size() || std::memcmp(r.code.data() + at, rdram + pos, bytes) != 0)
                        all = false;
                    pos += bytes;
                }
                if (any && all)
                    return packet;
            }
            return 0;
        }

        // One qword of the packed GIF packet a program's output becomes, and what it holds.
        enum class Slot : uint8_t
        {
            Tag,
            Register, // a 64-bit register value (TEX0_1), compared whole
            Prim,     // PRIM: the low 11 bits
            St,
            Rgbaq,
            Xyzf,
            Stale,      // not written by the program: not compared
            StaleXyzf,  // not written but for the ADC bit (ISW.w of 0x8000)
        };

        struct Qword
        {
            Slot slot;
            uint32_t w[4];
        };

        // The tag, then for every vertex the register writes ahead of it and its ST, RGBAQ and XYZF2.
        std::vector<Qword> encode(const VuPacket &p)
        {
            std::vector<Qword> out;
            out.push_back({Slot::Tag, {static_cast<uint32_t>(p.tagLo), static_cast<uint32_t>(p.tagLo >> 32),
                                       static_cast<uint32_t>(p.tagHi), static_cast<uint32_t>(p.tagHi >> 32)}});
            for (const VuVertex &v : p.vertices)
            {
                for (int i = 0; i < v.beforeCount; ++i)
                {
                    const VuRegister &r = v.before[i];
                    const Slot slot = r.stale ? Slot::Stale : r.reg == 0 ? Slot::Prim : Slot::Register;
                    out.push_back({slot, {static_cast<uint32_t>(r.value), static_cast<uint32_t>(r.value >> 32), 0, 0}});
                }
                uint32_t s, t, q;
                std::memcpy(&s, &v.s, 4);
                std::memcpy(&t, &v.t, 4);
                std::memcpy(&q, &v.q, 4);
                if (v.hasSt)
                    out.push_back({v.stale ? Slot::Stale : Slot::St, {s, t, q, 0}});
                if (v.hasRgba)
                    out.push_back({v.stale ? Slot::Stale : Slot::Rgbaq, {v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]}});
                out.push_back({v.stale ? (v.adc ? Slot::StaleXyzf : Slot::Stale) : Slot::Xyzf,
                               {vuFtoi4(v.x), vuFtoi4(v.y), vuFtoi4(v.z), (vuFtoi4(v.fog) & 0xFFFF) | (v.adc ? 0x8000u : 0u)}});
            }
            return out;
        }

        // How far a native packet is from the real one: 0 the same as the GS reads it, 1 within rounding (float rounding,
        // one 16th of a pixel, one colour step), 2 different. `where` says where the first worst is.
        int compare(const std::vector<uint8_t> &real, const std::vector<Qword> &native, std::string &where)
        {
            const size_t qwords = real.size() / 16;
            if (native.size() != qwords)
            {
                where = "size " + std::to_string(native.size()) + " vs " + std::to_string(qwords) + " qwords";
                return 2;
            }
            int worst = 0;
            const auto note = [&](int level, size_t qword, const std::string &what) {
                if (level > worst)
                {
                    worst = level;
                    where = what + " at qword " + std::to_string(qword);
                }
            };
            for (size_t q = 0; q < qwords; ++q)
            {
                const uint32_t *n = native[q].w;
                uint32_t r[4];
                std::memcpy(r, real.data() + q * 16, 16);
                char text[128];
                switch (native[q].slot)
                {
                case Slot::Tag: // NLOOP, EOP, the PRIM word, NREG and the register list (REGS)
                    if ((r[0] & 0xFFFF) != (n[0] & 0xFFFF) || r[1] != n[1] || r[2] != n[2] || r[3] != n[3])
                    {
                        std::snprintf(text, sizeof(text), "tag %08x %08x %08x %08x vs native %08x %08x %08x %08x", r[0], r[1],
                                      r[2], r[3], n[0], n[1], n[2], n[3]);
                        note(2, q, text);
                        return worst;
                    }
                    break;
                case Slot::Register:
                    if (r[0] != n[0] || r[1] != n[1])
                    {
                        std::snprintf(text, sizeof(text), "register %08x%08x vs native %08x%08x", r[1], r[0], n[1], n[0]);
                        note(2, q, text);
                    }
                    break;
                case Slot::Prim:
                    if ((r[0] & 0x7FF) != (n[0] & 0x7FF))
                        note(2, q, "PRIM");
                    break;
                case Slot::St: // S, T, Q as floats
                    for (int i = 0; i < 3; ++i)
                    {
                        float a, b;
                        std::memcpy(&a, &r[i], 4);
                        std::memcpy(&b, &n[i], 4);
                        if (r[i] != n[i])
                        {
                            std::snprintf(text, sizeof(text), "ST lane %d %.9g vs native %.9g", i, a, b);
                            note(std::fabs(a - b) <= std::max(std::fabs(a), std::fabs(b)) * 3e-5f + 1e-30f ? 1 : 2, q, text);
                        }
                    }
                    break;
                case Slot::Rgbaq: // the bytes the GS takes
                    for (int i = 0; i < 4; ++i)
                    {
                        const int d = std::abs(static_cast<int>(r[i] & 0xFF) - static_cast<int>(n[i] & 0xFF));
                        if (d != 0)
                        {
                            std::snprintf(text, sizeof(text), "RGBA lane %d %u vs native %u", i, r[i] & 0xFF, n[i] & 0xFF);
                            note(d <= 1 ? 1 : 2, q, text);
                        }
                    }
                    break;
                case Slot::Xyzf: // X, Y in 1/16 pixels, Z (bits 4-27), F (bits 4-11 of w), ADC
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        const int d = std::abs(static_cast<int>(static_cast<int16_t>(r[i])) - static_cast<int>(static_cast<int16_t>(n[i])));
                        if (d != 0)
                        {
                            std::snprintf(text, sizeof(text), "%s %08x vs native %08x", i == 0 ? "X" : "Y", r[i], n[i]);
                            note(d <= 1 ? 1 : 2, q, text);
                        }
                    }
                    const int64_t zr = (r[2] >> 4) & 0xFFFFFF, zn = (n[2] >> 4) & 0xFFFFFF;
                    if (zr != zn)
                    {
                        std::snprintf(text, sizeof(text), "Z %lld vs native %lld", static_cast<long long>(zr), static_cast<long long>(zn));
                        note(std::llabs(zr - zn) <= std::max<int64_t>(2, zr / 20000) ? 1 : 2, q, text);
                    }
                    const int fr = (r[3] >> 4) & 0xFF, fn = (n[3] >> 4) & 0xFF;
                    if (fr != fn)
                        note(std::abs(fr - fn) <= 1 ? 1 : 2, q, "F");
                    if ((r[3] & 0x8000) != (n[3] & 0x8000))
                        note(2, q, "ADC");
                    break;
                }
                case Slot::StaleXyzf:
                    if ((r[3] & 0x8000) != (n[3] & 0x8000))
                        note(2, q, "ADC of a skipped slot");
                    break;
                case Slot::Stale:
                    break;
                }
            }
            return worst;
        }
    }

    int runVu1RecordTest(const char *dir, const uint8_t *rdram)
    {
        int unreadable = 0;
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".vu1r")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        struct Tally
        {
            int records = 0, exact = 0, rounding = 0, different = 0, skipped = 0;
        };
        std::map<uint32_t, Tally> tallies;
        for (const auto &file : files)
        {
            Record r;
            if (!loadRecord(file, r))
            {
                std::cerr << "[render-vu1] " << file.filename().string() << ": not read" << std::endl;
                ++unreadable;
                continue;
            }
            const uint32_t program = programOf(r, rdram);
            Tally &tally = tallies[program];
            ++tally.records;
            VuProgram *native = nativeProgram(program);
            if (native == nullptr)
            {
                ++tally.skipped;
                continue;
            }
            VuMemory memory;
            std::memcpy(memory.words, r.dataBefore.data(), std::min(r.dataBefore.size(), sizeof(memory.words)));
            std::vector<VuPacket> packets;
            if (r.kind != 0 && !native->takeRegisters(r.vf, r.vi)) // MSCNT: the setup the batch follows
                runVuProgram(*native, memory, 0, r.top, r.itop, packets);
            packets.clear();
            runVuProgram(*native, memory, r.kind == 0 ? static_cast<int32_t>(r.start) : -1, r.top, r.itop, packets);
            int worst = 0;
            std::string where;
            if (packets.size() != r.packets.size())
            {
                worst = 2;
                where = std::to_string(packets.size()) + " packets vs " + std::to_string(r.packets.size());
            }
            else
                for (size_t i = 0; i < packets.size() && worst < 2; ++i)
                {
                    std::string w;
                    const int level = compare(r.packets[i], encode(packets[i]), w);
                    if (level > worst)
                        worst = level, where = "packet " + std::to_string(i) + " " + w;
                }
            (worst == 0 ? tally.exact : worst == 1 ? tally.rounding : tally.different) += 1;
            if (worst == 2)
                std::cerr << "[render-vu1] " << file.filename().string() << " (" << std::hex << program << std::dec
                          << "): different, " << where << std::endl;
        }
        int different = 0;
        for (const auto &entry : tallies)
        {
            std::cerr << "[render-vu1] program " << std::hex << entry.first << std::dec << ": " << entry.second.records
                      << " records, " << entry.second.exact << " exact, " << entry.second.rounding << " within rounding, "
                      << entry.second.different << " different, " << entry.second.skipped << " without a native program"
                      << std::endl;
            different += entry.second.different;
        }
        if (files.empty())
            std::cerr << "[render-vu1] no records in " << dir << std::endl;
        return different + unreadable + (files.empty() ? 1 : 0);
    }
}
