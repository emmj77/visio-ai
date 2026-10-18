#include "core/tile_diff.h"

#include <algorithm>
#include <cstring>

namespace visio {

bool frames_identiques(const Frame& a, const Frame& b) noexcept {
    if (a.region != b.region || a.px.size() != b.px.size()) return false;
    return std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(uint32_t)) == 0;
}

std::vector<TileHit> tile_diff(const Frame& a, const Frame& b, int tuile, int seuil) {
    std::vector<TileHit> hits;
    if (a.vide() || b.vide()) return hits;
    if (a.region != b.region) {
        hits.push_back(TileHit{a.region, (int)a.region.area()});
        return hits;
    }

    const Rect& r = a.region;
    for (int ty = 0; ty < r.h; ty += tuile) {
        for (int tx = 0; tx < r.w; tx += tuile) {
            const int w = std::min(tuile, r.w - tx);
            const int h = std::min(tuile, r.h - ty);
            const uint32_t* la = a.px.data() + (size_t)ty * r.w + tx;
            const uint32_t* lb = b.px.data() + (size_t)ty * r.w + tx;
            const size_t octets = (size_t)w * sizeof(uint32_t);

            bool identique = true;
            for (int y = 0; y < h && identique; ++y) {
                if (std::memcmp(la + (size_t)y * r.w, lb + (size_t)y * r.w, octets) != 0)
                    identique = false;
            }
            if (identique) continue;

            int n = 0;
            for (int y = 0; y < h; ++y) {
                const uint32_t* pa = la + (size_t)y * r.w;
                const uint32_t* pb = lb + (size_t)y * r.w;
                for (int x = 0; x < w; ++x) n += (pa[x] != pb[x]);
            }
            if (n > seuil) hits.push_back(TileHit{Rect{r.x + tx, r.y + ty, w, h}, n});
        }
    }
    return hits;
}

} // namespace visio
