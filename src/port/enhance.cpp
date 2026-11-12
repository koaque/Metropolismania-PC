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
        if (widescreen() == 1.0f)
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
