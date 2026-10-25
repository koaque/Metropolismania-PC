// Differential tests for ported functions (plan P2).
//
// Capture: MMPC_CAPTURE=<names or 0xADDRs, comma separated, or "ported"> during a normal run records
// MMPC_CAPTURE_MAX calls (default 8) of each listed function, every MMPC_CAPTURE_EVERY-th call (default 1,
// so the first ones; "exp" takes calls 1, 8, 64, 512 ... so busy and rare functions both spread over the run),
// into MMPC_CAPTURE_DIR/<name>/<n>.mmdt:
// the CPU context at entry, all of RDRAM (deflate) and the scratchpad. Ported functions are not installed
// during a capture, so the records show the original code at work. Records are game-derived: they stay
// in work/difftest/ and are never committed.
//
// Replay: MMPC_DIFFTEST=<dir> (with PS2X_HEADLESS=1) replays, right after the ELF is loaded, every record of
// every ported function twice from the same state: through the generated function and through the ported
// one. It compares RDRAM and the scratchpad (except the free stack below the entry sp, where the two use
// different frames), the result register (v0 or f0, unless the function returns void), sp, ra, the
// callee-saved registers (s0-s7, gp, fp, f20-f31) and the VU0 registers (vf1-vf31, ACC, Q). The process exits
// with the number of failing records.
//
// Fuzzing: MMPC_DIFFTEST_FUZZ=N with MMPC_DIFFTEST_FUZZ_FUNCTIONS=<names or "all"> also replays each
// record N times with the float and integer arguments mutated (zero often, sign flips, small and large
// values, within each parameter's type); pointers keep their recorded values. That reaches branches the
// recorded calls never take. Only for functions whose integer arguments are not loop bounds.
//
// Subset: MMPC_DIFFTEST_ONLY=<names> replays only those functions (results.json is then left as it was).
// A replay that does not return within MMPC_DIFFTEST_HANG_SECONDS (default 60) is reported as HANG and ends the
// run: a record edited into a state the game never reaches can leave the original waiting forever.
//
// Changes: MMPC_DIFFTEST_CHANGES=<names or "all"> prints, for each record of those functions, the RDRAM ranges
// the original run changed (outside the free stack), and its v0 for functions with an integer result. A record
// that passes but changes nothing, or not what it was made for (a record edited to reach a rare branch), is not
// evidence for that path.
// Side effects outside guest memory (GS, IOP, files) are not compared yet.

#include "difftest.h"

