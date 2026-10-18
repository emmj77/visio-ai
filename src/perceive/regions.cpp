#include "perceive/regions.h"

#include <algorithm>

namespace visio {

namespace {

// Un segment horizontal de couleur uniforme. C'est la bonne unité de travail : une barre
// d'outils fait une vingtaine de segments par ligne, pas mille pixels. La version « un
// nœud par pixel » coûtait 38 ms pour 1 Mpx, mesuré sur une fenêtre Firefox.
struct Seg {
    int      y = 0, x0 = 0, x1 = 0;   // [x0, x1)
    uint32_t cle = 0;                 // couleur quantifiée
    int32_t  parent = -1;
    int32_t  taille = 1;
};

inline uint32_t cle_couleur(uint32_t argb, int quant) {
    const uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return ((r >> quant) << 16) | ((g >> quant) << 8) | (b >> quant);
}

int32_t racine(std::vector<Seg>& seg, int32_t i) {
    while (seg[(size_t)i].parent != i) {
        seg[(size_t)i].parent = seg[(size_t)seg[(size_t)i].parent].parent;   // compression
        i = seg[(size_t)i].parent;
    }
    return i;
}

void unir(std::vector<Seg>& seg, int32_t a, int32_t b) {
    a = racine(seg, a);
    b = racine(seg, b);
    if (a == b) return;
    if (seg[(size_t)a].taille < seg[(size_t)b].taille) std::swap(a, b);
    seg[(size_t)b].parent = a;
    seg[(size_t)a].taille += seg[(size_t)b].taille;
}

struct Boite { int32_t x0, y0, x1, y1; int64_t n; };

} // namespace

std::vector<Region> regions_uniformes(const Frame& f, int quant, int64_t aire_min,
                                      double remplissage_min) {
    std::vector<Region> out;
    if (f.vide()) return out;
    const int W = f.region.w, H = f.region.h;
    if (W <= 0 || H <= 0) return out;

    // ---- 1. segments par ligne, et réunion avec la ligne du dessus --------------
    std::vector<Seg> seg;
    seg.reserve((size_t)H * 16 + 32);
    std::vector<int32_t> prec, cour;

    for (int y = 0; y < H; ++y) {
        cour.clear();
        const uint32_t* p = f.px.data() + (size_t)y * W;
        int debut = 0;
        uint32_t cle = cle_couleur(p[0], quant);
        for (int x = 1; x <= W; ++x) {
            const uint32_t k = (x < W) ? cle_couleur(p[x], quant) : ~cle;
            if (k == cle && x < W) continue;
            const int32_t id = (int32_t)seg.size();
            seg.push_back(Seg{y, debut, x, cle, id, 1});
            cour.push_back(id);
            debut = x;
            cle = k;
        }

        // deux pointeurs : les segments sont ordonnés en x sur les deux lignes
        size_t a = 0, b = 0;
        while (y > 0 && a < prec.size() && b < cour.size()) {
            Seg& sa = seg[(size_t)prec[a]];
            Seg& sb = seg[(size_t)cour[b]];
            if (sa.x1 > sb.x0 && sb.x1 > sa.x0 && sa.cle == sb.cle) unir(seg, prec[a], cour[b]);
            if (sa.x1 <= sb.x1) ++a;
            if (sb.x1 <= sa.x1) ++b;
        }
        prec.swap(cour);
    }

    // ---- 2. une boîte par composante -------------------------------------------
    std::vector<int32_t> index(seg.size(), -1);
    std::vector<Boite> boites;
    boites.reserve(seg.size() / 8 + 8);

    for (size_t i = 0; i < seg.size(); ++i) {
        const int32_t r = racine(seg, (int32_t)i);
        int32_t& k = index[(size_t)r];
        const Seg& s = seg[i];
        if (k < 0) {
            k = (int32_t)boites.size();
            boites.push_back(Boite{s.x0, s.y, s.x1, s.y + 1, s.x1 - s.x0});
            continue;
        }
        Boite& b = boites[(size_t)k];
        b.x0 = std::min(b.x0, s.x0);
        b.x1 = std::max(b.x1, s.x1);
        b.y0 = std::min(b.y0, s.y);
        b.y1 = std::max(b.y1, s.y + 1);
        b.n += s.x1 - s.x0;
    }

    // ---- 3. filtre, puis couleur moyenne des seules composantes gardées -------
    std::vector<int32_t> garde(boites.size(), -1);
    struct Cumul { int64_t r = 0, g = 0, b = 0; };
    std::vector<Cumul> cumuls;

    for (size_t k = 0; k < boites.size(); ++k) {
        const Boite& b = boites[k];
        const Rect rect{f.region.x + b.x0, f.region.y + b.y0, b.x1 - b.x0, b.y1 - b.y0};
        if (rect.area() < aire_min) continue;
        const double remplissage = (double)b.n / (double)rect.area();
        if (remplissage < remplissage_min) continue;
        garde[k] = (int32_t)cumuls.size();
        cumuls.push_back(Cumul{});
        out.push_back(Region{rect, 0, b.n, remplissage});
    }

    if (!out.empty()) {
        for (size_t i = 0; i < seg.size(); ++i) {
            const int32_t r = racine(seg, (int32_t)i);
            const int32_t k = garde[(size_t)index[(size_t)r]];
            if (k < 0) continue;
            const Seg& s = seg[i];
            Cumul& c = cumuls[(size_t)k];
            const uint32_t* p = f.px.data() + (size_t)s.y * W;
            for (int x = s.x0; x < s.x1; ++x) {
                c.r += (p[x] >> 16) & 0xFF;
                c.g += (p[x] >> 8) & 0xFF;
                c.b += p[x] & 0xFF;
            }
        }
        for (size_t k = 0; k < out.size(); ++k) {
            const auto& c = cumuls[k];
            const int64_t n = out[k].pixels > 0 ? out[k].pixels : 1;
            const uint32_t r = (uint32_t)(c.r / n), g = (uint32_t)(c.g / n), b = (uint32_t)(c.b / n);
            out[k].couleur = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    std::sort(out.begin(), out.end(), [](const Region& p, const Region& q) {
        return p.rect.area() != q.rect.area() ? p.rect.area() > q.rect.area() : p.pixels > q.pixels;
    });
    return out;
}

} // namespace visio
