// Differential tests for ported functions (plan P2).
//
// Capture: MMPC_CAPTURE=<names or 0xADDRs, comma separated, or "ported"> during a normal run records
// MMPC_CAPTURE_MAX calls (default 8) of each listed function, every MMPC_CAPTURE_EVERY-th call (default 1,
// so the first ones; "exp" takes calls 1, 8, 64, 512 ... so busy and rare functions both spread over the run),
// counted from call MMPC_CAPTURE_FROM (0-based, default 0: the calls before it are not recorded),
// into MMPC_CAPTURE_DIR/<name>/<n>.mmdt:
// the CPU context at entry, all of RDRAM (deflate), the scratchpad, the disc files the game has open (a VFS1
// section: each file's descriptor, path and offset, and the next descriptor) and the guest heap's bookkeeping (a HEP1
// section: its limits and every block; older records end at the scratchpad or after VFS1).
// The replay opens those files again under the same descriptors at the same offsets before each run, so a call
// that reads the disc (a family record not in the cache) reads what it read in the game, and starts from the record's heap,
// so a call that allocates gets the block the game would have given it. Ported functions are not installed
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
// Numeric fuzz: MMPC_DIFFTEST_NUMERIC=<cases> (with MMPC_DIFFTEST set to any directory, and MMPC_DIFFTEST_NUMERIC_FUNCTIONS=<names or all>)
// calls the pure numeric platform functions (soft float, libm) with random bit patterns, as the generated function and as the port, and
// compares the result register, sp, ra and the callee-saved registers. It needs no records and runs millions of cases in seconds.
//
// Subset: MMPC_DIFFTEST_ONLY=<names> replays only those functions (results.json is then left as it was).
// The functions that load the sound driver's IOP modules (Sound_LoadModules, Sound_Init, App_ctor) are replayed by a child process
// (this program again, with MMPC_DIFFTEST_ONLY set to the function and MMPC_DIFFTEST_CHILD=1), which starts with an empty IOP.
// The ones that talk to the running driver at the program's end (Sound_Shutdown, App_dtor) are too, and their child first starts
// the driver from Sound_Init's first record.
// A replay that does not return within MMPC_DIFFTEST_HANG_SECONDS (default 60) is reported as HANG and ends the
// run: a record edited into a state the game never reaches can leave the original waiting forever. The HANG line ends
// with the run that did not return, "(original)" or "(port)"; a fuzzed one also names its variant.
// MMPC_DIFFTEST_FROM=<function>:<record file> then resumes that function after the record that hung (it is moved away) by leaving
// out the records that sort before the file. MMPC_DIFFTEST_ORIGINAL_ONLY=1 runs only the generated function (to see that a record
// that hangs hangs in the original too, not in the port).
//
// Debugging a failing record: MMPC_DIFFTEST_RANGES=<n> lists up to n differing RDRAM ranges (default 8; the ranges MMPC_DIFFTEST_CHANGES
// lists too, default 12), MMPC_DIFFTEST_DUMP=
// <address>:<words> prints those words of both runs, and MMPC_DIFFTEST_TRACE=<names> prints the argument registers of every call
// to those functions in the original run and again in the ported one (the ported run's callees are the ported ones), which shows
// what the two hand a callee (for example a float the original never doubled).
// MMPC_DIFFTEST_WATCH=<hex address> adds, to every traced call, a line with the word at that address after the call returns; the
// first call after which the original run and the port run differ points at the callee (or the code between two traced calls)
// that wrote it. MMPC_PORT_DISABLE=0xADDR,... also keeps the generated function at those addresses in the port run (for calls made
// through the guest tables, not the port's direct calls), to see which ported callee a difference comes from.
//
// Warm records: a record whose file name starts with warm<N> (for example warm33_s.mmdt) is first advanced by N calls of the original
// with the same arguments, memory and heap bookkeeping carrying over, and the replay then compares the next call. That is how a
// function that works in steps over many frames (a scene's briefing) is tested at a step that needs the objects earlier steps made.
//
// Scripted waits: the functions that wait for the vertical blank (the movie's display start, clear and resume, Main, MainLoop)
// cannot be replayed with the runtime's wait, which switches guest threads. Their runs get a stand-in for sceGsSyncV instead: it
// returns at once with alternating fields (0 first) and counts its calls, which are compared, and timer 0 reads the deterministic
// values of PS2X_TIMER0=frame from the same state in each run. A record of one of them named stop<N>_... (for example
// stop31_000.mmdt) ends both runs at the Nth wait and compares memory, the heap and the wait count there (not registers): that is how
// Main and MainLoop, which never return, are tested over N frames. The stand-in also moves time on to the Nth replay frame at its
// Nth call, as a real wait idles until the vertical blank (the IOP runs on those cycles, and the recompiled code charges more per
// frame than the port). Their two runs each get a process of their own (sideChildren: the IOP, the memory card and the RPC layer
// are not reset between two runs in one process); MainLoop's processes first start the sound driver.
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
#include "miniz.h"
#include "sdk.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
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

