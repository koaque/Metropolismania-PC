// Enhancements beyond the original game, off unless asked for. See enhance.h.

#include "enhance.h"

#include "game_globals.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace port::enhance
{
    float widescreen()
    {
        static const float factor = [] {
            const char *value = std::getenv("MMPC_WIDESCREEN");
            if (value == nullptr || value[0] == '\0' || std::strcmp(value, "0") == 0)
                return 1.0f;
            const char *renderer = std::getenv("MMPC_RENDERER");
            if (renderer == nullptr || (std::strcmp(renderer, "native") != 0 && std::strcmp(renderer, "split") != 0 &&
                                        std::strcmp(renderer, "side") != 0))
            {
                std::cerr << "[mm] widescreen: off, it needs the native renderer (MMPC_RENDERER=native)" << std::endl;
                return 1.0f;
            }
            float w = 16.0f, h = 9.0f;
            if (std::strcmp(value, "1") != 0 && std::sscanf(value, "%f:%f", &w, &h) != 2)
                return 1.0f;
            const float k = std::clamp((w / h) / (4.0f / 3.0f), 1.0f, 3.0f);
            std::cerr << "[mm] widescreen: the city's view " << k << " times as wide as 4:3 (MMPC_WIDESCREEN=" << value << ")"
                      << std::endl;
            return k;
        }();
        return factor;
    }

    float viewDistance()
    {
        static const float factor = [] {
            const char *value = std::getenv("MMPC_VIEW_DISTANCE");
            if (value == nullptr || value[0] == '\0' || std::strcmp(value, "off") == 0 || std::strcmp(value, "1") == 0)
                return 1.0f;
            const char *renderer = std::getenv("MMPC_RENDERER");
            if (renderer == nullptr || (std::strcmp(renderer, "native") != 0 && std::strcmp(renderer, "split") != 0 &&
                                        std::strcmp(renderer, "side") != 0))
            {
                std::cerr << "[mm] view distance: off, it needs the native renderer (MMPC_RENDERER=native)" << std::endl;
                return 1.0f;
            }
            // 4 at most (max): farther, the render thread could not keep 60 frames a second looking along the streets
            // (measured 2026-11-18: 8 times, which reached the whole map from anywhere, ran at 13 to 24).
            constexpr float kMost = 4.0f;
            const float asked = std::strcmp(value, "max") == 0 ? kMost : static_cast<float>(std::atof(value));
            const float f = std::clamp(asked, 1.0f, kMost);
            std::cerr << "[mm] view distance: the city drawn " << f << " times as far (MMPC_VIEW_DISTANCE=" << value
                      << (asked > kMost ? ", 4 at most" : "") << ")" << std::endl;
            return f;
        }();
        return factor;
    }

    bool extrasOn()
    {
        static const bool on = widescreen() != 1.0f || viewDistance() != 1.0f;
        return on;
    }

    namespace
    {
        bool s_sidePass = false;
        uint32_t s_cityWorld = 0;
        // tiles from -64 to 191 on both axes (the walk's window reaches past the map's edges)
        constexpr int kTileOrigin = 64, kTileSide = 256;
        std::vector<uint8_t> &ps2Tiles()
        {
            static std::vector<uint8_t> tiles(kTileSide * kTileSide, 0);
            return tiles;
        }
        int tileIndex(int col, int row)
        {
            col += kTileOrigin;
            row += kTileOrigin;
            return col < 0 || row < 0 || col >= kTileSide || row >= kTileSide ? -1 : row * kTileSide + col;
        }
    }

    bool inSidePass() { return s_sidePass; }
    void setSidePass(bool on) { s_sidePass = on; }
    void noteCityWorld(uint32_t city) { s_cityWorld = city; }
    uint32_t takeCityWorld() { return std::exchange(s_cityWorld, 0u); }

    void clearPs2Tiles() { std::fill(ps2Tiles().begin(), ps2Tiles().end(), uint8_t{0}); }
    void notePs2Tile(int col, int row)
    {
        if (const int i = tileIndex(col, row); i >= 0)
            ps2Tiles()[static_cast<size_t>(i)] = 1;
    }
    bool isPs2Tile(int col, int row)
    {
        const int i = tileIndex(col, row);
        return i >= 0 && ps2Tiles()[static_cast<size_t>(i)] != 0;
    }

    std::atomic<uint64_t> &wideFrames()
    {
        static std::atomic<uint64_t> frames{0};
        return frames;
    }

    namespace
    {
        // tag address -> span; few per frame, and taken as the lists go out (a list that never does is dropped with the
        // oldest marks)
        std::vector<std::pair<uint32_t, uint8_t>> &marks()
        {
            static std::vector<std::pair<uint32_t, uint8_t>> list;
            return list;
        }
    }

    void markFrameList(Span span)
    {
        if (!extrasOn())
            return;
        auto &list = marks();
        const uint32_t address = mm::globals::dmaListPtr.get() & 0x7FFFFFF0u;
        for (auto &mark : list)
            if (mark.first == address)
            {
                mark.second = static_cast<uint8_t>(span);
                return;
            }
        if (list.size() >= 64)
            list.erase(list.begin());
        list.emplace_back(address, static_cast<uint8_t>(span));
    }

    uint8_t takeFrameListMark(uint32_t tagAddress)
    {
        auto &list = marks();
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].first == tagAddress)
            {
                const uint8_t span = list[i].second;
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                return span;
            }
        return 0;
    }

    bool frameListMarked() { return !marks().empty(); }
}
