// The native renderer: the game's DMA transfers in, a progressive picture on the GPU out (plan N3). See render_native.h.
//
// The runtime calls onDmaStart for every VIF1 and GIF transfer the game starts, on the game thread, before the emulated
// hardware takes it up. The renderer walks the transfer (a DMA chain, or plain quadwords), sends the GIF packets in it to its
// own GS (render_gs.h), and when the transfer was one of the game's two frame lists, hands the frame it built to the window
// thread. There, the host's present() calls back into present() below, which uploads new textures and vertices, draws the
// frame into a render target at MMPC_RENDER_SCALE times 640 x 448, and fits that to the window at 4:3. The VU1 programs'
// output (the 3D) is not drawn natively yet (plan N4): side mode shows the emulated picture beside it.

#include "render_native.h"

#include "render_gs.h"
#include "render_vif.h"
#include "render_shaders.h"

#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mm::render
{
    namespace
    {
        constexpr uint32_t kDmaLists = 0x001BD9C0u; // globals::kDmaLists: the game's two frame list addresses
        constexpr uint32_t kRamMask = 0x01FFFFFFu;

        enum class Mode
        {
            Off,
            Native,
            Side,  // F2 switches between the emulated and the native picture
            Split, // both at once: emulated on the left, native on the right
        };

        struct DrawParams // the shader's DrawParams
        {
            uint32_t flags;
            uint32_t alphaTest;
            float alphaRef;
            float scale;
            float fogColor[4];
            uint32_t region[4];
        };

        struct GpuTexture
        {
            SDL_GPUTexture *texture = nullptr;
            uint64_t lastUsed = 0;
        };

        struct Renderer
        {
            Mode mode = Mode::Off;
            PS2Runtime *runtime = nullptr;
            Vram vram;
            Gs gs{vram};
            Vif1 vif{gs};
            std::vector<uint8_t> stream;
            // MMPC_RENDER_STATS=1: what each frame held that the native path does not draw yet
            std::map<uint32_t, uint32_t> programCalls; // microcode upload packet -> CALLs this frame
            uint32_t gifTransfers = 0, otherVifTransfers = 0;
            uint64_t lastVsync = 0, frameListVsync = 0; // the vertical blank of the last transfer and of the last frame list
            bool f2Down = false;
            std::atomic<bool> showNative{true};

            // handoff, game thread -> window thread
            std::mutex mutex;
            Frame latest;
            bool fresh = false;
            std::unordered_map<uint64_t, TextureImage> pendingTextures;
            std::vector<uint64_t> evicted; // window thread -> game thread: texture contents the GPU side let go

            // window thread
            uint32_t scale = 3;
            SDL_GPUDevice *device = nullptr;
            SDL_GPUShader *vertexShader = nullptr, *pixelShader = nullptr;
            std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline *> pipelines;
            SDL_GPUSampler *sampler = nullptr; // bound with each texture; the shader reads texels itself
            SDL_GPUTexture *colors[2] = {}, *depth = nullptr, *white = nullptr;
            int target = 0; // the colour target drawn last; the other holds the frame before it
            SDL_GPUBuffer *vertexBuffer = nullptr;
            uint32_t vertexCapacity = 0;
            SDL_GPUTransferBuffer *vertexUpload = nullptr;
            std::unordered_map<uint64_t, GpuTexture> textures;
            Frame current;
            uint64_t framesDrawn = 0;
            bool gpuFailed = false;
        };

        Renderer &renderer()
        {
            static Renderer r;
            return r;
        }

        uint32_t load32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t v;
            std::memcpy(&v, rdram + (address & kRamMask & ~3u), 4);
            return v;
        }

        // ---- the game thread: DMA transfers to GIF packets ----

        // Appends the data of a DMA chain from `tadr`, as the channel would send it: with tag transfer (VIF1), each tag's
        // upper half goes first as two VIF codes, after two NOPs that keep the transfer's quadword alignment.
        constexpr uint32_t kMicrocodeStart = 0x001A3C80u, kMicrocodeEnd = 0x001AB1C0u; // the 13 VU1 program upload packets

        void walkChain(const uint8_t *rdram, uint32_t tadr, bool tagTransfer, std::vector<uint8_t> &out,
                       std::map<uint32_t, uint32_t> *programCalls = nullptr)
        {
            uint32_t address = tadr & 0x7FFFFFF0u;
            uint32_t stack[2] = {};
            int depth = 0;
            for (int tags = 0; tags < (1 << 20); ++tags)
            {
                if ((address & kRamMask) + 16u > 0x02000000u)
                    return;
                uint64_t lo, hi;
                std::memcpy(&lo, rdram + (address & kRamMask), 8);
                std::memcpy(&hi, rdram + (address & kRamMask) + 8, 8);
                const uint32_t qwc = static_cast<uint32_t>(lo & 0xFFFF);
                const uint32_t id = static_cast<uint32_t>((lo >> 28) & 7);
                const uint32_t tagAddress = static_cast<uint32_t>((lo >> 32) & 0x7FFFFFF0u);
                if (tagTransfer)
                {
                    // The two NOPs before the tag's upper half; a CALL of a microcode upload packet gets the packet's
                    // address in them (render_vif.h), so the VIF knows which program its MPG loads.
                    uint32_t nops[2] = {0, 0};
                    if (id == 5 && tagAddress >= kMicrocodeStart && tagAddress < kMicrocodeEnd)
                    {
                        nops[0] = (kProgramMarkLow << 16) | (tagAddress & 0xFFFF);
                        nops[1] = (kProgramMarkHigh << 16) | (tagAddress >> 16);
                    }
                    const auto *bytes = reinterpret_cast<const uint8_t *>(nops);
                    out.insert(out.end(), bytes, bytes + 8);
                    const uint8_t *upper = rdram + (address & kRamMask) + 8;
                    out.insert(out.end(), upper, upper + 8);
                }
                const bool dataElsewhere = id == 0 || id == 3 || id == 4; // REFE, REF, REFS
                const uint32_t data = dataElsewhere ? tagAddress : address + 16u;
                if ((data & kRamMask) + qwc * 16u <= 0x02000000u)
                {
                    const uint8_t *from = rdram + (data & kRamMask);
                    out.insert(out.end(), from, from + qwc * 16u);
                }
                const uint32_t after = dataElsewhere ? address + 16u : address + 16u + qwc * 16u;
                switch (id)
                {
                case 0: // REFE
                case 7: // END
                    return;
                case 1: // CNT
                case 3: // REF
                case 4: // REFS
                    address = after;
                    break;
                case 2: // NEXT
                    address = tagAddress;
                    break;
                case 5: // CALL
                    if (programCalls != nullptr && tagAddress >= kMicrocodeStart && tagAddress < kMicrocodeEnd)
                        ++(*programCalls)[tagAddress];
                    if (depth >= 2)
                        return;
                    stack[depth++] = after;
                    address = tagAddress;
                    break;
                case 6: // RET
                    if (depth == 0)
                        return;
                    address = stack[--depth];
                    break;
                default:
                    return;
                }
            }
        }

        void publishFrame(Renderer &r)
        {
            Frame frame = r.gs.takeFrame();
            // MMPC_RENDER_STATS=1: every 120 frames, what the last one held (batches, vertices, their extent).
            static const bool s_stats = [] {
                const char *v = std::getenv("MMPC_RENDER_STATS");
                return v != nullptr && v[0] == '1';
            }();
            if (s_stats && frame.number % 120u == 0u)
            {
                float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
                for (const Vertex &v : frame.vertices)
                {
                    minX = std::min(minX, v.x), maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y), maxY = std::max(maxY, v.y);
                }
                uint32_t textured = 0, previous = 0;
                for (const Batch &b : frame.batches)
                {
                    textured += b.state.textured;
                    previous += b.state.textured && b.state.texture == kPreviousFrame ? 1u : 0u;
                }
                std::cerr << "[render] frame " << frame.number << ": " << frame.batches.size() << " batches (" << textured
                          << " textured, " << previous << " from the previous frame), " << frame.vertices.size() << " vertices, x " << minX << ".." << maxX << ", y " << minY
                          << ".." << maxY << ", " << frame.newTextures.size() << " new textures; " << r.gifTransfers
                          << " GIF transfers, " << r.otherVifTransfers << " other VIF1 transfers; programs";
                for (const auto &entry : r.programCalls)
                    std::cerr << " " << std::hex << entry.first << std::dec << " x" << entry.second;
                std::cerr << "; runs without a native program";
                for (const auto &entry : r.vif.missingRuns())
                    std::cerr << " " << std::hex << entry.first << std::dec << " x" << entry.second;
                std::cerr << std::endl;
            }
            r.programCalls.clear();
            r.vif.missingRuns().clear();
            r.gifTransfers = r.otherVifTransfers = 0;
            std::lock_guard<std::mutex> lock(r.mutex);
            for (auto &entry : frame.newTextures)
                r.pendingTextures[entry.first] = std::move(entry.second);
            frame.newTextures.clear();
            r.latest = std::move(frame);
            if (!r.evicted.empty())
            {
                r.gs.forget(r.evicted);
                r.evicted.clear();
            }
            r.fresh = true;
        }

        void onDmaStart(void *user, uint32_t channel, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr)
        {
            Renderer &r = *static_cast<Renderer *>(user);
            const uint8_t *rdram = r.runtime->memory().getRDRAM();
            const bool chainMode = ((chcr >> 2) & 3) == 1;
            if (const char *test = std::getenv("MMPC_RENDER_VU1TEST")) // the game is loaded by its first transfer
            {
                runVu1RecordTest(test, rdram);
                std::exit(0);
            }
            // The drawing environment as the emulated GS has it now: the game's double-buffer setup writes it directly.
            for (int ctx = 0; ctx < 2; ++ctx)
            {
                const GSContext &c = r.runtime->gs().getContext(ctx);
                const uint64_t xyoffset = static_cast<uint64_t>(c.xyoffset.ofx) | (static_cast<uint64_t>(c.xyoffset.ofy) << 32);
                const uint64_t scissor = static_cast<uint64_t>(c.scissor.x0) | (static_cast<uint64_t>(c.scissor.x1) << 16) |
                                         (static_cast<uint64_t>(c.scissor.y0) << 32) | (static_cast<uint64_t>(c.scissor.y1) << 48);
                const uint64_t zbuf = static_cast<uint64_t>(c.zbuf.zbp) | (static_cast<uint64_t>(c.zbuf.psm) << 24) |
                                      (static_cast<uint64_t>(c.zbuf.zmask ? 1u : 0u) << 32);
                const uint64_t frame = static_cast<uint64_t>(c.frame.fbp) | (static_cast<uint64_t>(c.frame.fbw) << 16) |
                                       (static_cast<uint64_t>(c.frame.psm) << 24) | (static_cast<uint64_t>(c.frame.fbmsk) << 32);
                r.gs.setEnvironment(ctx, frame, xyoffset, scissor, zbuf, c.test);
            }
            // A picture drawn outside the frame lists (the movie player draws each field from its vertical blank handler) is closed
            // at the first transfer after a vertical blank; while frame lists run, they close their own frames.
            const uint64_t vsync = ps2_syscalls::GetCurrentVSyncTick(r.runtime);
            if (vsync != r.lastVsync)
            {
                if (vsync > r.frameListVsync + 2u && !r.gs.frame().batches.empty())
                    publishFrame(r);
                r.lastVsync = vsync;
            }
            r.stream.clear();
            if (channel == 0x10009000u) // VIF1
            {
                const bool tagTransfer = ((chcr >> 6) & 1) != 0;
                if (chainMode)
                    walkChain(rdram, tadr, tagTransfer, r.stream, &r.programCalls);
                else if ((madr & kRamMask) + qwc * 16u <= 0x02000000u)
                    r.stream.assign(rdram + (madr & kRamMask), rdram + (madr & kRamMask) + qwc * 16u);
                r.vif.run(r.stream.data(), r.stream.size());
                const uint32_t chainStart = tadr & 0x7FFFFFF0u;
                const bool frameChain = chainMode && (chainStart == (load32(rdram, kDmaLists) & 0x7FFFFFF0u) ||
                                                      chainStart == (load32(rdram, kDmaLists + 4u) & 0x7FFFFFF0u));
                static const bool s_stats = [] {
                    const char *v = std::getenv("MMPC_RENDER_STATS");
                    return v != nullptr && v[0] == '1';
                }();
                if (s_stats && !frameChain && r.otherVifTransfers % 300u == 299u)
                    std::cerr << "[render] no frame list for " << (r.otherVifTransfers + 1) << " VIF1 transfers: last chcr "
                              << std::hex << chcr << " madr " << madr << " qwc " << qwc << " tadr " << tadr << ", lists "
                              << load32(rdram, kDmaLists) << " " << load32(rdram, kDmaLists + 4u) << std::dec << std::endl;
                if (frameChain)
                {
                    publishFrame(r);
                    r.frameListVsync = vsync;
                    r.otherVifTransfers = static_cast<uint32_t>(-1); // this one is counted below, so the next frame starts at 0
                    if (r.mode == Mode::Side) // F2 switches the picture shown
                    {
                        const bool down = ps2x::host::keyDown(SDL_SCANCODE_F2);
                        if (down && !r.f2Down)
                        {
                            r.showNative = !r.showNative;
                            std::cerr << "[render] showing the " << (r.showNative ? "native" : "emulated") << " picture" << std::endl;
                        }
                        r.f2Down = down;
                    }
                }
            }
            if (channel == 0x10009000u)
                ++r.otherVifTransfers;
            else // GIF
            {
                ++r.gifTransfers;
                if (chainMode)
                    walkChain(rdram, tadr, false, r.stream);
                else if ((madr & kRamMask) + qwc * 16u <= 0x02000000u)
                    r.stream.assign(rdram + (madr & kRamMask), rdram + (madr & kRamMask) + qwc * 16u);
                r.gs.gif(r.stream.data(), r.stream.size());
            }
        }

        // ---- the window thread: drawing ----

        SDL_GPUShader *makeShader(SDL_GPUDevice *device, SDL_GPUShaderStage stage)
        {
            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
            SDL_GPUShaderCreateInfo info{};
            const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
            {
                info.format = SDL_GPU_SHADERFORMAT_DXIL;
                info.code = vertex ? kDrawVsDxil : kDrawPsDxil;
                info.code_size = vertex ? sizeof(kDrawVsDxil) : sizeof(kDrawPsDxil);
            }
            else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
            {
                info.format = SDL_GPU_SHADERFORMAT_SPIRV;
                info.code = vertex ? kDrawVsSpirv : kDrawPsSpirv;
                info.code_size = vertex ? sizeof(kDrawVsSpirv) : sizeof(kDrawPsSpirv);
            }
            else
                return nullptr;
            info.entrypoint = vertex ? "VSMain" : "PSMain";
            info.stage = stage;
            info.num_samplers = vertex ? 0 : 1;
            info.num_uniform_buffers = vertex ? 0 : 1;
            return SDL_CreateGPUShader(device, &info);
        }

        SDL_GPUTexture *makeTexture(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, uint32_t w,
                                    uint32_t h)
        {
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = format;
            info.usage = usage;
            info.width = w;
            info.height = h;
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            return SDL_CreateGPUTexture(device, &info);
        }

        bool initGpu(Renderer &r)
        {
            if (r.device != nullptr || r.gpuFailed)
                return !r.gpuFailed;
            r.device = ps2x::host::device();
            if (r.device == nullptr)
                return !(r.gpuFailed = true);
            r.vertexShader = makeShader(r.device, SDL_GPU_SHADERSTAGE_VERTEX);
            r.pixelShader = makeShader(r.device, SDL_GPU_SHADERSTAGE_FRAGMENT);
            if (r.vertexShader == nullptr || r.pixelShader == nullptr)
            {
                std::cerr << "[render] shaders failed: " << SDL_GetError() << std::endl;
                return !(r.gpuFailed = true);
            }
            {
                SDL_GPUSamplerCreateInfo info{};
                info.min_filter = info.mag_filter = SDL_GPU_FILTER_NEAREST;
                info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                r.sampler = SDL_CreateGPUSampler(r.device, &info);
            }
            const uint32_t w = 640u * r.scale, h = 448u * r.scale;
            for (SDL_GPUTexture *&color : r.colors)
                color = makeTexture(r.device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
            r.depth = makeTexture(r.device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, w, h);
            r.white = makeTexture(r.device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
            if (r.colors[0] == nullptr || r.colors[1] == nullptr || r.depth == nullptr || r.white == nullptr)
            {
                std::cerr << "[render] render targets failed: " << SDL_GetError() << std::endl;
                return !(r.gpuFailed = true);
            }
            std::cerr << "[render] native renderer at " << w << " x " << h << std::endl;
            return true;
        }

        SDL_GPUGraphicsPipeline *pipelineFor(Renderer &r, const DrawState &s)
        {
            const uint32_t key = static_cast<uint32_t>(s.topology) | (s.blend << 1) | (s.colorWrite << 4) | (s.depthTest << 5) |
                                 (s.depthWrite << 7);
            auto found = r.pipelines.find(key);
            if (found != r.pipelines.end())
                return found->second;

            SDL_GPUVertexBufferDescription buffer{};
            buffer.slot = 0;
            buffer.pitch = sizeof(Vertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            SDL_GPUVertexAttribute attributes[5]{};
            attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(Vertex, x))};
            attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(Vertex, z))};
            attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(Vertex, rgba))};
            attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(Vertex, s))};
            attributes[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(Vertex, fog))};

            SDL_GPUColorTargetDescription target{};
            target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            SDL_GPUColorTargetBlendState &blend = target.blend_state;
            blend.enable_blend = s.blend != BlendNone;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            switch (s.blend)
            {
            case BlendMix:
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                break;
            case BlendAdd:
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                break;
            case BlendMixFix:
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
                break;
            case BlendAddFix:
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                break;
            case BlendSubtract:
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blend.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
                break;
            default:
                break;
            }
            blend.enable_color_write_mask = true;
            blend.color_write_mask = s.colorWrite ? (SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B |
                                                     SDL_GPU_COLORCOMPONENT_A)
                                                  : 0;

            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = r.vertexShader;
            info.fragment_shader = r.pixelShader;
            info.vertex_input_state.vertex_buffer_descriptions = &buffer;
            info.vertex_input_state.num_vertex_buffers = 1;
            info.vertex_input_state.vertex_attributes = attributes;
            info.vertex_input_state.num_vertex_attributes = 5;
            info.primitive_type = s.topology == Topology::Lines ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.depth_stencil_state.enable_depth_test = true;
            info.depth_stencil_state.enable_depth_write = s.depthWrite != 0;
            static const SDL_GPUCompareOp kDepth[4] = {SDL_GPU_COMPAREOP_NEVER, SDL_GPU_COMPAREOP_ALWAYS,
                                                       SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER};
            info.depth_stencil_state.compare_op = kDepth[s.depthTest & 3];
            info.target_info.color_target_descriptions = &target;
            info.target_info.num_color_targets = 1;
            info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            info.target_info.has_depth_stencil_target = true;
            SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(r.device, &info);
            if (pipeline == nullptr)
                std::cerr << "[render] pipeline failed: " << SDL_GetError() << std::endl;
            r.pipelines.emplace(key, pipeline);
            return pipeline;
        }

        // Uploads the textures that came with the frame and the frame's vertices (one copy pass).
        void upload(Renderer &r, SDL_GPUCommandBuffer *commands, std::unordered_map<uint64_t, TextureImage> &newTextures)
        {
            size_t textureBytes = 0;
            for (auto &entry : newTextures)
                if (r.textures.find(entry.first) == r.textures.end())
                    textureBytes += entry.second.pixels.size() * 4u;
            const uint32_t vertexBytes = static_cast<uint32_t>(r.current.vertices.size() * sizeof(Vertex));
            if (vertexBytes > r.vertexCapacity)
            {
                if (r.vertexBuffer != nullptr)
                    SDL_ReleaseGPUBuffer(r.device, r.vertexBuffer);
                if (r.vertexUpload != nullptr)
                    SDL_ReleaseGPUTransferBuffer(r.device, r.vertexUpload);
                r.vertexCapacity = std::max(vertexBytes * 2u, 1u << 20);
                SDL_GPUBufferCreateInfo info{};
                info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                info.size = r.vertexCapacity;
                r.vertexBuffer = SDL_CreateGPUBuffer(r.device, &info);
                SDL_GPUTransferBufferCreateInfo up{};
                up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                up.size = r.vertexCapacity;
                r.vertexUpload = SDL_CreateGPUTransferBuffer(r.device, &up);
            }
            SDL_GPUTransferBuffer *textureUpload = nullptr;
            if (textureBytes > 0)
            {
                SDL_GPUTransferBufferCreateInfo up{};
                up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                up.size = static_cast<Uint32>(textureBytes);
                textureUpload = SDL_CreateGPUTransferBuffer(r.device, &up);
            }
            SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
            if (vertexBytes > 0 && r.vertexUpload != nullptr)
            {
                void *mapped = SDL_MapGPUTransferBuffer(r.device, r.vertexUpload, true);
                std::memcpy(mapped, r.current.vertices.data(), vertexBytes);
                SDL_UnmapGPUTransferBuffer(r.device, r.vertexUpload);
                SDL_GPUTransferBufferLocation from{r.vertexUpload, 0};
                SDL_GPUBufferRegion to{r.vertexBuffer, 0, vertexBytes};
                SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            if (textureUpload != nullptr)
            {
                auto *mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(r.device, textureUpload, false));
                uint32_t offset = 0;
                for (auto &entry : newTextures)
                {
                    if (r.textures.find(entry.first) != r.textures.end())
                        continue;
                    const TextureImage &image = entry.second;
                    SDL_GPUTexture *texture = makeTexture(r.device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                          image.width, image.height);
                    if (texture == nullptr)
                        continue;
                    std::memcpy(mapped + offset, image.pixels.data(), image.pixels.size() * 4u);
                    SDL_GPUTextureTransferInfo source{};
                    source.transfer_buffer = textureUpload;
                    source.offset = offset;
                    source.pixels_per_row = image.width;
                    source.rows_per_layer = image.height;
                    SDL_GPUTextureRegion region{};
                    region.texture = texture;
                    region.w = image.width;
                    region.h = image.height;
                    region.d = 1;
                    // the copy is recorded now and reads the transfer buffer when the command buffer runs
                    r.textures[entry.first] = GpuTexture{texture, r.framesDrawn};
                    SDL_UploadToGPUTexture(copy, &source, &region, false);
                    offset += static_cast<uint32_t>(image.pixels.size() * 4u);
                }
                SDL_UnmapGPUTransferBuffer(r.device, textureUpload);
            }
            static const uint32_t kWhite = 0xFFFFFFFFu;
            static bool s_whiteDone = false;
            SDL_GPUTransferBuffer *whiteUpload = nullptr;
            if (!s_whiteDone)
            {
                SDL_GPUTransferBufferCreateInfo up{};
                up.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                up.size = 4;
                whiteUpload = SDL_CreateGPUTransferBuffer(r.device, &up);
                std::memcpy(SDL_MapGPUTransferBuffer(r.device, whiteUpload, false), &kWhite, 4);
                SDL_UnmapGPUTransferBuffer(r.device, whiteUpload);
                SDL_GPUTextureTransferInfo source{};
                source.transfer_buffer = whiteUpload;
                SDL_GPUTextureRegion region{};
                region.texture = r.white;
                region.w = region.h = region.d = 1;
                SDL_UploadToGPUTexture(copy, &source, &region, false);
                s_whiteDone = true;
            }
            SDL_EndGPUCopyPass(copy);
            if (textureUpload != nullptr)
                SDL_ReleaseGPUTransferBuffer(r.device, textureUpload); // released once the GPU is done with it
            if (whiteUpload != nullptr)
                SDL_ReleaseGPUTransferBuffer(r.device, whiteUpload);
            newTextures.clear();
        }

        void drawFrame(Renderer &r, SDL_GPUCommandBuffer *commands)
        {
            // Draw into the other target: the one shown until now becomes the previous frame (read by kPreviousFrame draws).
            r.target ^= 1;
            SDL_GPUTexture *previous = r.colors[r.target ^ 1];
            SDL_GPUColorTargetInfo colorTarget{};
            colorTarget.texture = r.colors[r.target];
            colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
            colorTarget.clear_color = SDL_FColor{0, 0, 0, 1};
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo depthTarget{};
            depthTarget.texture = r.depth;
            depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
            depthTarget.clear_depth = 0.0f;
            depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
            depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, &depthTarget);
            const float s = static_cast<float>(r.scale);
            SDL_GPUViewport viewport{0, 0, 640.0f * s, 448.0f * s, 0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &viewport);
            if (r.vertexBuffer != nullptr)
            {
                SDL_GPUBufferBinding binding{r.vertexBuffer, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            }
            SDL_GPUGraphicsPipeline *bound = nullptr;
            for (const Batch &batch : r.current.batches)
            {
                const DrawState &st = batch.state;
                SDL_GPUGraphicsPipeline *pipeline = pipelineFor(r, st);
                if (pipeline == nullptr)
                    continue;
                if (pipeline != bound)
                {
                    SDL_BindGPUGraphicsPipeline(pass, pipeline);
                    bound = pipeline;
                }
                // The GS's scissor in 1x pixels, inclusive.
                const int scale = static_cast<int>(r.scale);
                const int x0 = st.scissor[0] * scale, y0 = st.scissor[2] * scale;
                const int x1 = std::min((st.scissor[1] + 1) * scale, 640 * scale), y1 = std::min((st.scissor[3] + 1) * scale, 448 * scale);
                SDL_Rect scissor{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
                SDL_SetGPUScissor(pass, &scissor);
                if (st.blend == BlendMixFix || st.blend == BlendAddFix)
                {
                    const float f = std::min(1.0f, st.fixAlpha / 128.0f);
                    SDL_SetGPUBlendConstants(pass, SDL_FColor{f, f, f, f});
                }
                SDL_GPUTexture *texture = r.white;
                if (st.textured && st.texture == kPreviousFrame)
                    texture = previous;
                else if (st.textured)
                {
                    auto found = r.textures.find(st.texture);
                    if (found != r.textures.end())
                    {
                        texture = found->second.texture;
                        found->second.lastUsed = r.framesDrawn;
                    }
                }
                SDL_GPUTextureSamplerBinding sampler{texture, r.sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
                DrawParams params{};
                params.flags = (st.textured ? 1u : 0u) | (st.tcc ? 2u : 0u) | (st.fog ? 4u : 0u) | (st.linear ? 8u : 0u) |
                               (static_cast<uint32_t>(st.snap & 3) << 4) | (static_cast<uint32_t>(st.tfx) << 8) |
                               (static_cast<uint32_t>(st.wms & 3) << 12) | (static_cast<uint32_t>(st.wmt & 3) << 14);
                params.alphaTest = st.alphaTest;
                params.alphaRef = static_cast<float>(st.alphaRef);
                params.scale = s;
                for (int i = 0; i < 4; ++i)
                    params.region[i] = st.region[i];
                params.fogColor[0] = static_cast<float>(st.fogColor & 0xFF) / 255.0f;
                params.fogColor[1] = static_cast<float>((st.fogColor >> 8) & 0xFF) / 255.0f;
                params.fogColor[2] = static_cast<float>((st.fogColor >> 16) & 0xFF) / 255.0f;
                params.fogColor[3] = 1.0f;
                SDL_PushGPUFragmentUniformData(commands, 0, &params, sizeof(params));
                SDL_DrawGPUPrimitives(pass, batch.count, 1, batch.first, 0);
            }
            SDL_EndGPURenderPass(pass);
        }

        // Fits a w x h picture at 4:3 into the swapchain image.
        void fit(uint32_t width, uint32_t height, Uint32 &x, Uint32 &y, Uint32 &w, Uint32 &h)
        {
            float dw = static_cast<float>(width), dh = dw * 3.0f / 4.0f;
            if (dh > static_cast<float>(height))
            {
                dh = static_cast<float>(height);
                dw = dh * 4.0f / 3.0f;
            }
            w = static_cast<Uint32>(dw);
            h = static_cast<Uint32>(dh);
            x = (width - w) / 2u;
            y = (height - h) / 2u;
        }

        // Draws the newest frame from the game thread into the render target, if one came since the last present.
        void updateNative(Renderer &r, SDL_GPUCommandBuffer *commands)
        {
            {
                bool fresh = false;
                std::unordered_map<uint64_t, TextureImage> newTextures;
                {
                    std::lock_guard<std::mutex> lock(r.mutex);
                    if (r.fresh)
                    {
                        r.current = std::move(r.latest);
                        r.latest = Frame{};
                        newTextures.swap(r.pendingTextures);
                        r.fresh = false;
                        fresh = true;
                    }
                }
                if (fresh)
                {
                    ++r.framesDrawn;
                    upload(r, commands, newTextures);
                    drawFrame(r, commands);
                    // textures no frame has used for a while
                    if (r.framesDrawn % 120u == 0u)
                        for (auto it = r.textures.begin(); it != r.textures.end();)
                        {
                            if (r.framesDrawn - it->second.lastUsed > 600u)
                            {
                                SDL_ReleaseGPUTexture(r.device, it->second.texture);
                                {
                                    std::lock_guard<std::mutex> lock(r.mutex);
                                    r.evicted.push_back(it->first);
                                }
                                it = r.textures.erase(it);
                            }
                            else
                                ++it;
                        }
                }
            }
        }

        // One picture into the area (x, y, w, h) of the swapchain image, at 4:3: the native render target or the emulated frame.
        void show(Renderer &r, SDL_GPUCommandBuffer *commands, SDL_GPUTexture *swapchain, uint32_t x, uint32_t y, uint32_t w,
                  uint32_t h, bool native, const ps2x::host::EmulatedFrame &emulated, bool clear)
        {
            SDL_GPUBlitInfo blit{};
            fit(w, h, blit.destination.x, blit.destination.y, blit.destination.w, blit.destination.h);
            blit.destination.x += x;
            blit.destination.y += y;
            blit.destination.texture = swapchain;
            blit.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            blit.clear_color = SDL_FColor{0, 0, 0, 1};
            if (native)
            {
                blit.source.texture = r.colors[r.target];
                blit.source.w = 640u * r.scale;
                blit.source.h = 448u * r.scale;
                blit.filter = SDL_GPU_FILTER_LINEAR;
            }
            else
            {
                if (emulated.texture == nullptr)
                    return;
                blit.source.texture = emulated.texture;
                blit.source.w = static_cast<Uint32>(emulated.sourceWidth);
                blit.source.h = static_cast<Uint32>(emulated.sourceHeight);
                blit.filter = SDL_GPU_FILTER_NEAREST;
            }
            SDL_BlitGPUTexture(commands, &blit);
        }

        void present(void *user, SDL_GPUCommandBuffer *commands, SDL_GPUTexture *swapchain, uint32_t width, uint32_t height,
                     const ps2x::host::EmulatedFrame &emulated)
        {
            Renderer &r = *static_cast<Renderer *>(user);
            const bool gpu = initGpu(r);
            if (gpu)
                updateNative(r, commands);
            if (r.mode == Mode::Split && gpu)
            {
                // emulated on the left, native on the right, each fitted at 4:3 into its half
                show(r, commands, swapchain, 0, 0, width / 2u, height, false, emulated, true);
                show(r, commands, swapchain, width / 2u, 0, width - width / 2u, height, true, emulated, false);
                return;
            }
            const bool native = gpu && (r.mode == Mode::Native || r.showNative);
            show(r, commands, swapchain, 0, 0, width, height, native, emulated, true);
        }
    }

    void install(PS2Runtime &runtime)
    {
        const char *value = std::getenv("MMPC_RENDERER");
        if (value == nullptr)
            return;
        Renderer &r = renderer();
        if (std::strcmp(value, "native") == 0)
            r.mode = Mode::Native;
        else if (std::strcmp(value, "side") == 0)
            r.mode = Mode::Side;
        else if (std::strcmp(value, "split") == 0)
            r.mode = Mode::Split;
        else
            return;
        r.runtime = &runtime;
        if (const char *scale = std::getenv("MMPC_RENDER_SCALE"))
            r.scale = static_cast<uint32_t>(std::clamp(std::atoi(scale), 1, 8));
        runtime.memory().setDmaStartObserver(onDmaStart, &r);
        ps2x::host::setReplacement(present, &r);
        std::cerr << "[render] native renderer installed (" << value << ", scale " << r.scale << ")" << std::endl;
    }
}
