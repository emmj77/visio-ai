// src/core/frame.h — une région de l'écran et ses pixels, en mémoire locale.
#pragma once
#include "common/rect.h"

#include <cstdint>
#include <vector>

namespace visio {

struct Frame {
    Rect region{};
    int  stride = 0;              // octets par ligne
    std::vector<uint32_t> px;     // region.w * region.h, ligne par ligne (pas de remplissage)

    static Frame pour(const Rect& r) {
        Frame f;
        f.region = r;
        f.stride = r.w * 4;
        f.px.assign(r.empty() ? 0 : (size_t)r.w * r.h, 0);
        return f;
    }

    bool vide() const noexcept { return region.empty() || px.empty(); }

    const uint32_t* ligne(int y) const noexcept {
        return px.data() + (size_t)(y - region.y) * region.w;
    }

    uint32_t at(int x, int y) const noexcept {
        return px[(size_t)(y - region.y) * region.w + (x - region.x)];
    }

    void set(int x, int y, uint32_t v) noexcept {
        px[(size_t)(y - region.y) * region.w + (x - region.x)] = v;
    }

    void remplir(uint32_t v) noexcept { for (auto& p : px) p = v; }
};

} // namespace visio