#include "abi.h"
#include "game_functions.h"
#include "port.h"
#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
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
        uint32_t g_captureEvery = 1; // 0: exponential, calls 1, 8, 64, 512 ...

        bool sampleCall(uint64_t call)
        {
            if (g_captureEvery != 0)
                return call % g_captureEvery == 0;
            // call is 0-based: record calls 1, 8, 64 ... (counted from 1), the powers of 8.
            const uint64_t n = call + 1;
            return (n & (n - 1)) == 0 && std::countr_zero(n) % 3 == 0;
        }

        // A record waiting to be compressed and written. The game thread only copies memory (a few ms);
        // a writer thread does the slow part, so captures barely slow the game down.
        struct PendingRecord
        {
            std::filesystem::path path;
            R5900Context ctx;
            std::vector<uint8_t> rdram;
            std::vector<uint8_t> scratchpad;
        };

        std::mutex g_queueMutex;
        std::condition_variable g_queueChanged;
        std::deque<std::unique_ptr<PendingRecord>> g_queue;
        constexpr size_t kMaxPending = 12; // 32 MB each

        void writeRecord(const PendingRecord &rec)
        {
            int packedSize = 0;
            unsigned char *packed = CompressData(rec.rdram.data(), static_cast<int>(rec.rdram.size()), &packedSize);
            if (!packed)
            {
                std::cerr << "[difftest] compression failed for " << rec.path.string() << std::endl;
                return;
            }
            const std::filesystem::path temp = rec.path.string() + ".part";
            {
                std::ofstream out(temp, std::ios::binary);
                const uint32_t header[4] = {kVersion, static_cast<uint32_t>(sizeof(R5900Context)), static_cast<uint32_t>(packedSize),
                                            PS2_SCRATCHPAD_SIZE};
                out.write(kMagic, 4);
                out.write(reinterpret_cast<const char *>(header), sizeof(header));
                out.write(reinterpret_cast<const char *>(&rec.ctx), sizeof(R5900Context));
                out.write(reinterpret_cast<const char *>(packed), packedSize);
                out.write(reinterpret_cast<const char *>(rec.scratchpad.data()), PS2_SCRATCHPAD_SIZE);
            }
            MemFree(packed);
            std::error_code ec;
            std::filesystem::rename(temp, rec.path, ec); // a killed run leaves only .part files behind
        }

        void writerLoop()
        {
            for (;;)
            {
                std::unique_ptr<PendingRecord> rec;
                {
                    std::unique_lock<std::mutex> lock(g_queueMutex);
                    g_queueChanged.wait(lock, [] { return !g_queue.empty(); });
                    rec = std::move(g_queue.front());
                    g_queue.pop_front();
                }
                g_queueChanged.notify_all();
                writeRecord(*rec);
            }
        }

        void queueRecord(const std::filesystem::path &path, const R5900Context &ctx)
        {
            auto rec = std::make_unique<PendingRecord>();
            rec->path = path;
            rec->ctx = ctx;
            rec->rdram.assign(g_rdram, g_rdram + PS2_RAM_SIZE);
            rec->scratchpad.assign(g_scratchpad, g_scratchpad + PS2_SCRATCHPAD_SIZE);
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueChanged.wait(lock, [] { return g_queue.size() < kMaxPending; });
            g_queue.push_back(std::move(rec));
            g_queueChanged.notify_all();
        }

        void captureEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t address = ctx->pc;
            auto it = g_targets.find(address);
            if (it == g_targets.end())
                fatal("capture wrapper reached for an unknown address", address);
            CaptureTarget &target = it->second;
            if (target.taken < g_captureMax && sampleCall(target.calls++))
            {
                const std::filesystem::path dir = g_captureDir / target.name;
                std::filesystem::create_directories(dir);
                char file[32];
                std::snprintf(file, sizeof(file), "%03u.mmdt", target.taken);
                queueRecord(dir / file, *ctx);
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
            std::vector<std::pair<uint32_t, uint32_t>> liveHeap;  // (address, size) of the guest heap blocks in use afterwards
            bool returned = false;
            bool switched = false;
            std::string fault;
        };

        void runOnce(PS2Runtime &runtime, GuestFn fn, uint32_t address, const Record &rec, const R5900Context &start, Outcome &out)
        {
            // The runtime keeps the heap's bookkeeping and the open file descriptors outside RDRAM. Every run starts
            // from the same ones, so a call that allocates or opens gets the same block and descriptor each time.
            static const PS2Runtime::GuestHeapState baseHeap = runtime.saveGuestHeapState();
            static const int32_t baseDescriptor = runtime.vfs().nextDescriptor();
            runtime.restoreGuestHeapState(baseHeap);
            runtime.vfs().closeFrom(baseDescriptor);
            std::memcpy(g_rdram, rec.rdram.data(), PS2_RAM_SIZE);
            std::memcpy(g_scratchpad, rec.scratchpad.data(), PS2_SCRATCHPAD_SIZE);
            out.ctx = start;
            setGpr64(&out.ctx, 31, kReturnSentinel);
            out.ctx.pc = address;
            runtime.eeScheduler().enterNoPreempt();
            try
            {
                fn(g_rdram, &out.ctx, &runtime);
                finishGuestCall(g_rdram, &out.ctx, &runtime, address, kReturnSentinel);
            }
            catch (const EeDispatcherTransfer &)
            {
                out.switched = true;
            }
            catch (const Fault &e)
            {
                out.fault = e.what();
            }
            runtime.eeScheduler().leaveNoPreempt();
            out.liveHeap.clear();
            for (const PS2Runtime::GuestHeapBlock &block : runtime.saveGuestHeapState().blocks)
                if (!block.free)
                    out.liveHeap.emplace_back(block.addr, block.size);
            std::sort(out.liveHeap.begin(), out.liveHeap.end());
            out.returned = !out.switched && out.fault.empty() && (out.ctx.pc == kReturnSentinel || out.ctx.pc == address);
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
            if (!a.fault.empty())
                diffs.push_back("original faulted: " + a.fault);
            if (!b.fault.empty())
                diffs.push_back("ported faulted: " + b.fault);
            if (!a.returned || !b.returned)
                diffs.push_back(std::string("did not return: original ") + (a.returned ? "returned" : a.switched ? "switched thread" : "stopped at pc " + hex(a.ctx.pc)) +
                                ", ported " + (b.returned ? "returned" : b.switched ? "switched thread" : "stopped at pc " + hex(b.ctx.pc)));
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
            // VU0 registers carry over from one function to the next: some code reads lanes it never wrote (the w
            // of vf6 in mm_Math_TriangleNormal, all of vf12 in mm_BldgField_Draw), so a port leaves them as the
            // original does.
            const auto lanes = [&](const __m128 &value) {
                uint32_t bits[4];
                std::memcpy(bits, &value, sizeof(bits));
                return hex(bits[0]) + " " + hex(bits[1]) + " " + hex(bits[2]) + " " + hex(bits[3]);
            };
            // Not compared for functions that end in a call whose VU0 loads read stack leftovers: Map_SampleHeight leaves the
            // w lanes of its stack vectors unwritten, so what the generated callees left there before (and the ported ones
            // do not) decides the final lanes. The game never reads them back: everything else is still compared.
            static const std::set<std::string> kVu0FromLeftovers = {"TalkPersonFar_ctor", "TalkPersonNear_ctor", "Person_ChooseDestination"};
            const bool vu0Compared = kVu0FromLeftovers.count(p.name) == 0;
            for (int v = 1; v < 32 && vu0Compared; ++v)
                if (std::memcmp(&a.ctx.vu0_vf[v], &b.ctx.vu0_vf[v], sizeof(a.ctx.vu0_vf[v])) != 0)
                    diffs.push_back("vf" + std::to_string(v) + " differs: " + lanes(a.ctx.vu0_vf[v]) + " vs " + lanes(b.ctx.vu0_vf[v]));
            if (vu0Compared && std::memcmp(&a.ctx.vu0_acc, &b.ctx.vu0_acc, sizeof(a.ctx.vu0_acc)) != 0)
                diffs.push_back("ACC differs: " + lanes(a.ctx.vu0_acc) + " vs " + lanes(b.ctx.vu0_acc));
            if (vu0Compared && std::memcmp(&a.ctx.vu0_q, &b.ctx.vu0_q, sizeof(a.ctx.vu0_q)) != 0)
                diffs.push_back("Q differs");

            // The semaphore id Movie_ViBufCreate stores at +0x40 of its buffer counts up in the kernel, which the replay does not
            // reset between the generated run and the ported one: the ported run always gets the next id.
            // The same for the IOP memory addresses (allocated through the SIF) that the movie's audio and disc stream objects keep:
            // the IOP's allocator is not reset between the two runs either.
            uint32_t ignoreLow = 0, ignoreHigh = 0, ignoreLow2 = 0, ignoreHigh2 = 0, ignoreLow3 = 0, ignoreHigh3 = 0;
            const uint32_t object = static_cast<uint32_t>(gpr64(&rec.ctx, 4));
            if (p.name == "Movie_ViBufCreate")
                ignoreLow = (object + 0x40u) & PS2_RAM_MASK;
            else if (p.name == "Movie_VideoDecCreate")
                ignoreLow = (object + 0x88u) & PS2_RAM_MASK;
            else if (p.name == "Movie_AudioDecCreate")
            {
                ignoreLow = (object + 0x44u) & PS2_RAM_MASK;
                ignoreLow2 = (object + 0x58u) & PS2_RAM_MASK;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            else if (p.name == "Movie_StrFileOpen")
                ignoreLow = (object + 0x2Cu) & PS2_RAM_MASK;
            // Sound_Init binds the sound driver's RPC client: the runtime numbers the binds (the client's id at +4) and keeps the server
            // descriptor it made on the first bind (its sid at 0x1F10000) outside the replay's reset, so the ported run sees them differ.
            else if (p.name == "Sound_Init")
            {
                ignoreLow = 0x001FE694u;
                ignoreLow2 = 0x01F10000u;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            // McManager_NewSaveName writes the console clock in seconds as digits: the two runs read it a moment apart, so the last
            // digits (bytes 16 to 23 of the name slot, which holds eight digits after the id string) are not compared.
            else if (p.name == "McManager_NewSaveName")
            {
                const uint32_t name = (0x001F3CF0u + (static_cast<uint32_t>(gpr64(&rec.ctx, 5)) & 0xFFu) * 0x20u) & PS2_RAM_MASK;
                ignoreLow = name + 0x10u;
                ignoreLow2 = name + 0x14u;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            // McSave_WriteIconSys (and the save state that calls it) opens a file on the card: the runtime's descriptor counter (kept outside RDRAM and not reset between the
            // two runs) gives the ported run the next number, which the game keeps in three words.
            else if (p.name == "McSave_WriteIconSys" || p.name == "PauseMenu_StateSave")
            {
                ignoreLow = 0x001BDCDCu;
                ignoreLow2 = 0x001F63E4u;
                ignoreHigh2 = ignoreLow2 + 4u;
                ignoreLow3 = 0x001F6408u;
                ignoreHigh3 = ignoreLow3 + 4u;
            }
            if (ignoreLow != 0)
                ignoreHigh = ignoreLow + 4u;
            const auto same = [&](uint32_t k) {
                return a.rdram[k] == b.rdram[k] || (k >= ignoreLow && k < ignoreHigh) || (k >= ignoreLow2 && k < ignoreHigh2) ||
                       (k >= ignoreLow3 && k < ignoreHigh3);
            };

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
                if (same(i))
                {
                    ++i;
                    continue;
                }
                uint32_t end = i;
                while (end < PS2_RAM_SIZE && !(end >= freeLow && end < sp) && !same(end))
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
            // MMPC_DIFFTEST_DUMP=<address>:<words> adds those words (hex and float) of both runs to a failing record.
            if (const char *dump = env("MMPC_DIFFTEST_DUMP"); dump != nullptr && !diffs.empty())
            {
                unsigned long address = 0, words = 0;
                if (std::sscanf(dump, "%lx:%lu", &address, &words) == 2)
                    for (unsigned long k = 0; k < words; ++k)
                    {
                        uint32_t original, ported;
                        std::memcpy(&original, &a.rdram[(address + 4 * k) & PS2_RAM_MASK], 4);
                        std::memcpy(&ported, &b.rdram[(address + 4 * k) & PS2_RAM_MASK], 4);
                        float originalFloat, portedFloat;
                        std::memcpy(&originalFloat, &original, 4);
                        std::memcpy(&portedFloat, &ported, 4);
                        diffs.push_back("dump " + hex(address + 4 * k) + ": original " + hex(original) + " (" + std::to_string(originalFloat) +
                                        ") ported " + hex(ported) + " (" + std::to_string(portedFloat) + ")");
                    }
            }
            if (a.scratchpad != b.scratchpad)
                diffs.push_back("scratchpad differs");
            // The heap's bookkeeping lives outside RDRAM: a destructor that frees the wrong block, or none, only shows here.
            if (a.liveHeap != b.liveHeap)
            {
                std::string line = "heap: " + std::to_string(a.liveHeap.size()) + " blocks in use after the original, " +
                                   std::to_string(b.liveHeap.size()) + " after the port";
                std::vector<std::pair<uint32_t, uint32_t>> onlyOriginal, onlyPorted;
                std::set_difference(a.liveHeap.begin(), a.liveHeap.end(), b.liveHeap.begin(), b.liveHeap.end(), std::back_inserter(onlyOriginal));
                std::set_difference(b.liveHeap.begin(), b.liveHeap.end(), a.liveHeap.begin(), a.liveHeap.end(), std::back_inserter(onlyPorted));
                for (size_t k = 0; k < onlyOriginal.size() && k < 3; ++k)
                    line += "; only original " + hex(onlyOriginal[k].first) + "+" + hex(onlyOriginal[k].second);
                for (size_t k = 0; k < onlyPorted.size() && k < 3; ++k)
                    line += "; only port " + hex(onlyPorted[k].first) + "+" + hex(onlyPorted[k].second);
                diffs.push_back(line);
            }
            return diffs;
        }
    }

    namespace
    {
        // "N bytes in K ranges: addr+len ..." for the RDRAM the run changed from the record (free stack excluded).
        std::string describeChanges(const Record &rec, const Outcome &run)
        {
            const uint32_t sp = static_cast<uint32_t>(gpr64(&rec.ctx, 29)) & PS2_RAM_MASK;
            const uint32_t freeLow = sp > kFreeStackBytes ? sp - kFreeStackBytes : 0u;
            std::string listed;
            size_t ranges = 0, bytes = 0;
            for (uint32_t i = 0; i < PS2_RAM_SIZE;)
            {
                if (i >= freeLow && i < sp)
                {
                    i = sp;
                    continue;
                }
                if (rec.rdram[i] == run.rdram[i])
                {
                    ++i;
                    continue;
                }
                uint32_t end = i;
                while (end < PS2_RAM_SIZE && !(end >= freeLow && end < sp) && rec.rdram[end] != run.rdram[end])
                    ++end;
                bytes += end - i;
                if (ranges++ < 12)
                {
                    char part[32];
                    std::snprintf(part, sizeof(part), " %X+%u", i, end - i);
                    listed += part;
                }
                i = end;
            }
            return std::to_string(bytes) + " bytes in " + std::to_string(ranges) + " ranges:" + listed;
        }
    }

    // ---------------------------------------------------------------- call counts
    // MMPC_CALL_COUNTS=<file>: every game function is wrapped and its calls counted; the file is rewritten
    // every 5 s (timed runs end with a kill) as "address name calls" lines, busiest first. Shows what a
    // run reaches, to choose what to port and capture next.
    namespace
    {
        struct Counted
        {
            const char *name;
            GuestFn original;
            std::atomic<uint64_t> calls{0};
        };
        std::unordered_map<uint32_t, Counted> g_counted;

        void countedEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            auto it = g_counted.find(ctx->pc);
            if (it == g_counted.end())
                fatal("call-count wrapper reached for an unknown address", ctx->pc);
            it->second.calls.fetch_add(1, std::memory_order_relaxed);
            it->second.original(rdram, ctx, runtime);
        }

        void writeCounts(const std::filesystem::path &path)
        {
            std::vector<std::pair<uint64_t, uint32_t>> rows;
            for (const auto &[address, c] : g_counted)
                if (const uint64_t n = c.calls.load(std::memory_order_relaxed))
                    rows.emplace_back(n, address);
            std::sort(rows.rbegin(), rows.rend());
            const std::filesystem::path temp = path.string() + ".tmp";
            {
                std::ofstream out(temp);
                for (const auto &[n, address] : rows)
                {
                    char line[160];
                    std::snprintf(line, sizeof(line), "0x%08X %s %llu\n", address, g_counted.at(address).name,
                                  static_cast<unsigned long long>(n));
                    out << line;
                }
            }
            std::error_code ec;
            std::filesystem::rename(temp, path, ec);
        }
    }

    void installCallCounts(PS2Runtime &runtime)
    {
        const char *file = env("MMPC_CALL_COUNTS");
        if (!file)
            return;
        for (const mm::GameFunction &f : mm::kGameFunctions)
        {
            Counted &c = g_counted[f.address];
            c.name = f.name;
            c.original = runtime.lookupFunction(f.address);
        }
        for (const auto &[address, c] : g_counted)
            runtime.registerFunction(address, countedEntry);
        const std::filesystem::path path = std::filesystem::absolute(file);
        std::thread([path]()
                    {
                        for (;;)
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                            writeCounts(path);
                        }
                    })
            .detach();
        std::cerr << "[difftest] counting calls of " << g_counted.size() << " game functions into " << path.string() << std::endl;
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
            g_captureEvery = std::strcmp(every, "exp") == 0 ? 0u : std::max<uint32_t>(1u, static_cast<uint32_t>(std::strtoul(every, nullptr, 0)));
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
        if (!addresses.empty())
            for (int i = 0; i < 4; ++i)
                std::thread(writerLoop).detach();
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
        setFaultsThrow(true);
        // MMPC_DIFFTEST_FUZZ=N replays every record N more times with mutated arguments, for the functions
        // named in MMPC_DIFFTEST_FUZZ_FUNCTIONS (comma separated, or "all").
        const int fuzzCount = env("MMPC_DIFFTEST_FUZZ") ? std::atoi(env("MMPC_DIFFTEST_FUZZ")) : 0;
        std::set<std::string> fuzzNames;
        bool fuzzAll = false;
        if (const char *names = env("MMPC_DIFFTEST_FUZZ_FUNCTIONS"))
        {
            std::stringstream list(names);
            for (std::string item; std::getline(list, item, ',');)
            {
                if (item == "all")
                    fuzzAll = true;
                else if (!item.empty())
                    fuzzNames.insert(item);
            }
        }
        std::set<std::string> onlyNames;
        if (const char *names = env("MMPC_DIFFTEST_ONLY"))
        {
            std::stringstream list(names);
            for (std::string item; std::getline(list, item, ',');)
                if (!item.empty())
                    onlyNames.insert(item);
        }
        struct Watch
        {
            std::mutex mutex;
            std::string what;
            std::chrono::steady_clock::time_point since;
            bool active = false;
        };
        static Watch watch;
        const int hangSeconds = env("MMPC_DIFFTEST_HANG_SECONDS") ? std::atoi(env("MMPC_DIFFTEST_HANG_SECONDS")) : 60;
        std::thread([hangSeconds] {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::lock_guard<std::mutex> lock(watch.mutex);
                if (watch.active && std::chrono::steady_clock::now() - watch.since > std::chrono::seconds(hangSeconds))
                {
                    std::cout << "[difftest] HANG " << watch.what << " (no return within " << hangSeconds << " s)" << std::endl;
                    std::cout.flush();
                    std::_Exit(124);
                }
            }
        }).detach();
        const auto watchStart = [](const std::string &what) {
            std::lock_guard<std::mutex> lock(watch.mutex);
            watch.what = what;
            watch.since = std::chrono::steady_clock::now();
            watch.active = true;
        };
        const auto watchStop = [] {
            std::lock_guard<std::mutex> lock(watch.mutex);
            watch.active = false;
        };
        std::set<std::string> changeNames;
        bool changesAll = false;
        if (const char *names = env("MMPC_DIFFTEST_CHANGES"))
        {
            std::stringstream list(names);
            for (std::string item; std::getline(list, item, ',');)
            {
                if (item == "all")
                    changesAll = true;
                else if (!item.empty())
                    changeNames.insert(item);
            }
        }
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
            if (!onlyNames.empty() && onlyNames.count(p.name) == 0)
                continue;
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
            size_t ok = 0, fuzzRuns = 0, fuzzOk = 0, fuzzReported = 0;
            const size_t failedBefore = failed;
            const bool fuzzThis = fuzzCount > 0 && p.mutate && (fuzzAll || fuzzNames.count(p.name) != 0);
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
                watchStart(std::string(p.name) + " " + file.filename().string());
                useGenerated(runtime, true);
                runOnce(runtime, p.original, p.address, rec, rec.ctx, original);
                useGenerated(runtime, false);
                runOnce(runtime, p.wrapper, p.address, rec, rec.ctx, portedRun);
                watchStop();
                const std::vector<std::string> diffs = compare(p, rec, original, portedRun);
                if (changesAll || changeNames.count(p.name) != 0)
                {
                    std::cout << "[difftest] " << p.name << " " << file.filename().string() << " changes "
                              << describeChanges(rec, original);
                    if (p.result == ResultKind::Int)
                    {
                        char v0[32];
                        std::snprintf(v0, sizeof(v0), "; v0 %llX", static_cast<unsigned long long>(gpr64(&original.ctx, 2)));
                        std::cout << v0;
                    }
                    std::cout << std::endl;
                }
                if (diffs.empty())
                    ++ok;
                else
                {
                    ++failed;
                    std::cout << "[difftest] FAIL " << p.name << " " << file.filename().string() << std::endl;
                    for (const std::string &d : diffs)
                        std::cout << "    " << d << std::endl;
                }

                // Fuzzing: the same record with other float and integer arguments (pointers unchanged).
                for (int variant = 0; fuzzThis && variant < fuzzCount; ++variant)
                {
                    R5900Context start = rec.ctx;
                    p.mutate(&start, 0x9E3779B9u * static_cast<uint32_t>((&file - files.data()) * 1009 + variant + 1));
                    Outcome a, b;
                    watchStart(std::string(p.name) + " " + file.filename().string() + " fuzz " + std::to_string(variant));
                    useGenerated(runtime, true);
                    runOnce(runtime, p.original, p.address, rec, start, a);
                    useGenerated(runtime, false);
                    runOnce(runtime, p.wrapper, p.address, rec, start, b);
                    watchStop();
                    ++fuzzRuns;
                    const std::vector<std::string> fd = compare(p, rec, a, b);
                    if (fd.empty())
                    {
                        ++fuzzOk;
                        continue;
                    }
                    ++failed;
                    if (fuzzReported++ < 3)
                    {
                        std::cout << "[difftest] FAIL " << p.name << " " << file.filename().string() << " fuzz " << variant << ": a0-t3";
                        for (int reg = 4; reg < 12; ++reg)
                            std::cout << " " << std::hex << gpr64(&start, reg) << std::dec;
                        std::cout << ", f12-f19";
                        for (int f = 12; f < 20; ++f)
                            std::cout << " " << start.f[f];
                        std::cout << std::endl;
                        for (const std::string &d : fd)
                            std::cout << "    " << d << std::endl;
                    }
                }
            }
            std::cout << "[difftest] " << p.name << ": " << ok << "/" << files.size() << " exact";
            if (fuzzThis)
                std::cout << ", fuzzed " << fuzzOk << "/" << fuzzRuns << " exact";
            std::cout << std::endl;
            addResult(p, files.size(), failed - failedBefore);
        }
        results += "\n}\n";
        if (onlyNames.empty())
            std::ofstream(dir / "results.json", std::ios::binary) << results;
        std::cout << "[difftest] " << functions << " function(s) tested, " << untested << " without records; " << records
                  << " record(s), " << failed << " failed" << std::endl;
        std::cout.flush();
        std::exit(static_cast<int>(std::min<size_t>(failed, 125)));
    }
}
