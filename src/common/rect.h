// src/common/rect.h — géométrie entière, sans allocation, sans exception.
#pragma once
#include <algorithm>
#include <cstdint>

namespace visio {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    constexpr int right()  const noexcept { return x + w; }
    constexpr int bottom() const noexcept { return y + h; }
    constexpr bool empty() const noexcept { return w <= 0 || h <= 0; }
    constexpr int64_t area() const noexcept { return empty() ? 0 : (int64_t)w * h; }

    constexpr bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < right() && py < bottom();
    }
    constexpr bool contains(const Rect& o) const noexcept {
        return !o.empty() && o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
    }

    friend constexpr bool operator==(const Rect& a, const Rect& b) noexcept {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }
};

// Intersection : un rectangle vide si les deux ne se touchent pas.
inline constexpr Rect intersect(const Rect& a, const Rect& b) noexcept {
    if (a.empty() || b.empty()) return {};
    const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

// Enveloppe : le plus petit rectangle qui contient les deux.
inline constexpr Rect unite(const Rect& a, const Rect& b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.right(), b.right()), y1 = std::max(a.bottom(), b.bottom());
    return {x0, y0, x1 - x0, y1 - y0};
}

inline constexpr Rect unite(const Rect& a, const Rect& b, const Rect& c) noexcept {
    return unite(unite(a, b), c);
}

// Clamp dans un écran : ce qui dépasse est coupé, pas déplacé.
inline constexpr Rect clamp(const Rect& r, const Rect& screen) noexcept {
    return intersect(r, screen);
}

// Quantification : deux cases distantes de moins de q px sur chaque bord sont la même.
// Sert à la stabilité des identifiants (D5, plan d'architecture §3).
inline constexpr Rect quantize(const Rect& r, int q) noexcept {
    if (q <= 1 || r.empty()) return r;
    const int x0 = (r.x / q) * q, y0 = (r.y / q) * q;
    const int x1 = ((r.right() + q - 1) / q) * q, y1 = ((r.bottom() + q - 1) / q) * q;
    return {x0, y0, x1 - x0, y1 - y0};
}

// Découpage en tuiles alignées sur la grille de q px.
template <typename F>
inline void for_each_tile(const Rect& r, int q, F&& f) {
    for (int ty = r.y; ty < r.bottom(); ty += q) {
        for (int tx = r.x; tx < r.right(); tx += q) {
            f(Rect{tx, ty, std::min(q, r.right() - tx), std::min(q, r.bottom() - ty)});
        }
    }
}

} // namespace visio