// The generated function table's range (generated/register_functions.cpp).
extern const uint32_t g_ps2RecompiledFunctionTableBase;
extern const uint32_t g_ps2RecompiledFunctionTableEnd;

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

        // Functions captured by address that no table names (an SDK function the platform layer does not cover, a runtime handler's
        // wrapper): named fn_<address>.
        std::map<uint32_t, std::string> g_addressNames;

        const char *gameName(uint32_t address)
        {
            for (const mm::GameFunction &f : mm::kGameFunctions)
                if (f.address == address)
                    return f.name;
            const auto it = g_addressNames.find(address);
            return it != g_addressNames.end() ? it->second.c_str() : nullptr;
        }

        bool gameAddress(const std::string &item, uint32_t &address)
        {
            if (item.rfind("0x", 0) == 0 || item.rfind("0X", 0) == 0)
            {
                address = static_cast<uint32_t>(std::stoul(item, nullptr, 16));
                if (gameName(address) == nullptr)
                {
                    char name[24];
                    std::snprintf(name, sizeof(name), "fn_%08X", address);
                    g_addressNames[address] = name;
                }
                return true;
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
        uint64_t g_captureFrom = 0;  // MMPC_CAPTURE_FROM: calls before this one (0-based) are not recorded

        bool sampleCall(uint64_t call)
        {
            if (call < g_captureFrom)
                return false;
            call -= g_captureFrom;
            if (g_captureEvery != 0)
                return call % g_captureEvery == 0;
            // call is 0-based: record calls 1, 8, 64 ... (counted from 1), the powers of 8.
            const uint64_t n = call + 1;
            return (n & (n - 1)) == 0 && std::countr_zero(n) % 3 == 0;
        }

        // A disc file the game had open when a record was captured: the descriptor the runtime gave it, its path and where it
        // was. The family file is opened at the start and stays open, its reads queued and issued a frame later, so a replayed
        // call that reads a family seeks and reads a descriptor the replay process never opened (the original then loops on
        // sceLseek's failure). Only the read-only devices are kept; a memory card file is never opened again by a replay.
        struct OpenDiscFile
        {
            int32_t descriptor = -1;
            int64_t offset = 0;
            std::string path;
        };
        constexpr char kFilesMagic[4] = {'V', 'F', 'S', '1'}; // the optional sections after the scratchpad
        // The guest heap's bookkeeping is host-side too: without it a replayed call allocates from the replay process's own heap,
        // whose free blocks can lie on objects that were live in the game (a constructor's big allocation then overwrites the
        // building types it reads next). HEP1 carries the heap's limits and every block.
        constexpr char kHeapMagic[4] = {'H', 'E', 'P', '1'};

        bool readOnlyDevice(const std::string &device)
        {
            return device == "cdrom0" || device == "cdrom" || device == "rom0";
        }

        // A record waiting to be compressed and written. The game thread only copies memory (a few ms);
        // a writer thread does the slow part, so captures barely slow the game down.
        struct PendingRecord
        {
            std::filesystem::path path;
            R5900Context ctx;
            std::vector<uint8_t> rdram;
            std::vector<uint8_t> scratchpad;
            std::vector<OpenDiscFile> files;
            int32_t nextDescriptor = 0;
            PS2Runtime::GuestHeapState heap;
        };

        std::mutex g_queueMutex;
        std::condition_variable g_queueChanged;
        std::deque<std::unique_ptr<PendingRecord>> g_queue;
        constexpr size_t kMaxPending = 12; // 32 MB each

        void writeRecord(const PendingRecord &rec)
        {
            // Raw deflate (no zlib header), as the records have always been written.
            size_t packedLength = 0;
            void *packed = tdefl_compress_mem_to_heap(rec.rdram.data(), rec.rdram.size(), &packedLength, TDEFL_DEFAULT_MAX_PROBES);
            const int packedSize = static_cast<int>(packedLength);
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
                // VFS1: the next descriptor, the count, then per file its descriptor, offset, path length and path.
                const uint32_t counts[2] = {static_cast<uint32_t>(rec.nextDescriptor), static_cast<uint32_t>(rec.files.size())};
                out.write(kFilesMagic, 4);
                out.write(reinterpret_cast<const char *>(counts), sizeof(counts));
                for (const OpenDiscFile &file : rec.files)
                {
                    const uint32_t length = static_cast<uint32_t>(file.path.size());
                    out.write(reinterpret_cast<const char *>(&file.descriptor), sizeof(file.descriptor));
                    out.write(reinterpret_cast<const char *>(&file.offset), sizeof(file.offset));
                    out.write(reinterpret_cast<const char *>(&length), sizeof(length));
                    out.write(file.path.data(), length);
                }
                // HEP1: base, end, limit, suggested base, configured, the block count, then per block its address, size and free flag.
                const uint32_t limits[6] = {rec.heap.base, rec.heap.end, rec.heap.limit, rec.heap.suggestedBase,
                                            rec.heap.configured ? 1u : 0u, static_cast<uint32_t>(rec.heap.blocks.size())};
                out.write(kHeapMagic, 4);
                out.write(reinterpret_cast<const char *>(limits), sizeof(limits));
                for (const PS2Runtime::GuestHeapBlock &block : rec.heap.blocks)
                {
                    const uint32_t words[3] = {block.addr, block.size, block.free ? 1u : 0u};
                    out.write(reinterpret_cast<const char *>(words), sizeof(words));
                }
            }
            mz_free(packed);
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

        void queueRecord(const std::filesystem::path &path, const R5900Context &ctx, PS2Runtime &runtime)
        {
            auto rec = std::make_unique<PendingRecord>();
            rec->path = path;
            rec->ctx = ctx;
            rec->rdram.assign(g_rdram, g_rdram + PS2_RAM_SIZE);
            rec->scratchpad.assign(g_scratchpad, g_scratchpad + PS2_SCRATCHPAD_SIZE);
            PS2Vfs &vfs = runtime.vfs();
            rec->nextDescriptor = vfs.nextDescriptor();
            for (const PS2VfsDescriptorInfo &open : vfs.descriptors())
                if (readOnlyDevice(open.device))
                {
                    const int64_t offset = vfs.seek(open.descriptor, 0, SEEK_CUR);
                    if (offset >= 0)
                        rec->files.push_back({open.descriptor, offset, open.path});
                }
            std::sort(rec->files.begin(), rec->files.end(),
                      [](const OpenDiscFile &a, const OpenDiscFile &b) { return a.descriptor < b.descriptor; });
            rec->heap = runtime.saveGuestHeapState();
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
                queueRecord(dir / file, *ctx, *runtime);
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
            std::vector<OpenDiscFile> files; // records captured before the VFS1 section have none
            int32_t nextDescriptor = 0;
            bool hasFiles = false;
            PS2Runtime::GuestHeapState heap; // records captured before the HEP1 section start from the replay's own heap
            bool hasHeap = false;
        };

        // The HEP1 section (its magic already read).
        bool readHeap(std::ifstream &in, Record &rec, std::string &error)
        {
            uint32_t limits[6];
            if (!in.read(reinterpret_cast<char *>(limits), sizeof(limits)) || limits[5] > (1u << 20))
            {
                error = "bad heap section";
                return false;
            }
            rec.heap.base = limits[0];
            rec.heap.end = limits[1];
            rec.heap.limit = limits[2];
            rec.heap.suggestedBase = limits[3];
            rec.heap.configured = limits[4] != 0;
            rec.heap.blocks.resize(limits[5]);
            for (PS2Runtime::GuestHeapBlock &block : rec.heap.blocks)
            {
                uint32_t words[3];
                if (!in.read(reinterpret_cast<char *>(words), sizeof(words)))
                {
                    error = "bad heap section";
                    return false;
                }
                block = {words[0], words[1], words[2] != 0};
            }
            rec.hasHeap = true;
            return true;
        }

        // The optional VFS1 section (its magic already read).
        bool readFiles(std::ifstream &in, Record &rec, std::string &error)
        {
            uint32_t counts[2];
            if (!in.read(reinterpret_cast<char *>(counts), sizeof(counts)) || counts[1] > 4096)
            {
                error = "bad open-file section";
                return false;
            }
            rec.nextDescriptor = static_cast<int32_t>(counts[0]);
            for (uint32_t i = 0; i < counts[1]; ++i)
            {
                OpenDiscFile file;
                uint32_t length = 0;
                in.read(reinterpret_cast<char *>(&file.descriptor), sizeof(file.descriptor));
                in.read(reinterpret_cast<char *>(&file.offset), sizeof(file.offset));
                in.read(reinterpret_cast<char *>(&length), sizeof(length));
                if (!in || length > 4096)
                {
                    error = "bad open-file section";
                    return false;
                }
                file.path.resize(length);
                if (!in.read(file.path.data(), length))
                {
                    error = "bad open-file section";
                    return false;
                }
                rec.files.push_back(std::move(file));
            }
            rec.hasFiles = true;
            return true;
        }

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
            // The optional sections after the scratchpad, in any order (older records end at the scratchpad, or after VFS1).
            for (char section[4]; in.read(section, 4);)
            {
                const bool files = std::memcmp(section, kFilesMagic, 4) == 0, heap = std::memcmp(section, kHeapMagic, 4) == 0;
                if (!files && !heap)
                {
                    error = "unknown section after the scratchpad";
                    return false;
                }
                if (!(files ? readFiles(in, rec, error) : readHeap(in, rec, error)))
                    return false;
            }
            rec.rdram.resize(PS2_RAM_SIZE);
            const size_t size = tinfl_decompress_mem_to_mem(rec.rdram.data(), rec.rdram.size(), packed.data(), packed.size(), 0);
            if (size != PS2_RAM_SIZE)
            {
                error = "bad RDRAM image";
                return false;
            }
            return true;
        }

        // Opens the record's disc files again under their own descriptors (open() hands out the counter, which closeFrom sets)
        // and moves each to where it was, then sets the counter to the game's. Descriptors the replay process itself holds
        // (below `base`) are never touched; a file that cannot be put back is reported once.
        void restoreFiles(PS2Runtime &runtime, const Record &rec, int32_t base)
        {
            PS2Vfs &vfs = runtime.vfs();
            const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
            const PS2VfsMounts mounts{paths.hostRoot, paths.cdRoot, paths.mcRoot};
            static std::set<std::string> reported;
            for (const OpenDiscFile &file : rec.files)
            {
                int32_t got = -1;
                if (file.descriptor >= base)
                {
                    vfs.closeFrom(file.descriptor);
                    got = vfs.open(file.path, PS2_FIO_O_RDONLY, mounts, runtime.romDevice());
                }
                if (got != file.descriptor || vfs.seek(got, file.offset, SEEK_SET) != file.offset)
                {
                    if (reported.insert(file.path).second)
                        std::cout << "[difftest] could not open " << file.path << " again as descriptor " << file.descriptor << std::endl;
                    if (got >= 0)
                        vfs.closeFrom(got);
                }
            }
            vfs.closeFrom(std::max(rec.nextDescriptor, base));
        }

        // After a warm call: the files' offsets and the counter as that call left them, so the next call starts there.
        void keepFiles(PS2Runtime &runtime, Record &rec)
        {
            PS2Vfs &vfs = runtime.vfs();
            for (OpenDiscFile &file : rec.files)
            {
                const int64_t offset = vfs.seek(file.descriptor, 0, SEEK_CUR);
                if (offset >= 0)
                    file.offset = offset;
            }
            rec.nextDescriptor = vfs.nextDescriptor();
        }

        struct Outcome
        {
            R5900Context ctx;
            std::vector<uint8_t> rdram;
            std::vector<uint8_t> scratchpad;
            std::vector<std::pair<uint32_t, uint32_t>> liveHeap;  // (address, size) of the guest heap blocks in use afterwards
            uint32_t syncVCalls = 0; // calls of the scripted vsync wait (scriptedWaits), compared too
            uint32_t overruns = 0;   // of those, the ones reached after the replay frame's time (see syncVStandIn)
            bool stopped = false;    // ended at the record's stop<N> wait
            bool returned = false;
            bool switched = false;
            std::string fault;
        };

        // Scripted vsync waits. sceGsSyncV is the runtime's vsync wait: the EE scheduler blocks the thread until the next vertical
        // blank, delivers the interrupts in between and runs the other guest threads, which a replay cannot do (the call switches
        // threads and the run ends there). For the functions in scriptedWaits() both runs get a stand-in at its address instead: it
        // returns at once with the field the runtime's wait gives an interlaced display, alternating and starting from field 0 in
        // every run, and counts its calls. The two runs then see the same waits, and the comparison covers the function's own logic
        // and how often it waited, not the wait itself (runtime code).
        // A scripted run also reads the deterministic timer 0 (patch 0154, PS2X_TIMER0=frame) from the same state each time (the
        // game takes the count as a random number), and a record named stop<N>_... ends both runs at the Nth wait: the stand-in
        // throws ReplayStop there, which unwinds the generated and the ported frames alike (the replay runs them on the harness's
        // own stack), and the two runs are compared at that point. That is how the functions that never return (Main, MainLoop)
        // are tested over N frames. Registers are not compared after a stop, only memory, the heap and the wait count.
        struct ReplayStop
        {
        };
        int g_syncVField = 0;
        uint32_t g_syncVCalls = 0;
        uint32_t g_syncVStopAt = 0; // 0: never
        bool g_scriptedRun = false;
        // Time. The IOP runs on the EE cycles the code charges (EeScheduler::accountCycles), and the recompiled code charges far
        // more per frame than the port; a real vsync wait idles until the vertical blank, which evens that out. So the stand-in
        // moves time on to the Nth replay frame after the run's start at its Nth call (MMPC_DIFFTEST_FRAME_CYCLES, default one
        // NTSC field): both runs then give the IOP the same time per frame. A frame whose own cycles pass that point (an
        // overrun) leaves the two runs' clocks apart; the overruns are counted and shown with a failing record.
        uint64_t g_syncVStartCycle = 0;
        uint32_t g_syncVOverruns = 0;

        uint64_t replayFrameCycles()
        {
            static const uint64_t cycles = [] {
                const char *value = std::getenv("MMPC_DIFFTEST_FRAME_CYCLES");
                return value != nullptr && value[0] != '\0' ? std::strtoull(value, nullptr, 0) : EeScheduler::kEeClockHz * 1001ull / 60000ull;
            }();
            return cycles;
        }

        void syncVStandIn(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
        {
            ctx->pc = static_cast<uint32_t>(gpr64(ctx, 31));
            setGpr64(ctx, 2, static_cast<uint64_t>(g_syncVField));
            g_syncVField ^= 1;
            ++g_syncVCalls;
            EeScheduler &ee = runtime->eeScheduler();
            const uint64_t target = g_syncVStartCycle + static_cast<uint64_t>(g_syncVCalls) * replayFrameCycles();
            uint64_t now = ee.cycleCount();
            if (now > target)
                ++g_syncVOverruns;
            while (now < target)
            {
                const uint32_t step = static_cast<uint32_t>(std::min<uint64_t>(target - now, 1u << 20));
                ee.accountCycles(step);
                now += step;
            }
            if (g_syncVCalls == g_syncVStopAt)
                throw ReplayStop{};
        }

        const std::set<std::string> &scriptedWaits()
        {
            static const std::set<std::string> names = {"Movie_DisplayStart", "Movie_ClearScreen", "Movie_ResumeAll", "MainLoop", "Main"};
            return names;
        }

        // The heap bookkeeping every record starts from (the state when the first replay began).
        const PS2Runtime::GuestHeapState &baseHeapState(PS2Runtime &runtime)
        {
            static const PS2Runtime::GuestHeapState baseHeap = runtime.saveGuestHeapState();
            return baseHeap;
        }

        void runOnce(PS2Runtime &runtime, GuestFn fn, uint32_t address, const Record &rec, const R5900Context &start, Outcome &out,
                     const PS2Runtime::GuestHeapState &heap)
        {
            // The runtime keeps the heap's bookkeeping and the open file descriptors outside RDRAM. Every run starts
            // from the same ones, so a call that allocates or opens gets the same block and descriptor each time.
            // The EE timers too: every run starts from their state before the first replay (a record of the game's start-up,
            // App_ctor, starts timer 0, which the game reads as a random number; the runtime's own timers keep running).
            static const int32_t baseDescriptor = runtime.vfs().nextDescriptor();
            static const auto baseTimers = runtime.memory().saveEeTimers();
            runtime.restoreGuestHeapState(heap);
            runtime.vfs().closeFrom(baseDescriptor);
            if (rec.hasFiles)
                restoreFiles(runtime, rec, baseDescriptor);
            runtime.memory().restoreEeTimers(baseTimers);
            static const PS2Memory::DeterministicTimer0State baseTimer0 = PS2Memory::saveDeterministicTimer0();
            PS2Memory::restoreDeterministicTimer0(g_scriptedRun ? PS2Memory::DeterministicTimer0State{true, 0u, 0u} : baseTimer0);
            g_syncVField = 0;
            g_syncVCalls = 0;
            g_syncVOverruns = 0;
            g_syncVStartCycle = runtime.eeScheduler().cycleCount();
            out.stopped = false;
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
            catch (const ReplayStop &)
            {
                out.stopped = true;
            }
            runtime.eeScheduler().leaveNoPreempt();
            out.syncVCalls = g_syncVCalls;
            out.overruns = g_syncVOverruns;
            out.liveHeap.clear();
            for (const PS2Runtime::GuestHeapBlock &block : runtime.saveGuestHeapState().blocks)
                if (!block.free)
                    out.liveHeap.emplace_back(block.addr, block.size);
            std::sort(out.liveHeap.begin(), out.liveHeap.end());
            out.returned = !out.switched && out.fault.empty() && (out.stopped || out.ctx.pc == kReturnSentinel || out.ctx.pc == address);
            out.rdram.assign(g_rdram, g_rdram + PS2_RAM_SIZE);
            out.scratchpad.assign(g_scratchpad, g_scratchpad + PS2_SCRATCHPAD_SIZE);
        }

        // MMPC_DIFFTEST_TRACE=<names> prints the argument registers (a0-a3, t0-t3, f12-f13, sp) of every call to those functions
        // during a replay, in the original run and again in the port run, to compare what the two hand them.
        struct Traced
        {
            const char *name;
            GuestFn target;
        };
        std::unordered_map<uint32_t, Traced> g_traced;

        void tracedEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const Traced &t = g_traced.at(ctx->pc);
            std::fprintf(stderr, "TR %s a0=%llx a1=%llx a2=%llx a3=%llx t0=%llx t1=%llx t2=%llx t3=%llx f12=%08x f13=%08x sp=%llx\n", t.name,
                         static_cast<unsigned long long>(gpr64(ctx, 4) & 0xFFFFFFFFu), static_cast<unsigned long long>(gpr64(ctx, 5) & 0xFFFFFFFFu),
                         static_cast<unsigned long long>(gpr64(ctx, 6) & 0xFFFFFFFFu), static_cast<unsigned long long>(gpr64(ctx, 7) & 0xFFFFFFFFu),
                         static_cast<unsigned long long>(gpr64(ctx, 8) & 0xFFFFFFFFu), static_cast<unsigned long long>(gpr64(ctx, 9) & 0xFFFFFFFFu),
                         static_cast<unsigned long long>(gpr64(ctx, 10) & 0xFFFFFFFFu), static_cast<unsigned long long>(gpr64(ctx, 11) & 0xFFFFFFFFu),
                         *reinterpret_cast<const uint32_t *>(&ctx->f[12]), *reinterpret_cast<const uint32_t *>(&ctx->f[13]),
                         static_cast<unsigned long long>(gpr64(ctx, 29) & 0xFFFFFFFFu));
            t.target(rdram, ctx, runtime);
            // MMPC_DIFFTEST_WATCH=<hex address> prints the word there after every traced call returns (where it first differs between
            // the original run and the port run points at the callee that wrote it).
            if (const char *watch = std::getenv("MMPC_DIFFTEST_WATCH"))
            {
                const uint32_t address = static_cast<uint32_t>(std::strtoul(watch, nullptr, 16));
                std::fprintf(stderr, "WATCH %s %08x\n", t.name, *reinterpret_cast<const uint32_t *>(rdram + (address & PS2_RAM_MASK)));
            }
        }

        // Points every ported address at its generated function (true) or back at the port (false).
        void useGenerated(PS2Runtime &runtime, bool generated)
        {
            static const std::vector<std::string> traced = env("MMPC_DIFFTEST_TRACE") ? splitList(env("MMPC_DIFFTEST_TRACE")) : std::vector<std::string>{};
            // MMPC_PORT_DISABLE=0xADDR,... keeps the generated functions at those addresses in the port run too (to find which ported
            // callee a difference comes from; it works for the calls made through the guest tables, not the port's direct calls).
            static const std::vector<std::string> kept = env("MMPC_PORT_DISABLE") ? splitList(env("MMPC_PORT_DISABLE")) : std::vector<std::string>{};
            static const std::set<uint32_t> keptAddresses = [] {
                std::set<uint32_t> out;
                for (const std::string &item : kept)
                    if (!item.empty() && item != "all")
                        out.insert(static_cast<uint32_t>(std::stoul(item, nullptr, 0)));
                return out;
            }();
            for (const Ported &p : ported())
            {
                const GuestFn target = generated || keptAddresses.count(p.address) != 0 ? p.original : p.wrapper;
                if (!traced.empty() && std::find(traced.begin(), traced.end(), std::string(p.name)) != traced.end())
                {
                    g_traced[p.address] = {p.name, target};
                    runtime.registerFunction(p.address, tracedEntry);
                }
                else
                    runtime.registerFunction(p.address, target);
            }
        }

        // The functions that talk to the sound driver only at the program's end, when it has run since start-up (Sound_Shutdown,
        // App_dtor, which calls it; Main's loop never ends, so their records are made): replayed in a child process that first
        // starts the driver (startSoundDriver), so their RPC reaches it as in the game rather than finding no server.
        const std::set<std::string> &afterSoundInit()
        {
            // MainLoop too: its record is taken at boot with the driver running, and its frames send the sound commands.
            static const std::set<std::string> names = {"Sound_Shutdown", "App_dtor", "MainLoop"};
            return names;
        }

        // Sound_Init's first record through the original code: it loads the driver's modules into the IOP and starts the driver
        // over its RPC. For a child process, whose IOP is empty. True when that call returned.
        bool startSoundDriver(PS2Runtime &runtime, const std::filesystem::path &dir)
        {
            const auto init = std::find_if(ported().begin(), ported().end(),
                                           [](const Ported &p) { return std::strcmp(p.name, "Sound_Init") == 0; });
            const std::filesystem::path sub = dir / "Sound_Init";
            if (init == ported().end() || !std::filesystem::is_directory(sub))
                return false;
            std::vector<std::filesystem::path> files;
            for (const auto &entry : std::filesystem::directory_iterator(sub))
                if (entry.path().extension() == ".mmdt")
                    files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            Record rec;
            std::string error;
            if (files.empty() || !readRecord(files.front(), rec, error))
                return false;
            Outcome out;
            useGenerated(runtime, true);
            runOnce(runtime, init->original, init->address, rec, rec.ctx, out, rec.hasHeap ? rec.heap : baseHeapState(runtime));
            return out.returned;
        }

        bool sameReg(const R5900Context &a, const R5900Context &b, int reg)
        {
            return std::memcmp(&a.r[reg], &b.r[reg], sizeof(a.r[reg])) == 0;
        }

        // Differences as "what: original vs ported" lines (empty when the runs agree).
        std::vector<std::string> compare(const Ported &p, const Record &rec, const Outcome &a, const Outcome &b)
        {
            std::vector<std::string> diffs;
            const std::string fn = p.name; // compared as text (p.name is a const char *)
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
            if (a.syncVCalls != b.syncVCalls)
                diffs.push_back("sceGsSyncV calls: " + std::to_string(a.syncVCalls) + " vs " + std::to_string(b.syncVCalls));
            if (a.stopped != b.stopped)
                diffs.push_back(std::string("stopped at the record's wait: original ") + (a.stopped ? "yes" : "no") + ", ported " + (b.stopped ? "yes" : "no"));
            // Both runs stopped in the middle of the function (stop<N> records): its registers hold whatever each was doing there.
            const bool midCall = a.stopped && b.stopped;
            if (!midCall && p.result == ResultKind::Int && gpr64(&a.ctx, 2) != gpr64(&b.ctx, 2))
                diffs.push_back("v0: " + hex(gpr64(&a.ctx, 2)) + " vs " + hex(gpr64(&b.ctx, 2)));
            if (!midCall && p.result == ResultKind::Float && std::memcmp(&a.ctx.f[0], &b.ctx.f[0], 4) != 0)
                diffs.push_back("f0: " + std::to_string(a.ctx.f[0]) + " vs " + std::to_string(b.ctx.f[0]));
            static const int kSaved[] = {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};
            for (int reg : kSaved)
                if (!midCall && !sameReg(a.ctx, b.ctx, reg))
                    diffs.push_back("r" + std::to_string(reg) + ": " + hex(gpr64(&a.ctx, reg)) + " vs " + hex(gpr64(&b.ctx, reg)));
            for (int f = 20; f < 32; ++f)
                if (!midCall && std::memcmp(&a.ctx.f[f], &b.ctx.f[f], 4) != 0)
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
            // do not) decides the final lanes. The game never reads them back: everything else is still compared. Person_Update
            // is the same through Path_NextWaypoint (its waypoint vectors' unwritten lanes); the failing records differ in no
            // other register or byte.
            static const std::set<std::string> kVu0FromLeftovers = {"TalkPersonFar_ctor", "TalkPersonNear_ctor", "Person_ChooseDestination", "CityCursor_Update",
                                                                    "Person_Update"};
            const bool vu0Compared = kVu0FromLeftovers.count(p.name) == 0 && !midCall;
            // Narrower: only vf4's x for functions whose last VU0 work is the square root of a schedule's spread
            // (Math_SqrtBits, inlined in Schedule_MemberActivity): it converts a 16-byte stack slot of which only x is
            // written, so vf4's y, z and w are what lay on the stack there. In a deeper call that is what the generated
            // callees' register saves left (an sq writes a register's upper half, 0), and the ported callees save nothing
            // on the guest stack. TalkView_DrawBuilding reaches it by family, through TalkView_AllAsleep after Family_Get.
            // The VU0 liveness scan found no function that reads vf4 before writing it.
            static const std::set<std::string> kVf4FromSqrtSlot = {"TalkView_DrawBuilding"};
            const size_t vf4Bytes = kVf4FromSqrtSlot.count(p.name) != 0 ? 4 : sizeof(a.ctx.vu0_vf[4]);
            for (int v = 1; v < 32 && vu0Compared; ++v)
                if (std::memcmp(&a.ctx.vu0_vf[v], &b.ctx.vu0_vf[v], v == 4 ? vf4Bytes : sizeof(a.ctx.vu0_vf[v])) != 0)
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
            if (fn == "Movie_ViBufCreate")
                ignoreLow = (object + 0x40u) & PS2_RAM_MASK;
            else if (fn == "Movie_VideoDecCreate")
                ignoreLow = (object + 0x88u) & PS2_RAM_MASK;
            else if (fn == "Movie_AudioDecCreate")
            {
                ignoreLow = (object + 0x44u) & PS2_RAM_MASK;
                ignoreLow2 = (object + 0x58u) & PS2_RAM_MASK;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            else if (fn == "Movie_StrFileOpen")
                ignoreLow = (object + 0x2Cu) & PS2_RAM_MASK;
            // Sound_Init (and App_ctor, which calls it) binds the sound driver's RPC client: the runtime numbers the binds (the client's
            // id at +4) and keeps the server descriptor it made on the first bind (its sid at 0x1F10000) outside the replay's reset, so the
            // ported run sees them differ.
            else if (fn == "Sound_Init" || fn == "App_ctor" || fn == "Main")
            {
                ignoreLow = 0x001FE694u;
                ignoreLow2 = 0x01F10000u;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            // supplement_crt0 (and _InitSys, which calls it) makes two semaphores and keeps their ids at 0x1AC494 and 0x1AC498: the kernel
            // numbers them up and the replay does not reset it, so the ported run gets the next two ids.
            else if (fn == "sdk_supplement_crt0" || fn == "sdk_InitSys")
            {
                ignoreLow = 0x001AC494u;
                ignoreLow2 = 0x001AC498u;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            // McManager_NewSaveName writes the console clock in seconds as digits: the two runs read it a moment apart, so the last
            // digits (bytes 16 to 23 of the name slot, which holds eight digits after the id string) are not compared.
            else if (fn == "McManager_NewSaveName")
            {
                const uint32_t name = (0x001F3CF0u + (static_cast<uint32_t>(gpr64(&rec.ctx, 5)) & 0xFFu) * 0x20u) & PS2_RAM_MASK;
                ignoreLow = name + 0x10u;
                ignoreLow2 = name + 0x14u;
                ignoreHigh2 = ignoreLow2 + 4u;
            }
            // McSave_WriteIconSys (and the save state and SaveFile_WriteIconSys, which call it) opens a file on the card: the runtime's
            // descriptor counter (kept outside RDRAM and not reset between the two runs) gives the ported run the next number, which the game
            // keeps in three words: 0x1BDCDC, and the first argument of the write and the close it queues in the two card queue slots after
            // the open's (64 slots of 0x24 bytes at 0x1F5CF0; the open goes in at the queue's tail, the word at 0x1BDCD4).
            else if (fn == "McSave_WriteIconSys" || fn == "PauseMenu_StateSave" || fn == "SaveFile_WriteIconSys")
            {
                uint32_t tail = 0;
                std::memcpy(&tail, rec.rdram.data() + 0x001BDCD4u, sizeof(tail));
                const auto fileWord = [tail](uint32_t after) { return 0x001F5CF0u + ((tail + after) % 64u) * 0x24u + 0x10u; };
                ignoreLow = 0x001BDCDCu;
                ignoreLow2 = fileWord(1);
                ignoreHigh2 = ignoreLow2 + 4u;
                ignoreLow3 = fileWord(2);
                ignoreHigh3 = ignoreLow3 + 4u;
            }
            if (ignoreLow != 0)
                ignoreHigh = ignoreLow + 4u;
            // The pause menu's save slot list (and the new slot it prepares) names a new save for the first free slot of each card, with
            // the clock digits NewSaveName writes: the clock's tick between the two runs changes the last digit of any of the 16 names.
            const bool newSaveNames = fn == "PauseMenu_Update" || fn == "PauseMenu_StateSlotSelect" || fn == "McManager_PrepareNewSlot";
            // The card scan's last directory entry (a CardDirEntry at 0x1F4300): the runtime gives a folder's "." and ".." entries the host
            // clock's time at the call (sceMcGetDir), which ticks between the two runs. A scan of an empty card ends on one (the second slot
            // of a test copy of the card), so the entry's two times (+0x00 to +0x0F) are not compared when both runs end on one.
            const auto dotEntry = [](const Outcome &o) {
                const uint8_t *name = o.rdram.data() + 0x001F4320u;
                return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
            };
            const bool scanEntryClock = dotEntry(a) && dotEntry(b);
            // MMPC_DIFFTEST_IGNORE=<hex address>:<bytes>,... leaves those ranges out of the comparison (to check that nothing else differs).
            static const std::vector<std::pair<uint32_t, uint32_t>> extraIgnored = [] {
                std::vector<std::pair<uint32_t, uint32_t>> out;
                if (const char *list = env("MMPC_DIFFTEST_IGNORE"))
                    for (const std::string &item : splitList(list))
                    {
                        unsigned long address = 0, bytes = 0;
                        if (std::sscanf(item.c_str(), "%lx:%lu", &address, &bytes) == 2)
                            out.emplace_back(static_cast<uint32_t>(address), static_cast<uint32_t>(bytes));
                    }
                return out;
            }();
            // CityScene_Update (and GameUpdate, which calls it) leaves two quadwords of this frame's packet tables (the 16 bytes at
            // 0x88BAA0 and 0x88BCC0 in the explore runs, 0x88BDF0 and 0x88C010 in the new game ones) different between the generated and
            // the ported run. Each is the end of a table of unit vectors: three lanes zero and the first one a stale stack word. Under the
            // generated code it looks like a saved register (GameUpdate's records show the app pointer and a code address), under the port the
            // old content, since a ported function writes no register spills for the frames below it to read. The slots look like padding;
            // nothing else differs.
            const bool playerSlots = fn == "CityScene_Update" || fn == "GameUpdate";
            // sceSdRemote copies all six argument registers into its argument buffer, the unused ones too: a caller that passes fewer
            // leaves whatever its registers held (the recompiled callee's own leftovers, which a native callee does not leave), so
            // arguments 3 to 5 (the words at +0x10 to +0x1B) differ in every function that calls it. Nothing reads them; the sound
            // driver ignores the arguments a command does not use. sdk_sceSdRemote's own records pass all six.
            const bool sdRemoteLeftovers = fn != "sdk_sceSdRemote";
            const auto same = [&](uint32_t k) {
                if (sdRemoteLeftovers && k >= 0x001C3510u && k < 0x001C351Cu)
                    return true;
                for (const auto &range : extraIgnored)
                    if (k >= range.first && k < range.first + range.second)
                        return true;
                if (playerSlots && ((k >= 0x0088BAA0u && k < 0x0088BAB0u) || (k >= 0x0088BCC0u && k < 0x0088BCD0u) ||
                                    (k >= 0x0088BDF0u && k < 0x0088BE00u) || (k >= 0x0088C010u && k < 0x0088C020u)))
                    return true;
                if (newSaveNames && k >= 0x001F3CF0u && k < 0x001F3CF0u + 0x200u && ((k - 0x001F3CF0u) & 0x1Fu) >= 0x10u && ((k - 0x001F3CF0u) & 0x1Fu) < 0x18u)
                    return true;
                if (scanEntryClock && k >= 0x001F4300u && k < 0x001F4310u)
                    return true;
                return a.rdram[k] == b.rdram[k] || (k >= ignoreLow && k < ignoreHigh) || (k >= ignoreLow2 && k < ignoreHigh2) ||
                       (k >= ignoreLow3 && k < ignoreHigh3);
            };

            const uint32_t sp = static_cast<uint32_t>(gpr64(&rec.ctx, 29)) & PS2_RAM_MASK;
            const uint32_t freeLow = sp > kFreeStackBytes ? sp - kFreeStackBytes : 0u;
            size_t reported = 0, total = 0;
            // MMPC_DIFFTEST_RANGES=<n> lists up to n differing ranges of a failing record (default 8).
            const size_t maxRanges = env("MMPC_DIFFTEST_RANGES") != nullptr ? static_cast<size_t>(std::strtoul(env("MMPC_DIFFTEST_RANGES"), nullptr, 10)) : 8u;
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
                if (reported++ < maxRanges)
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
            if (reported > maxRanges)
                diffs.push_back("RDRAM: " + std::to_string(reported) + " differing ranges, " + std::to_string(total) + " bytes");
            // MMPC_DIFFTEST_DUMP=<address>:<words> adds those words (hex and float) of both runs to a failing record.
            if (const char *dump = env("MMPC_DIFFTEST_DUMP"); dump != nullptr && !diffs.empty())
            {
                unsigned long address = 0, words = 0;
                if (std::sscanf(dump, "%lx:%lu", &address, &words) == 2)
                {
                    for (const auto &block : a.liveHeap)
                        if (address >= block.first && address < block.first + block.second)
                            diffs.push_back("dump: " + hex(static_cast<uint32_t>(address)) + " is in the heap block " + hex(block.first) + "+" + hex(block.second));
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
            if (!diffs.empty() && (a.overruns != 0 || b.overruns != 0))
                diffs.push_back("note: waits reached after the replay frame's time: original " + std::to_string(a.overruns) + ", port " +
                                std::to_string(b.overruns) + " (MMPC_DIFFTEST_FRAME_CYCLES)");
            return diffs;
        }

        // A run's outcome as a file, for the functions whose two runs each get a process of their own (sideChildren): the child
        // writes it, the parent reads both and compares.
        constexpr char kOutcomeMagic[4] = {'M', 'M', 'O', 'C'};

        bool writeOutcome(const std::filesystem::path &path, const Outcome &out)
        {
            std::ofstream file(path, std::ios::binary);
            const uint32_t counts[6] = {out.syncVCalls, out.overruns, static_cast<uint32_t>(out.liveHeap.size()),
                                        static_cast<uint32_t>(out.fault.size()),
                                        (out.returned ? 1u : 0u) | (out.switched ? 2u : 0u) | (out.stopped ? 4u : 0u),
                                        static_cast<uint32_t>(sizeof(R5900Context))};
            file.write(kOutcomeMagic, 4);
            file.write(reinterpret_cast<const char *>(counts), sizeof(counts));
            file.write(reinterpret_cast<const char *>(&out.ctx), sizeof(R5900Context));
            file.write(reinterpret_cast<const char *>(out.rdram.data()), static_cast<std::streamsize>(out.rdram.size()));
            file.write(reinterpret_cast<const char *>(out.scratchpad.data()), static_cast<std::streamsize>(out.scratchpad.size()));
            for (const auto &block : out.liveHeap)
            {
                const uint32_t words[2] = {block.first, block.second};
                file.write(reinterpret_cast<const char *>(words), sizeof(words));
            }
            file.write(out.fault.data(), static_cast<std::streamsize>(out.fault.size()));
            return static_cast<bool>(file);
        }

        bool readOutcome(const std::filesystem::path &path, Outcome &out)
        {
            std::ifstream file(path, std::ios::binary);
            char magic[4];
            uint32_t counts[6];
            if (!file.read(magic, 4) || std::memcmp(magic, kOutcomeMagic, 4) != 0 || !file.read(reinterpret_cast<char *>(counts), sizeof(counts)) ||
                counts[5] != sizeof(R5900Context) || counts[2] > (1u << 20) || counts[3] > 4096)
                return false;
            out.syncVCalls = counts[0];
            out.overruns = counts[1];
            out.returned = (counts[4] & 1u) != 0;
            out.switched = (counts[4] & 2u) != 0;
            out.stopped = (counts[4] & 4u) != 0;
            out.rdram.resize(PS2_RAM_SIZE);
            out.scratchpad.resize(PS2_SCRATCHPAD_SIZE);
            file.read(reinterpret_cast<char *>(&out.ctx), sizeof(R5900Context));
            file.read(reinterpret_cast<char *>(out.rdram.data()), PS2_RAM_SIZE);
            file.read(reinterpret_cast<char *>(out.scratchpad.data()), PS2_SCRATCHPAD_SIZE);
            out.liveHeap.resize(counts[2]);
            for (auto &block : out.liveHeap)
            {
                uint32_t words[2];
                file.read(reinterpret_cast<char *>(words), sizeof(words));
                block = {words[0], words[1]};
            }
            out.fault.resize(counts[3]);
            file.read(out.fault.data(), counts[3]);
            return static_cast<bool>(file);
        }

        // Main and MainLoop run many frames with the IOP, the memory card and the RPC layer alive, and the replay resets none of
        // these between two runs in one process (the port run would find the files the original opened, the sound commands it
        // left queued). Each of their runs is a child process of its own, which starts the same way (MainLoop's child first
        // starts the sound driver, as for afterSoundInit).
        const std::set<std::string> &sideChildren()
        {
            static const std::set<std::string> names = {"Main", "MainLoop"};
            return names;
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
            // The first 12 ranges are listed, or as many as MMPC_DIFFTEST_RANGES says.
            static const size_t kListed = env("MMPC_DIFFTEST_RANGES") ? static_cast<size_t>(std::strtoul(env("MMPC_DIFFTEST_RANGES"), nullptr, 0)) : 12u;
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
                if (ranges++ < kListed)
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

    // ---------------------------------------------------------------- generated entries (plan P7)
    // MMPC_GENERATED_ENTRIES=<file>: every entry of the runtime's function table that is not a ported function (recompiled code, and
    // the generated wrappers of the runtime's own handlers) is wrapped and counted by address; the file is rewritten every 5 s as
    // "0xADDR calls" lines. tools/generated_audit.py joins it with the function map and the stub list: which recompiled functions
    // still run. Only calls through the function table are seen (a ported function calling a ported one is a direct call).
    namespace
    {
        uint32_t g_auditBase = 0;
        std::vector<GuestFn> g_auditOriginal;
        std::vector<std::atomic<uint64_t>> g_auditCalls;

        void auditEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const size_t slot = (ctx->pc - g_auditBase) >> 2;
            if (slot >= g_auditCalls.size())
                fatal("generated-entry wrapper reached for an unknown address", ctx->pc);
            g_auditCalls[slot].fetch_add(1, std::memory_order_relaxed);
            g_auditOriginal[slot](rdram, ctx, runtime);
        }

        void writeAudit(const std::filesystem::path &path)
        {
            const std::filesystem::path temp = path.string() + ".tmp";
            {
                std::ofstream out(temp);
                for (size_t slot = 0; slot < g_auditCalls.size(); ++slot)
                    if (const uint64_t n = g_auditCalls[slot].load(std::memory_order_relaxed))
                    {
                        char line[64];
                        std::snprintf(line, sizeof(line), "0x%08X %llu\n", g_auditBase + static_cast<uint32_t>(slot) * 4u, static_cast<unsigned long long>(n));
                        out << line;
                    }
            }
            std::error_code ec;
            std::filesystem::rename(temp, path, ec);
        }

        // ---------------------------------------------------------------- numeric fuzz

        // The pure numeric platform functions (soft float, libm) take their arguments in registers and touch only the stack below
        // their entry sp. MMPC_DIFFTEST_NUMERIC=<cases> calls each of them with that many random bit patterns, once as the recompiled
        // function and once as the port, and compares the result register, sp, ra and the callee-saved registers. Arguments: D is a
        // double (64 bit register), F a float (f12, f13), U a 32 bit unsigned (the upper half of its register is random), I a 32 bit int sign extended,
        // N a small signed count, X and Y the base and the exponent of pow.
        struct NumericSpec
        {
            const char *name;
            const char *args;
        };
        const NumericSpec kNumericSpecs[] = {
            {"sdk_fptosi", "F"}, {"sdk_fptoui", "F"}, {"sdk_fptodp", "F"}, {"sdk_dptofp", "D"}, {"sdk_dptoli", "D"}, {"sdk_d_utod", "U"},
            {"sdk_dpadd", "DD"}, {"sdk_dpmul", "DD"},  {"sdk_dpdiv", "DD"},  {"sdk_dpcmp", "DD"},  {"sdk_fp_ltdf2", "DD"},
            {"sdk_fabsf", "F"},  {"sdk_sqrtf", "F"},   {"sdk_atanf", "F"},   {"sdk_acosf", "F"},   {"sdk_atan2f", "FF"},
            {"sdk_fmodf", "FF"}, {"sdk_sinf", "F"},    {"sdk_cosf", "F"},
            {"sdk_dpsub", "DD"}, {"sdk_litodp", "I"},  {"sdk_copysign", "DD"}, {"sdk_scalbn", "DN"}, {"sdk_sqrt", "D"}, {"sdk_pow", "XY"},
        };

        struct Rng
        {
            uint64_t state;
            uint64_t next()
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                return state;
            }
            uint32_t below(uint32_t n) { return static_cast<uint32_t>(next() % n); }
        };

        uint64_t randomBits(Rng &rng, int bits)
        {
            const uint64_t mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
            switch (rng.below(8))
            {
            case 0: return 0;
            case 1: return mask;
            case 2: return 1;
            case 3: return (1ull << rng.below(static_cast<uint32_t>(bits))) & mask;
            case 4: return mask ^ (1ull << rng.below(static_cast<uint32_t>(bits)));
            default: return rng.next() & mask;
            }
        }

        // A double or a float as bits: the exponent mostly where arithmetic stays in range, now and then at the edges.
        uint64_t randomFloatBits(Rng &rng, bool isDouble)
        {
            const int mantissaBits = isDouble ? 52 : 23;
            const int exponentBits = isDouble ? 11 : 8;
            const uint32_t exponentMax = (1u << exponentBits) - 1;
            const uint32_t bias = exponentMax >> 1;
            uint32_t exponent;
            switch (rng.below(10))
            {
            case 0: exponent = 0; break;
            case 1: exponent = exponentMax; break;
            case 2: exponent = exponentMax - 1; break;
            case 3: exponent = 1; break;
            case 4:
            case 5:
            case 6: exponent = bias + rng.below(9) - 4; break;
            case 7: exponent = bias + rng.below(80) - 20; break;
            case 8: exponent = bias + rng.below(130) - 65; break;
            default: exponent = rng.below(exponentMax + 1); break;
            }
            return (static_cast<uint64_t>(rng.next() & 1) << (exponentBits + mantissaBits)) | (static_cast<uint64_t>(exponent) << mantissaBits) |
                   randomBits(rng, mantissaBits);
        }

        // An operand close to another one (equal, opposite, one bit or one unit apart, the same exponent), so that sums cancel and
        // comparisons meet equal operands.
        uint64_t correlatedBits(Rng &rng, uint64_t first, bool isDouble)
        {
            const int mantissaBits = isDouble ? 52 : 23;
            const uint64_t signBit = 1ull << ((isDouble ? 11 : 8) + mantissaBits);
            const uint64_t mantissaMask = (1ull << mantissaBits) - 1;
            switch (rng.below(7))
            {
            case 0: return first ^ signBit;
            case 1: return first;
            case 2: return first ^ (1ull << rng.below(static_cast<uint32_t>(mantissaBits)));
            case 3: return first + 1;
            case 4: return first - 1;
            case 5: return (first & ~mantissaMask) | randomBits(rng, mantissaBits);
            default: return randomFloatBits(rng, isDouble);
            }
        }

        uint64_t randomUnsigned(Rng &rng)
        {
            switch (rng.below(6))
            {
            case 0: return rng.below(16);
            case 1: return (1ull << rng.below(32)) + rng.below(3) - 1;
            case 2: return 0x7FFFFFFFull + rng.below(3) - 1;
            default: return randomBits(rng, 32);
            }
        }

        // A small signed int for an exponent count, with the edges of its range now and then.
        uint64_t randomCount(Rng &rng)
        {
            int64_t n;
            switch (rng.below(10))
            {
            case 0:
            case 1: n = 50000 + static_cast<int64_t>(rng.below(5)) - 2; break;
            case 2: n = -50000 + static_cast<int64_t>(rng.below(5)) - 2; break;
            case 3: n = static_cast<int32_t>(randomUnsigned(rng)); break;
            default: n = static_cast<int64_t>(rng.below(4400)) - 2200; break;
            }
            return static_cast<uint64_t>(n);
        }

        // A base for pow: usually a positive number of moderate size, sometimes the special ones, negative or random.
        uint64_t randomBase(Rng &rng)
        {
            switch (rng.below(10))
            {
            case 0: return std::bit_cast<uint64_t>(10.0);
            case 1: return std::bit_cast<uint64_t>(static_cast<double>(rng.below(5)) * 0.5 + 0.5);
            case 2: return randomBits(rng, 64);
            case 3: return randomFloatBits(rng, true);
            case 4: return std::bit_cast<uint64_t>(-static_cast<double>(rng.below(2000) + 1) / static_cast<double>(1u << rng.below(8)));
            default: return std::bit_cast<uint64_t>(static_cast<double>(rng.below(1000000) + 1) / static_cast<double>(1u << rng.below(12)));
            }
        }

        // An exponent for pow: mostly small integers and halves, sometimes huge or random.
        uint64_t randomExponent(Rng &rng)
        {
            switch (rng.below(10))
            {
            case 0:
            case 1:
            case 2:
            case 3: return std::bit_cast<uint64_t>(static_cast<double>(static_cast<int32_t>(rng.below(61)) - 30));
            case 4: return std::bit_cast<uint64_t>(static_cast<double>(static_cast<int32_t>(rng.below(61)) - 30) + 0.5);
            case 5: return std::bit_cast<uint64_t>(std::ldexp(static_cast<double>(rng.below(1000) + 1), static_cast<int>(rng.below(40))) * (rng.below(2) ? 1.0 : -1.0));
            case 6: return std::bit_cast<uint64_t>(static_cast<double>(static_cast<int32_t>(rng.below(2001)) - 1000) / 7.0);
            default: return randomFloatBits(rng, true);
            }
        }

        struct NumericResult
        {
            uint64_t v0;
            uint32_t f0;
            uint64_t sp, ra;
            uint64_t saved[12];
            uint32_t fsaved[12];
            bool returned;
            bool faulted;
        };

        // MMPC_DIFFTEST_NUMERIC_ARGS=<hex>,<hex> replaces the random arguments by these (one case per function, both results printed).
        std::vector<uint64_t> g_numericFixedArguments;

        void numericArguments(const char *spec, uint64_t seed, uint64_t *values)
        {
            if (!g_numericFixedArguments.empty())
            {
                for (size_t i = 0; i < g_numericFixedArguments.size() && i < 4; ++i)
                    values[i] = g_numericFixedArguments[i];
                return;
            }
            Rng rng{seed * 0x9E3779B97F4A7C15ull + 0x1234567ull};
            for (int i = 0; i < 4; ++i)
                rng.next();
            int index = 0;
            for (const char *c = spec; *c; ++c, ++index)
            {
                const bool isDouble = *c == 'D';
                if (*c == 'D' || *c == 'F')
                    values[index] = index > 0 && rng.below(10) < 4 ? correlatedBits(rng, values[index - 1], isDouble) : randomFloatBits(rng, isDouble);
                else if (*c == 'U')
                    values[index] = randomUnsigned(rng) | (rng.below(2) ? 0 : rng.next() << 32);
                else if (*c == 'N')
                    values[index] = randomCount(rng);
                else if (*c == 'I')
                    values[index] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(randomUnsigned(rng))));
                else if (*c == 'X')
                    values[index] = randomBase(rng);
                else if (*c == 'Y')
                    values[index] = randomExponent(rng);
                else
                    values[index] = rng.next();
            }
        }

        void numericRun(PS2Runtime &runtime, GuestFn fn, uint32_t address, const char *spec, const uint64_t *values, NumericResult &out)
        {
            R5900Context ctx{};
            // every case starts from the same stack: the recompiled functions read parts of their frames they never wrote
            std::memset(g_rdram + 0x01D7F000u, 0, 0x1000);
            setGpr64(&ctx, 29, 0x01D80000u);
            setGpr64(&ctx, 31, kReturnSentinel);
            for (int i = 16; i < 24; ++i)
                setGpr64(&ctx, i, 0x5A5A5A5A00000000ull | static_cast<uint64_t>(i));
            setGpr64(&ctx, 28, 0x5A5A5A5A5A5A5A5Aull);
            setGpr64(&ctx, 30, 0x5A5A5A5A0000001Eull);
            for (int f = 20; f < 32; ++f)
            {
                const uint32_t bits = 0x3F800000u + static_cast<uint32_t>(f);
                std::memcpy(&ctx.f[f], &bits, sizeof(bits));
            }
            int index = 0;
            for (const char *c = spec; *c; ++c, ++index)
            {
                if (*c == 'F')
                {
                    const uint32_t bits = static_cast<uint32_t>(values[index]);
                    std::memcpy(&ctx.f[12 + index], &bits, sizeof(bits));
                }
                else
                    setGpr64(&ctx, 4 + index, values[index]); // a 32 bit argument may carry anything in the upper half, a callee ignores it
            }
            ctx.pc = address;
            out = {};
            runtime.eeScheduler().enterNoPreempt();
            try
            {
                fn(g_rdram, &ctx, &runtime);
                out.returned = finishGuestCall(g_rdram, &ctx, &runtime, address, kReturnSentinel) && (ctx.pc == kReturnSentinel || ctx.pc == address);
            }
            catch (const EeDispatcherTransfer &)
            {
            }
            catch (const Fault &)
            {
                out.faulted = true;
            }
            runtime.eeScheduler().leaveNoPreempt();
            out.v0 = gpr64(&ctx, 2);
            std::memcpy(&out.f0, &ctx.f[0], sizeof(out.f0));
            out.sp = gpr64(&ctx, 29);
            out.ra = gpr64(&ctx, 31);
            static const int kSavedRegisters[] = {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};
            for (int i = 0; i < 12; ++i)
            {
                out.saved[i] = gpr64(&ctx, kSavedRegisters[i]);
                std::memcpy(&out.fsaved[i], &ctx.f[20 + i], sizeof(uint32_t));
            }
        }

        // Runs every numeric function that is ported (or those named); returns the number of cases that differed.
        size_t runNumericFuzz(PS2Runtime &runtime, uint64_t cases, const std::set<std::string> &names, bool all)
        {
            constexpr uint64_t kChunk = 4096;
            size_t totalBad = 0;
            if (const char *fixed = env("MMPC_DIFFTEST_NUMERIC_ARGS"))
                for (const std::string &item : splitList(fixed))
                    g_numericFixedArguments.push_back(std::strtoull(item.c_str(), nullptr, 16));
            for (const NumericSpec &spec : kNumericSpecs)
            {
                if (!all && names.count(spec.name) == 0)
                    continue;
                const Ported *ported_ = nullptr;
                for (const Ported &p : ported())
                    if (std::strcmp(p.name, spec.name) == 0)
                        ported_ = &p;
                if (ported_ == nullptr)
                {
                    std::cout << "[numeric] " << spec.name << ": not ported, skipped" << std::endl;
                    continue;
                }
                const bool intResult = ported_->result == ResultKind::Int;
                const bool floatResult = ported_->result == ResultKind::Float;
                size_t bad = 0, reported = 0;
                std::vector<NumericResult> generated(kChunk), native(kChunk);
                for (uint64_t base = 0; base < cases; base += kChunk)
                {
                    const uint64_t n = std::min<uint64_t>(kChunk, cases - base);
                    useGenerated(runtime, true);
                    for (uint64_t i = 0; i < n; ++i)
                    {
                        uint64_t values[4] = {};
                        numericArguments(spec.args, base + i + 1, values);
                        numericRun(runtime, ported_->original, ported_->address, spec.args, values, generated[i]);
                    }
                    useGenerated(runtime, false);
                    for (uint64_t i = 0; i < n; ++i)
                    {
                        uint64_t values[4] = {};
                        numericArguments(spec.args, base + i + 1, values);
                        numericRun(runtime, ported_->wrapper, ported_->address, spec.args, values, native[i]);
                        const NumericResult &a = generated[i], &b = native[i];
                        if (!g_numericFixedArguments.empty())
                            std::cout << "[numeric] " << spec.name << " probe: original v0 " << std::hex << a.v0 << " f0 " << a.f0 << ", port v0 " << b.v0 << " f0 "
                                      << b.f0 << std::dec << std::endl;
                        bool same = a.returned == b.returned && a.faulted == b.faulted && a.sp == b.sp && a.ra == b.ra;
                        same = same && (!intResult || a.v0 == b.v0) && (!floatResult || a.f0 == b.f0);
                        for (int k = 0; k < 12; ++k)
                            same = same && a.saved[k] == b.saved[k] && a.fsaved[k] == b.fsaved[k];
                        if (same)
                            continue;
                        ++bad;
                        if (reported++ < 5)
                        {
                            std::cout << "[numeric] FAIL " << spec.name << " case " << (base + i + 1) << " args";
                            for (int k = 0; spec.args[k]; ++k)
                                std::cout << " " << std::hex << values[k] << std::dec;
                            std::cout << ": original v0 " << std::hex << a.v0 << " f0 " << a.f0 << " vs port v0 " << b.v0 << " f0 " << b.f0 << std::dec
                                      << (a.returned ? "" : " (original did not return)") << (b.returned ? "" : " (port did not return)")
                                      << (b.faulted ? " (port faulted)" : "") << std::endl;
                        }
                    }
                }
                std::cout << "[numeric] " << spec.name << ": " << cases << " cases, " << bad << " differ" << std::endl;
                totalBad += bad;
            }
            return totalBad;
        }

        // MMPC_DIFFTEST_NUMERIC_REFERENCE=<cases>: the soft-float functions as a running game has them (g_recompiledQuirks off) against the
        // host's IEEE doubles, since the recompiled code cannot be the reference where it is wrong (docs/PLATFORM_FINDINGS.md). The library
        // does not round (it cuts the result off), has no denormals and its own NaNs, so only normal operands and results count: a sum or
        // difference must be the exact sum cut off toward zero (the host rounds to nearest, and TwoSum gives the rounding error exactly),
        // allowing the neighbour for the sticky shift; the conversions must be exact; pow must be close to the host's and the powers of
        // ten the game uses must come out as integers. Returns the number of cases that failed.
        size_t runNumericReference(uint64_t cases)
        {
            mm::g_recompiledQuirks = false;
            auto asDouble = [](uint64_t bits) { return std::bit_cast<double>(bits); };
            auto asBits = [](double value) { return std::bit_cast<uint64_t>(value); };
            auto isNormal = [](uint64_t bits) {
                const uint32_t exponent = static_cast<uint32_t>(bits >> 52) & 0x7FFu;
                return exponent != 0 && exponent != 0x7FF;
            };
            auto report = [](const char *name, uint64_t a, uint64_t b, uint64_t got, uint64_t expected) {
                std::cout << "[reference] FAIL " << name << " " << std::hex << a << " " << b << ": port " << got << ", expected " << expected << std::dec
                          << std::endl;
            };
            size_t totalBad = 0;

            // The game's own library code run by an interpreter (LOCAL-AI/tools/oracle_cases.py), with the recompiler's quirks and with
            // the full 64 bit branches: every case must come out bit for bit in both of the native functions' modes.
            if (const char *file = env("MMPC_DIFFTEST_ORACLE_FILE"))
            {
                std::ifstream in(file);
                std::map<std::string, std::array<size_t, 4>> counts; // cases and failures with the quirks, cases and failures without
                std::string name;
                uint64_t a, b, withQuirks, without;
                size_t shown = 0;
                while (in >> name >> std::hex >> a >> b >> withQuirks >> without >> std::dec)
                {
                    auto run = [&](bool quirks) -> uint64_t {
                        mm::g_recompiledQuirks = quirks;
                        if (name == "dpadd") return mm::sdk_dpadd(a, b);
                        if (name == "dpsub") return mm::sdk_dpsub(a, b);
                        if (name == "dpmul") return mm::sdk_dpmul(a, b);
                        if (name == "dpdiv") return mm::sdk_dpdiv(a, b);
                        if (name == "dpcmp") return static_cast<uint32_t>(mm::sdk_dpcmp(a, b));
                        if (name == "litodp") return mm::sdk_litodp(static_cast<int32_t>(a));
                        if (name == "d_utod") return mm::sdk_d_utod(static_cast<uint32_t>(a));
                        if (name == "dptoli") return static_cast<uint32_t>(mm::sdk_dptoli(a));
                        return mm::sdk_pow(a, b);
                    };
                    const bool low = name == "dpcmp" || name == "dptoli";
                    const uint64_t expectedQuirks = low ? static_cast<uint32_t>(withQuirks) : withQuirks;
                    const uint64_t expectedCorrect = low ? static_cast<uint32_t>(without) : without;
                    auto &c = counts[name];
                    ++c[0];
                    ++c[2];
                    if (const uint64_t got = run(true); got != expectedQuirks)
                    {
                        ++c[1];
                        if (shown++ < 8)
                            std::cout << "[oracle] FAIL " << name << " with the quirks " << std::hex << a << " " << b << ": port " << got << ", oracle " << expectedQuirks << std::dec << std::endl;
                    }
                    if (const uint64_t got = run(false); got != expectedCorrect)
                    {
                        ++c[3];
                        if (shown++ < 8)
                            std::cout << "[oracle] FAIL " << name << " without " << std::hex << a << " " << b << ": port " << got << ", oracle " << expectedCorrect << std::dec << std::endl;
                    }
                }
                mm::g_recompiledQuirks = false;
                for (const auto &[function, c] : counts)
                {
                    std::cout << "[oracle] " << function << ": " << c[0] << " cases, with the quirks " << c[1] << " differ, without " << c[3] << " differ" << std::endl;
                    totalBad += c[1] + c[3];
                }
            }

            // Sums and differences
            for (int subtract = 0; subtract < 2; ++subtract)
            {
                const char *name = subtract ? "sdk_dpsub" : "sdk_dpadd";
                size_t counted = 0, skipped = 0, exact = 0, neighbour = 0, bad = 0;
                for (uint64_t i = 0; i < cases; ++i)
                {
                    uint64_t v[4] = {};
                    numericArguments("DD", i + 1, v);
                    if (!isNormal(v[0]) || !isNormal(v[1]))
                    {
                        ++skipped;
                        continue;
                    }
                    const double x = asDouble(v[0]), y = subtract ? -asDouble(v[1]) : asDouble(v[1]);
                    const double s = x + y;
                    if (s == 0 || !std::isfinite(s) || !isNormal(asBits(s)))
                    {
                        ++skipped;
                        continue;
                    }
                    const double bb = s - x;
                    const double rounding = (x - (s - bb)) + (y - bb); // exact sum minus s
                    const double cut = rounding != 0 && ((rounding < 0) != (s < 0)) ? std::nextafter(s, 0.0) : s;
                    if (!isNormal(asBits(cut)))
                    {
                        ++skipped;
                        continue;
                    }
                    const uint64_t got = subtract ? mm::sdk_dpsub(v[0], v[1]) : mm::sdk_dpadd(v[0], v[1]);
                    ++counted;
                    const uint64_t expected = asBits(cut);
                    const uint64_t distance = got > expected ? got - expected : expected - got;
                    if (got == expected)
                        ++exact;
                    else if (distance == 1 && (got >> 63) == (expected >> 63))
                        ++neighbour;
                    else
                    {
                        if (bad++ < 5)
                            report(name, v[0], v[1], got, expected);
                    }
                }
                std::cout << "[reference] " << name << ": " << counted << " counted (" << skipped << " skipped), " << exact << " equal to the cut off sum, "
                          << neighbour << " its neighbour, " << bad << " failed" << std::endl;
                totalBad += bad;
            }

            // Conversions
            {
                size_t bad = 0;
                for (uint64_t i = 0; i < cases; ++i)
                {
                    uint64_t v[4] = {};
                    numericArguments("U", i + 1, v);
                    const uint32_t value = static_cast<uint32_t>(v[0]);
                    const uint64_t expected = asBits(static_cast<double>(value));
                    if (const uint64_t got = mm::sdk_d_utod(value); got != expected && bad++ < 5)
                        report("sdk_d_utod", value, 0, got, expected);
                    numericArguments("I", i + 1, v);
                    const int32_t signedValue = static_cast<int32_t>(v[0]);
                    const uint64_t signedExpected = asBits(static_cast<double>(signedValue));
                    if (const uint64_t got = mm::sdk_litodp(signedValue); got != signedExpected && bad++ < 5)
                        report("sdk_litodp", static_cast<uint32_t>(signedValue), 0, got, signedExpected);
                }
                std::cout << "[reference] sdk_d_utod and sdk_litodp: " << cases << " cases each, " << bad << " failed" << std::endl;
                totalBad += bad;
            }

            // pow: relative distance to the host's, and the goods price's powers of ten
            {
                size_t counted = 0, bad = 0;
                double worst = 0;
                for (uint64_t i = 0; i < cases; ++i)
                {
                    uint64_t v[4] = {};
                    numericArguments("XY", i + 1, v);
                    const double x = asDouble(v[0]), y = asDouble(v[1]);
                    if (!isNormal(v[0]) || !isNormal(v[1]) || x < 0)
                        continue;
                    const double expected = std::pow(x, y);
                    if (!std::isfinite(expected) || !isNormal(asBits(expected)) || expected == 0 || std::fabs(expected) > 1e300 || std::fabs(expected) < 1e-300)
                        continue;
                    const double got = asDouble(mm::sdk_pow(v[0], v[1]));
                    const double relative = std::fabs((got - expected) / expected);
                    ++counted;
                    worst = std::max(worst, relative);
                    if (!(relative < 1e-12) && bad++ < 5)
                        report("sdk_pow", v[0], v[1], asBits(got), asBits(expected));
                }
                std::cout << "[reference] sdk_pow: " << counted << " counted, worst relative distance " << worst << ", " << bad << " beyond 1e-12" << std::endl;
                totalBad += bad;
                for (int n = 0; n <= 9; ++n)
                {
                    const uint64_t got = mm::sdk_pow(asBits(10.0), asBits(static_cast<double>(n)));
                    const int asInteger = mm::sdk_dptoli(got);
                    int expected = 1;
                    for (int k = 0; k < n; ++k)
                        expected *= 10;
                    std::cout << "[reference] pow(10, " << n << ") = " << std::hex << got << std::dec << " as an integer " << asInteger << (asInteger == expected ? "" : " WRONG, expected ")
                              << (asInteger == expected ? "" : std::to_string(expected)) << std::endl;
                    if (asInteger != expected)
                        ++totalBad;
                }
            }
            return totalBad;
        }
    }

    void installGeneratedAudit(PS2Runtime &runtime)
    {
        const char *file = env("MMPC_GENERATED_ENTRIES");
        if (!file)
            return;
        g_auditBase = g_ps2RecompiledFunctionTableBase;
        const size_t slots = ((g_ps2RecompiledFunctionTableEnd - g_auditBase) >> 2) + 1;
        std::set<uint32_t> ports;
        for (const Ported &p : ported())
            ports.insert(p.address);
        g_auditOriginal.assign(slots, nullptr);
        g_auditCalls = std::vector<std::atomic<uint64_t>>(slots);
        size_t wrapped = 0;
        for (size_t slot = 0; slot < slots; ++slot)
        {
            const uint32_t address = g_auditBase + static_cast<uint32_t>(slot) * 4u;
            if (!runtime.hasFunction(address) || ports.count(address) != 0)
                continue;
            g_auditOriginal[slot] = runtime.lookupFunction(address);
            runtime.registerFunction(address, auditEntry);
            ++wrapped;
        }
        const std::filesystem::path path = std::filesystem::absolute(file);
        std::thread([path]()
                    {
                        for (;;)
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                            writeAudit(path);
                        }
                    })
            .detach();
        std::cerr << "[difftest] counting entries of " << wrapped << " generated functions' addresses into " << path.string() << std::endl;
    }

    // ---------------------------------------------------------------- movie picture check
    // MMPC_MOVIE_CHECK=1: a live check of the movie decoder group (Movie_PlayPss, Movie_InitAll, Movie_VideoDecMain,
    // Movie_DecodeLoop, PlayOpeningMovie), which the difftest cannot replay: two guest threads and the vblank interrupt interleave
    // by time, and native code charges other cycles than the recompiled code, so two runs interleave differently. The decode loop
    // hands each picture's slot to sceMpegGetPicture and then, once it has counted the picture into the output queue, calls
    // sceMpegIsEnd; the recompiled loop and the ported one both call them through the function table. At each sceMpegIsEnd after a
    // sceMpegGetPicture the picture is logged as one line: its number in the run, the decoder's frame number, the slot, a hash of
    // the slot's pixels and the subtitle state (the next subtitle and a hash of the subtitle packet). The vblank handler only reads
    // these, so they do not depend on timing: a run with every port off (MMPC_PORT_DISABLE=all) and one with the ports must log the
    // same lines (tools/movie_check_cmp.py). What depends on timing (which vblank shows a picture, the miss counter, the
    // fade overlay's steps) is not compared.
    namespace
    {
        GuestFn g_movieGetPicture = nullptr;
        GuestFn g_movieIsEnd = nullptr;
        uint32_t g_moviePendingSlot = 0, g_moviePendingBlocks = 0, g_moviePictures = 0;

        uint64_t hashGuest(const uint8_t *rdram, uint32_t address, uint32_t bytes)
        {
            uint64_t h = 0xCBF29CE484222325ull;
            for (uint32_t i = 0; i < bytes; ++i)
                h = (h ^ rdram[(address + i) & PS2_RAM_MASK]) * 0x100000001B3ull;
            return h;
        }

        void movieGetPictureEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            // sceMpegGetPicture(mpeg, slot, macroblocks): RGB32 macroblocks of 16 x 16 pixels, 1 KB each. It may wait for the picture
            // (the call then ends in a thread switch), so the slot is hashed at the next sceMpegIsEnd.
            g_moviePendingSlot = static_cast<uint32_t>(gpr64(ctx, 5));
            g_moviePendingBlocks = static_cast<uint32_t>(gpr64(ctx, 6));
            g_movieGetPicture(rdram, ctx, runtime);
        }

        void movieIsEndEntry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            constexpr uint32_t kSubtitleNext = 0x001BD9ECu, kSubtitlePacket = 0x0020C1A0u, kSubtitleEnd = 0x00215F01u;
            if (g_moviePendingSlot != 0u)
            {
                const uint32_t videoDec = static_cast<uint32_t>(gpr64(ctx, 4));
                int32_t frame = 0, subtitle = 0;
                std::memcpy(&frame, rdram + ((videoDec + 8u) & PS2_RAM_MASK), 4);
                std::memcpy(&subtitle, rdram + (kSubtitleNext & PS2_RAM_MASK), 4);
                const uint64_t image = hashGuest(rdram, g_moviePendingSlot, std::min(g_moviePendingBlocks, 0x2000u) * 0x400u);
                const uint64_t packet = hashGuest(rdram, kSubtitlePacket, kSubtitleEnd - kSubtitlePacket);
                char line[160];
                std::snprintf(line, sizeof(line), "[movie] picture %u frame %d slot %08X blocks %u image %016llX subtitle %d %016llX\n",
                              g_moviePictures++, frame, g_moviePendingSlot, g_moviePendingBlocks, static_cast<unsigned long long>(image),
                              subtitle, static_cast<unsigned long long>(packet));
                std::fputs(line, stderr);
                g_moviePendingSlot = 0;
            }
            g_movieIsEnd(rdram, ctx, runtime);
        }
    }

    void installMovieCheck(PS2Runtime &runtime)
    {
        if (env("MMPC_MOVIE_CHECK") == nullptr)
            return;
        g_movieGetPicture = runtime.lookupFunction(mm::sdk::addr::sceMpegGetPicture);
        g_movieIsEnd = runtime.lookupFunction(mm::sdk::addr::sceMpegIsEnd);
        runtime.registerFunction(mm::sdk::addr::sceMpegGetPicture, movieGetPictureEntry);
        runtime.registerFunction(mm::sdk::addr::sceMpegIsEnd, movieIsEndEntry);
        std::cerr << "[difftest] movie check on: each decoded picture is logged as a [movie] line" << std::endl;
    }

    bool captureRequested() { return env("MMPC_CAPTURE") != nullptr; }

    void installCapture(PS2Runtime &runtime)
    {
        const char *list = env("MMPC_CAPTURE");
        if (!list)
            return;
        mm::g_recompiledQuirks = true; // a capture run plays as the recorded ones did (sdk.h)
        g_captureDir = env("MMPC_CAPTURE_DIR") ? env("MMPC_CAPTURE_DIR") : "difftest";
        if (const char *max = env("MMPC_CAPTURE_MAX"))
            g_captureMax = static_cast<uint32_t>(std::strtoul(max, nullptr, 0));
        if (const char *every = env("MMPC_CAPTURE_EVERY"))
            g_captureEvery = std::strcmp(every, "exp") == 0 ? 0u : std::max<uint32_t>(1u, static_cast<uint32_t>(std::strtoul(every, nullptr, 0)));
        if (const char *from = env("MMPC_CAPTURE_FROM"))
            g_captureFrom = std::strtoull(from, nullptr, 0);
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
        // The replay and the fuzz compare the native functions with the recompiled ones, so the natives keep the recompiled quirks
        // (sdk.h); the reference check turns them off.
        mm::g_recompiledQuirks = true;
        if (const char *casesText = env("MMPC_DIFFTEST_NUMERIC_REFERENCE"))
        {
            const size_t bad = runNumericReference(std::strtoull(casesText, nullptr, 0));
            std::cout << "[reference] " << bad << " case(s) failed" << std::endl;
            std::cout.flush();
            std::exit(static_cast<int>(std::min<size_t>(bad, 125)));
        }
        // MMPC_DIFFTEST_NUMERIC=<cases> [MMPC_DIFFTEST_NUMERIC_FUNCTIONS=<names or all>]: the numeric fuzz (see above), then exit.
        if (const char *casesText = env("MMPC_DIFFTEST_NUMERIC"))
        {
            std::set<std::string> numericNames;
            bool numericAll = env("MMPC_DIFFTEST_NUMERIC_FUNCTIONS") == nullptr;
            if (const char *list = env("MMPC_DIFFTEST_NUMERIC_FUNCTIONS"))
                for (const std::string &item : splitList(list))
                {
                    if (item == "all")
                        numericAll = true;
                    else
                        numericNames.insert(item);
                }
            const size_t bad = runNumericFuzz(runtime, std::strtoull(casesText, nullptr, 0), numericNames, numericAll);
            std::cout << "[numeric] " << bad << " case(s) differ" << std::endl;
            std::cout.flush();
            std::exit(static_cast<int>(std::min<size_t>(bad, 125)));
        }
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
        const bool childReplay = env("MMPC_DIFFTEST_CHILD") != nullptr; // started by replayInChildProcess for one function
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
            int extraSeconds = 0; // on top of the limit, for this replay
            bool active = false;
        };
        static Watch watch;
        const int hangSeconds = env("MMPC_DIFFTEST_HANG_SECONDS") ? std::atoi(env("MMPC_DIFFTEST_HANG_SECONDS")) : 60;
        std::thread([hangSeconds] {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::lock_guard<std::mutex> lock(watch.mutex);
                const int limit = hangSeconds + watch.extraSeconds;
                if (watch.active && std::chrono::steady_clock::now() - watch.since > std::chrono::seconds(limit))
                {
                    std::cout << "[difftest] HANG " << watch.what << " (no return within " << limit << " s)" << std::endl;
                    std::cout.flush();
                    std::_Exit(124);
                }
            }
        }).detach();
        const auto watchStart = [](const std::string &what) {
            std::lock_guard<std::mutex> lock(watch.mutex);
            watch.what = what;
            watch.since = std::chrono::steady_clock::now();
            // A stop<N> record replays N frames from boot, about 150 a second on an idle machine (3,001 take some 20 s), which a
            // short limit (the full run's 15 s) cuts off: it gets a second more for every 50 frames.
            watch.extraSeconds = static_cast<int>(g_syncVStopAt / 50u);
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
        std::string resumeFunction, resumeFile;
        if (const char *from = env("MMPC_DIFFTEST_FROM"))
        {
            const std::string value = from;
            const size_t colon = value.find(':');
            if (colon != std::string::npos)
            {
                resumeFunction = value.substr(0, colon);
                resumeFile = value.substr(colon + 1);
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
        // A record's two outcomes: the changes listing when asked for, the comparison and the FAIL lines. True when they agree.
        const auto judge = [&](const Ported &p, const std::filesystem::path &file, const Record &rec, const Outcome &original, const Outcome &portedRun) {
            const std::vector<std::string> diffs = compare(p, rec, original, portedRun);
            if (changesAll || changeNames.count(p.name) != 0)
            {
                std::cout << "[difftest] " << p.name << " " << file.filename().string() << " changes " << describeChanges(rec, original);
                if (p.result == ResultKind::Int)
                {
                    char v0[32];
                    std::snprintf(v0, sizeof(v0), "; v0 %llX", static_cast<unsigned long long>(gpr64(&original.ctx, 2)));
                    std::cout << v0;
                }
                std::cout << std::endl;
            }
            if (!diffs.empty())
            {
                std::cout << "[difftest] FAIL " << p.name << " " << file.filename().string() << std::endl;
                for (const std::string &d : diffs)
                    std::cout << "    " << d << std::endl;
            }
            // MMPC_DIFFTEST_SHOW=<hex address>:<words>,...: those words of both runs' end states for every record, passing or not
            // (where a long run got to: a scene's state word), without counting them as a difference.
            if (const char *show = env("MMPC_DIFFTEST_SHOW"))
                for (const std::string &item : splitList(show))
                {
                    unsigned long address = 0, words = 0;
                    if (std::sscanf(item.c_str(), "%lx:%lu", &address, &words) != 2)
                        continue;
                    std::string line = "[difftest] " + std::string(p.name) + " " + file.filename().string() + " show";
                    for (unsigned long k = 0; k < words; ++k)
                    {
                        uint32_t a = 0, b = 0;
                        std::memcpy(&a, &original.rdram[(address + 4 * k) & PS2_RAM_MASK], 4);
                        std::memcpy(&b, &portedRun.rdram[(address + 4 * k) & PS2_RAM_MASK], 4);
                        char text[40];
                        if (a == b)
                            std::snprintf(text, sizeof(text), " %lx:%x", address + 4 * k, a);
                        else
                            std::snprintf(text, sizeof(text), " %lx:%x/%x", address + 4 * k, a, b);
                        line += text;
                    }
                    std::cout << line << std::endl;
                }
            return diffs.empty();
        };
        // A child of sideChildren: MMPC_DIFFTEST_SIDE=original|port, MMPC_DIFFTEST_RECORD=<file name>, MMPC_DIFFTEST_OUTCOME=<file>.
        const char *sideOnly = childReplay ? env("MMPC_DIFFTEST_SIDE") : nullptr;
        const char *recordOnly = childReplay ? env("MMPC_DIFFTEST_RECORD") : nullptr;
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
            // MMPC_DIFFTEST_FROM=<function>:<record file> leaves out that function's records that sort before the file, to go on
            // after a record that hung (it has been moved away) without replaying the ones before it again.
            if (p.name == resumeFunction)
                files.erase(std::remove_if(files.begin(), files.end(), [&](const std::filesystem::path &f) { return f.filename().string() < resumeFile; }),
                            files.end());
            if (recordOnly != nullptr)
                files.erase(std::remove_if(files.begin(), files.end(), [&](const std::filesystem::path &f) { return f.filename().string() != recordOnly; }),
                            files.end());
            if (files.empty())
            {
                ++untested;
                addResult(p, 0, 0);
                std::cout << "[difftest] " << p.name << ": no records (capture with MMPC_CAPTURE=" << p.name << ")" << std::endl;
                continue;
            }
            ++functions;
            // sideChildren: each record's two runs in two child processes, compared here.
            if (!childReplay && sideChildren().count(p.name) != 0)
            {
                const std::filesystem::path outcomes = std::filesystem::temp_directory_path() / "mmpc_difftest_outcomes";
                std::error_code ec;
                std::filesystem::create_directories(outcomes, ec);
                size_t ok = 0;
                const size_t failedBefore = failed;
                for (const auto &file : files)
                {
                    ++records;
                    Record rec;
                    std::string error;
                    if (!readRecord(file, rec, error))
                    {
                        ++failed;
                        std::cout << "[difftest] " << p.name << " " << file.filename().string() << ": " << error << std::endl;
                        continue;
                    }
                    Outcome runs[2];
                    bool read[2] = {};
                    int codes[2] = {};
                    for (int side = 0; side < 2; ++side)
                    {
                        const char *sideName = side == 0 ? "original" : "port";
                        const std::filesystem::path path = outcomes / (std::string(p.name) + "." + sideName + ".outcome");
                        std::filesystem::remove(path, ec);
                        codes[side] = replayInChildProcess(p.name, {{"MMPC_DIFFTEST_SIDE", sideName},
                                                                    {"MMPC_DIFFTEST_RECORD", file.filename().string()},
                                                                    {"MMPC_DIFFTEST_OUTCOME", path.string()}});
                        read[side] = readOutcome(path, runs[side]);
                        std::filesystem::remove(path, ec);
                    }
                    if (!read[0] || !read[1])
                    {
                        ++failed;
                        std::cout << "[difftest] FAIL " << p.name << " " << file.filename().string() << ": no outcome from the "
                                  << (!read[0] ? "original" : "port") << " run's process (exit codes " << codes[0] << ", " << codes[1] << ")"
                                  << std::endl;
                        continue;
                    }
                    if (judge(p, file, rec, runs[0], runs[1]))
                        ++ok;
                    else
                        ++failed;
                }
                std::cout << "[difftest] " << p.name << ": " << ok << "/" << files.size() << " exact (each run in a process of its own)" << std::endl;
                addResult(p, files.size(), failed - failedBefore);
                continue;
            }
            // The functions that load the sound driver's IOP modules (App_ctor through Sound_Init) are replayed in a child process of
            // their own: the replay does not reset the IOP, and every load takes new room in its module arena (the loaded modules'
            // threads and RPC servers stay too), so after a few runs the loads fail and the game retries them forever. A child starts
            // with an empty IOP. So are the functions that need the driver running (afterSoundInit): their child starts it first.
            if (!childReplay && (std::strcmp(p.name, "Sound_LoadModules") == 0 || std::strcmp(p.name, "Sound_Init") == 0 ||
                                 std::strcmp(p.name, "App_ctor") == 0 || afterSoundInit().count(p.name) != 0))
            {
                const int code = replayInChildProcess(p.name);
                if (code < 0)
                    std::cout << "[difftest] " << p.name << ": the child process did not start" << std::endl;
                const size_t bad = code == 0 ? 0 : code < 0 ? files.size() : std::min(static_cast<size_t>(code), files.size());
                records += files.size();
                failed += bad;
                addResult(p, files.size(), bad);
                continue;
            }
            if (childReplay && afterSoundInit().count(p.name) != 0 && !startSoundDriver(runtime, dir))
                std::cout << "[difftest] " << p.name << ": the sound driver could not be started (no Sound_Init record that returns)"
                          << std::endl;
            // The scripted vsync wait (scriptedWaits) for this function's runs; useGenerated leaves it alone (not a ported address).
            const bool scripted = scriptedWaits().count(p.name) != 0;
            const GuestFn realSyncV = scripted ? runtime.lookupFunction(mm::sdk::addr::sceGsSyncV) : nullptr;
            if (scripted)
                runtime.registerFunction(mm::sdk::addr::sceGsSyncV, syncVStandIn);
            g_scriptedRun = scripted;
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
                // A scripted function's record named stop<N>_... ends both runs at the Nth vsync wait (see ReplayStop).
                {
                    const std::string recordName = file.filename().string();
                    g_syncVStopAt = scripted && recordName.rfind("stop", 0) == 0 ? static_cast<uint32_t>(std::strtoul(recordName.c_str() + 4, nullptr, 10)) : 0u;
                }
                watchStart(std::string(p.name) + " " + file.filename().string());
                // A record named warm<N>_... is first advanced by N calls of the original with its arguments: the state a function that
                // works in steps over many frames reaches by itself (memory and heap bookkeeping carry over from call to call).
                PS2Runtime::GuestHeapState heap = rec.hasHeap ? rec.heap : baseHeapState(runtime);
                {
                    const std::string recordName = file.filename().string();
                    const size_t warmCalls = recordName.rfind("warm", 0) == 0 ? std::strtoul(recordName.c_str() + 4, nullptr, 10) : 0;
                    Outcome warmed;
                    useGenerated(runtime, true);
                    for (size_t call = 0; call < warmCalls; ++call)
                    {
                        runOnce(runtime, p.original, p.address, rec, rec.ctx, warmed, heap);
                        rec.rdram = warmed.rdram;
                        rec.scratchpad = warmed.scratchpad;
                        heap = runtime.saveGuestHeapState();
                        if (rec.hasFiles)
                            keepFiles(runtime, rec);
                    }
                }
                // A child of sideChildren runs one side only (MMPC_DIFFTEST_SIDE) and leaves its outcome for the parent to compare.
                if (sideOnly != nullptr)
                {
                    const bool port = std::strcmp(sideOnly, "port") == 0;
                    Outcome run;
                    watchStart(std::string(p.name) + " " + file.filename().string() + (port ? " (port)" : " (original)"));
                    useGenerated(runtime, !port);
                    runOnce(runtime, port ? p.wrapper : p.original, p.address, rec, rec.ctx, run, heap);
                    watchStop();
                    const char *outcomePath = env("MMPC_DIFFTEST_OUTCOME");
                    if (outcomePath == nullptr || !writeOutcome(outcomePath, run))
                        ++failed;
                    continue;
                }
                // Each run is watched on its own, so a HANG line says which one did not return.
                watchStart(std::string(p.name) + " " + file.filename().string() + " (original)");
                useGenerated(runtime, true);
                runOnce(runtime, p.original, p.address, rec, rec.ctx, original, heap);
                // MMPC_DIFFTEST_ORIGINAL_ONLY=1 skips the port run: a record that hangs in the replay and returns here hung in the
                // port (a bug), while one that hangs here too is a state the game cannot reach in the replay.
                if (env("MMPC_DIFFTEST_ORIGINAL_ONLY") != nullptr)
                {
                    watchStop();
                    std::cout << "[difftest] " << p.name << " " << file.filename().string() << ": the original returned" << std::endl;
                    continue;
                }
                watchStart(std::string(p.name) + " " + file.filename().string() + " (port)");
                useGenerated(runtime, false);
                runOnce(runtime, p.wrapper, p.address, rec, rec.ctx, portedRun, heap);
                watchStop();
                if (judge(p, file, rec, original, portedRun))
                    ++ok;
                else
                    ++failed;

                // Fuzzing: the same record with other float and integer arguments (pointers unchanged).
                for (int variant = 0; fuzzThis && variant < fuzzCount; ++variant)
                {
                    R5900Context start = rec.ctx;
                    p.mutate(&start, 0x9E3779B9u * static_cast<uint32_t>((&file - files.data()) * 1009 + variant + 1));
                    Outcome a, b;
                    watchStart(std::string(p.name) + " " + file.filename().string() + " fuzz " + std::to_string(variant) + " (original)");
                    useGenerated(runtime, true);
                    runOnce(runtime, p.original, p.address, rec, start, a, heap);
                    watchStart(std::string(p.name) + " " + file.filename().string() + " fuzz " + std::to_string(variant) + " (port)");
                    useGenerated(runtime, false);
                    runOnce(runtime, p.wrapper, p.address, rec, start, b, heap);
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
            if (scripted)
                runtime.registerFunction(mm::sdk::addr::sceGsSyncV, realSyncV);
            g_scriptedRun = false;
            g_syncVStopAt = 0;
            if (sideOnly != nullptr) // one run of one record, for the parent to compare
                continue;
            std::cout << "[difftest] " << p.name << ": " << ok << "/" << files.size() << " exact";
            if (fuzzThis)
                std::cout << ", fuzzed " << fuzzOk << "/" << fuzzRuns << " exact";
            std::cout << std::endl;
            addResult(p, files.size(), failed - failedBefore);
        }
        results += "\n}\n";
        if (onlyNames.empty())
            std::ofstream(dir / "results.json", std::ios::binary) << results;
        if (!childReplay) // a child's records are counted in its parent's line
            std::cout << "[difftest] " << functions << " function(s) tested, " << untested << " without records; " << records
                      << " record(s), " << failed << " failed" << std::endl;
        std::cout.flush();
        std::exit(static_cast<int>(std::min<size_t>(failed, 125)));
    }
}
