// Differential tests for ported functions (plan P2).
//
// Capture: MMPC_CAPTURE=<names or 0xADDRs, comma separated, or "ported"> during a normal run records
// MMPC_CAPTURE_MAX calls (default 8) of each listed function, every MMPC_CAPTURE_EVERY-th call (default 1,
// so the first ones), into MMPC_CAPTURE_DIR/<name>/<n>.mmdt:
// the CPU context at entry, all of RDRAM (deflate) and the scratchpad. Ported functions are not installed
// during a capture, so the records show the original code at work. Records are game-derived: they stay
// in work/difftest/ and are never committed.
//
// Replay: MMPC_DIFFTEST=<dir> (with PS2X_HEADLESS=1) replays, right after the ELF is loaded, every record of
// every ported function twice from the same state: through the generated function and through the ported
// one. It compares RDRAM and the scratchpad (except the free stack below the entry sp, where the two use
// different frames), the result register (v0 or f0, unless the function returns void), sp, ra and the
// callee-saved registers (s0-s7, gp, fp, f20-f31). The process exits with the number of failing records.
// Side effects outside guest memory (GS, IOP, files) are not compared yet.

#include "difftest.h"

#include "abi.h"
#include "game_functions.h"
#include "port.h"
#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace port
{
    namespace
    {
        constexpr char kMagic[4] = {'M', 'M', 'D', 'T'};
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kFreeStackBytes = 0x10000; // ignored below the entry sp

        const char *env(const char *name)
        {
            const char *value = std::getenv(name);
            return value && value[0] ? value : nullptr;
        }

        std::vector<std::string> splitList(const std::string &text)
        {
            std::vector<std::string> out;
            size_t start = 0;
            while (start <= text.size())
            {
                const size_t end = text.find(',', start);
                std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!item.empty())
                    out.push_back(item);
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return out;
        }

        const char *gameName(uint32_t address)
        {
            for (const mm::GameFunction &f : mm::kGameFunctions)
                if (f.address == address)
                    return f.name;
            return nullptr;
        }

        bool gameAddress(const std::string &item, uint32_t &address)
        {
            if (item.rfind("0x", 0) == 0 || item.rfind("0X", 0) == 0)
            {
                address = static_cast<uint32_t>(std::stoul(item, nullptr, 16));
                return gameName(address) != nullptr;
            }
            for (const mm::GameFunction &f : mm::kGameFunctions)
                if (item == f.name)
                {
                    address = f.address;
                    return true;
                }
            return false;
        }

        // ---------------------------------------------------------------- capture

        struct CaptureTarget
        {
            const char *name;
            GuestFn original;
            uint32_t taken;
            uint64_t calls;
        };
        std::map<uint32_t, CaptureTarget> g_targets;
        std::filesystem::path g_captureDir;
        uint32_t g_captureMax = 8;
        uint32_t g_captureEvery = 1;

        void writeRecord(const std::filesystem::path &path, const R5900Context &ctx)
        {
            int packedSize = 0;
            unsigned char *packed = CompressData(g_rdram, static_cast<int>(PS2_RAM_SIZE), &packedSize);
            if (!packed)
            {
                std::cerr << "[difftest] compression failed for " << path.string() << std::endl;
                return;
            }
            std::ofstream out(path, std::ios::binary);
            const uint32_t header[4] = {kVersion, static_cast<uint32_t>(sizeof(R5900Context)), static_cast<uint32_t>(packedSize),
                                        PS2_SCRATCHPAD_SIZE};
            out.write(kMagic, 4);
            out.write(reinterpret_cast<const char *>(header), sizeof(header));
            out.write(reinterpret_cast<const char *>(&ctx), sizeof(R5900Context));
            out.write(reinterpret_cast<const char *>(packed), packedSize);
            out.write(reinterpret_cast<const char *>(g_scratchpad), PS2_SCRATCHPAD_SIZE);
            MemFree(packed);
        }

        void captureEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t address = ctx->pc;
            auto it = g_targets.find(address);
            if (it == g_targets.end())
                fatal("capture wrapper reached for an unknown address", address);
            CaptureTarget &target = it->second;
            if (target.taken < g_captureMax && target.calls++ % g_captureEvery == 0)
            {
                const std::filesystem::path dir = g_captureDir / target.name;
                std::filesystem::create_directories(dir);
                char file[32];
                std::snprintf(file, sizeof(file), "%03u.mmdt", target.taken);
                writeRecord(dir / file, *ctx);
                ++target.taken;
                std::cerr << "[difftest] captured " << target.name << " #" << target.taken << std::endl;
            }
            target.original(rdram, ctx, runtime);
        }

        // ---------------------------------------------------------------- replay

        struct Record
        {
            R5900Context ctx;
            std::vector<uint8_t> rdram;
            std::vector<uint8_t> scratchpad;
        };

        bool readRecord(const std::filesystem::path &path, Record &rec, std::string &error)
        {
            std::ifstream in(path, std::ios::binary);
            char magic[4];
            uint32_t header[4];
            if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 ||
                !in.read(reinterpret_cast<char *>(header), sizeof(header)))
            {
                error = "not a difftest record";
                return false;
            }
            if (header[0] != kVersion || header[1] != sizeof(R5900Context) || header[3] != PS2_SCRATCHPAD_SIZE)
            {
                error = "record from another runtime build (context or format changed); capture again";
                return false;
            }
            in.read(reinterpret_cast<char *>(&rec.ctx), sizeof(R5900Context));
            std::vector<unsigned char> packed(header[2]);
            in.read(reinterpret_cast<char *>(packed.data()), header[2]);
            rec.scratchpad.resize(PS2_SCRATCHPAD_SIZE);
            in.read(reinterpret_cast<char *>(rec.scratchpad.data()), PS2_SCRATCHPAD_SIZE);
            if (!in)
            {
                error = "truncated record";
                return false;
            }
            int size = 0;
            unsigned char *raw = DecompressData(packed.data(), static_cast<int>(packed.size()), &size);
            if (!raw || size != static_cast<int>(PS2_RAM_SIZE))
            {
                if (raw)
                    MemFree(raw);
                error = "bad RDRAM image";
                return false;
            }
            rec.rdram.assign(raw, raw + size);
            MemFree(raw);
            return true;
        }

        struct Outcome
        {
            R5900Context ctx;
            std::vector<uint8_t> rdram;
            std::vector<uint8_t> scratchpad;
            bool returned = false;
            bool switched = false;
        };

        void runOnce(PS2Runtime &runtime, GuestFn fn, uint32_t address, const Record &rec, Outcome &out)
        {
            std::memcpy(g_rdram, rec.rdram.data(), PS2_RAM_SIZE);
            std::memcpy(g_scratchpad, rec.scratchpad.data(), PS2_SCRATCHPAD_SIZE);
            out.ctx = rec.ctx;
            setGpr64(&out.ctx, 31, kReturnSentinel);
            out.ctx.pc = address;
            runtime.eeScheduler().enterNoPreempt();
            try
            {
                fn(g_rdram, &out.ctx, &runtime);
            }
            catch (const EeDispatcherTransfer &)
            {
                out.switched = true;
            }
            runtime.eeScheduler().leaveNoPreempt();
            out.returned = !out.switched && (out.ctx.pc == kReturnSentinel || out.ctx.pc == address);
            out.rdram.assign(g_rdram, g_rdram + PS2_RAM_SIZE);
            out.scratchpad.assign(g_scratchpad, g_scratchpad + PS2_SCRATCHPAD_SIZE);
        }

        // Points every ported address at its generated function (true) or back at the port (false).
        void useGenerated(PS2Runtime &runtime, bool generated)
        {
            for (const Ported &p : ported())
                runtime.registerFunction(p.address, generated ? p.original : p.wrapper);
        }

        bool sameReg(const R5900Context &a, const R5900Context &b, int reg)
        {
            return std::memcmp(&a.r[reg], &b.r[reg], sizeof(a.r[reg])) == 0;
        }

        // Differences as "what: original vs ported" lines (empty when the runs agree).
        std::vector<std::string> compare(const Ported &p, const Record &rec, const Outcome &a, const Outcome &b)
        {
            std::vector<std::string> diffs;
            auto hex = [](uint64_t v)
            {
                char text[24];
                std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(v));
                return std::string(text);
            };
            if (!a.returned || !b.returned)
                diffs.push_back(std::string("did not return: original ") + (a.returned ? "returned" : a.switched ? "switched thread" : "stopped") +
                                ", ported " + (b.returned ? "returned" : b.switched ? "switched thread" : "stopped"));
            if (p.result == ResultKind::Int && gpr64(&a.ctx, 2) != gpr64(&b.ctx, 2))
                diffs.push_back("v0: " + hex(gpr64(&a.ctx, 2)) + " vs " + hex(gpr64(&b.ctx, 2)));
            if (p.result == ResultKind::Float && std::memcmp(&a.ctx.f[0], &b.ctx.f[0], 4) != 0)
                diffs.push_back("f0: " + std::to_string(a.ctx.f[0]) + " vs " + std::to_string(b.ctx.f[0]));
            static const int kSaved[] = {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};
            for (int reg : kSaved)
                if (!sameReg(a.ctx, b.ctx, reg))
                    diffs.push_back("r" + std::to_string(reg) + ": " + hex(gpr64(&a.ctx, reg)) + " vs " + hex(gpr64(&b.ctx, reg)));
            for (int f = 20; f < 32; ++f)
                if (std::memcmp(&a.ctx.f[f], &b.ctx.f[f], 4) != 0)
                    diffs.push_back("f" + std::to_string(f) + " differs");

            const uint32_t sp = static_cast<uint32_t>(gpr64(&rec.ctx, 29)) & PS2_RAM_MASK;
            const uint32_t freeLow = sp > kFreeStackBytes ? sp - kFreeStackBytes : 0u;
            size_t reported = 0, total = 0;
            for (uint32_t i = 0; i < PS2_RAM_SIZE;)
            {
                if (i >= freeLow && i < sp)
                {
                    i = sp;
                    continue;
                }
                if (a.rdram[i] == b.rdram[i])
                {
                    ++i;
                    continue;
                }
                uint32_t end = i;
                while (end < PS2_RAM_SIZE && !(end >= freeLow && end < sp) && a.rdram[end] != b.rdram[end])
                    ++end;
                total += end - i;
                if (reported++ < 8)
                {
                    std::string line = "RDRAM " + hex(i) + "+" + std::to_string(end - i) + ":";
                    for (uint32_t k = i; k < std::min(end, i + 8u); ++k)
                    {
                        char byte[16];
                        std::snprintf(byte, sizeof(byte), " %02x/%02x", a.rdram[k], b.rdram[k]);
                        line += byte;
                    }
                    diffs.push_back(line);
                }
                i = end;
            }
            if (reported > 8)
                diffs.push_back("RDRAM: " + std::to_string(reported) + " differing ranges, " + std::to_string(total) + " bytes");
            if (a.scratchpad != b.scratchpad)
                diffs.push_back("scratchpad differs");
            return diffs;
        }
    }

    bool captureRequested() { return env("MMPC_CAPTURE") != nullptr; }

    void installCapture(PS2Runtime &runtime)
    {
        const char *list = env("MMPC_CAPTURE");
        if (!list)
            return;
        g_captureDir = env("MMPC_CAPTURE_DIR") ? env("MMPC_CAPTURE_DIR") : "difftest";
        if (const char *max = env("MMPC_CAPTURE_MAX"))
            g_captureMax = static_cast<uint32_t>(std::strtoul(max, nullptr, 0));
        if (const char *every = env("MMPC_CAPTURE_EVERY"))
            g_captureEvery = std::max<uint32_t>(1u, static_cast<uint32_t>(std::strtoul(every, nullptr, 0)));
        std::vector<uint32_t> addresses;
        for (const std::string &item : splitList(list))
        {
            if (item == "ported")
            {
                for (const Ported &p : ported())
                    addresses.push_back(p.address);
                continue;
            }
            uint32_t address = 0;
            if (!gameAddress(item, address))
            {
                std::cerr << "[difftest] MMPC_CAPTURE: unknown game function " << item << std::endl;
                continue;
            }
            addresses.push_back(address);
        }
        for (uint32_t address : addresses)
        {
            if (g_targets.count(address))
                continue;
            g_targets[address] = {gameName(address), runtime.lookupFunction(address), 0, 0};
            runtime.registerFunction(address, captureEntry);
        }
        std::cerr << "[difftest] capturing " << g_targets.size() << " function(s), up to " << g_captureMax << " call(s) each (every "
                  << g_captureEvery << "), into "
                  << std::filesystem::absolute(g_captureDir).string() << std::endl;
    }

    void runDifftestIfRequested(PS2Runtime &runtime)
    {
        const char *dirText = env("MMPC_DIFFTEST");
        if (!dirText)
            return;
        const std::filesystem::path dir(dirText);
        size_t records = 0, failed = 0, functions = 0, untested = 0;
        // results.json: per function, how many records ran and failed (tools/port_status.py reads it).
        std::string results = "{\n";
        auto addResult = [&](const Ported &p, size_t count, size_t bad)
        {
            char line[160];
            std::snprintf(line, sizeof(line), "%s  \"0x%08X\": {\"name\": \"%s\", \"records\": %zu, \"failed\": %zu, \"pass\": %s}",
                          results.size() > 2 ? ",\n" : "", p.address, p.name, count, bad, count > 0 && bad == 0 ? "true" : "false");
            results += line;
        };
        for (const Ported &p : ported())
        {
            const std::filesystem::path sub = dir / p.name;
            std::vector<std::filesystem::path> files;
            if (std::filesystem::is_directory(sub))
                for (const auto &entry : std::filesystem::directory_iterator(sub))
                    if (entry.path().extension() == ".mmdt")
                        files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            if (files.empty())
            {
                ++untested;
                addResult(p, 0, 0);
                std::cout << "[difftest] " << p.name << ": no records (capture with MMPC_CAPTURE=" << p.name << ")" << std::endl;
                continue;
            }
            ++functions;
            size_t ok = 0;
            const size_t failedBefore = failed;
            for (const auto &file : files)
            {
                Record rec;
                std::string error;
                ++records;
                if (!readRecord(file, rec, error))
                {
                    ++failed;
                    std::cout << "[difftest] " << p.name << " " << file.filename().string() << ": " << error << std::endl;
                    continue;
                }
                // The original run uses generated code throughout, the ported run ported code wherever it
                // exists, so a ported callee cannot hide a difference in its caller.
                Outcome original, portedRun;
                useGenerated(runtime, true);
                runOnce(runtime, p.original, p.address, rec, original);
                useGenerated(runtime, false);
                runOnce(runtime, p.wrapper, p.address, rec, portedRun);
                const std::vector<std::string> diffs = compare(p, rec, original, portedRun);
                if (diffs.empty())
                {
                    ++ok;
                    continue;
                }
                ++failed;
                std::cout << "[difftest] FAIL " << p.name << " " << file.filename().string() << std::endl;
                for (const std::string &d : diffs)
                    std::cout << "    " << d << std::endl;
            }
            std::cout << "[difftest] " << p.name << ": " << ok << "/" << files.size() << " exact" << std::endl;
            addResult(p, files.size(), failed - failedBefore);
        }
        results += "\n}\n";
        std::ofstream(dir / "results.json", std::ios::binary) << results;
        std::cout << "[difftest] " << functions << " function(s) tested, " << untested << " without records; " << records
                  << " record(s), " << failed << " failed" << std::endl;
        std::cout.flush();
        std::exit(static_cast<int>(std::min<size_t>(failed, 125)));
    }
}
