// src/perceive/glyph.cpp — découpage d'une bande de texte en lettre.
#include "perceive/glyph.h"

#include <algorithm>
#include <cstring>

namespace visio {

namespace {

// Force d'encre d'un pixel : son écart au fond, 0 à 255.
inline int force_encre(uint32_t c, int fr, int fg, int fb) {
    const int dr = std::abs((int)((c >> 16) & 0xFF) - fr);
    const int dg = std::abs((int)((c >> 8) & 0xFF) - fg);
    const int db = std::abs((int)(c & 0xFF) - fb);
    return std::min(255, std::max(dr, std::max(dg, db)));
}

inline bool est_encre(uint32_t c, int fr, int fg, int fb, int seuil) {
    return force_encre(c, fr, fg, fb) > seuil;
}

// Remplit le bitmap d'intensités de [x0,x1) x [y0,y1), normalisé sur le contraste
// propre du glyphe : un texte gris sur gris se compare comme un texte noir sur blanc.
void remplir_bitmap(const Frame& f, int x0, int y0, int x1, int y1, int fr, int fg, int fb,
                    Glyphe& g) {
    g.w = (uint8_t)std::min(GP_W, x1 - x0);
    g.h = (uint8_t)std::min(GP_H, y1 - y0);
    std::memset(g.px, 0, GP_PIX);

    int fmax = 1;
    for (int y = 0; y < g.h; ++y) {
        const uint32_t* p = f.px.data() + (size_t)(y0 + y - f.region.y) * f.region.w +
                            (x0 - f.region.x);
        for (int x = 0; x < g.w; ++x) fmax = std::max(fmax, force_encre(p[x], fr, fg, fb));
    }
    for (int y = 0; y < g.h; ++y) {
        const uint32_t* p = f.px.data() + (size_t)(y0 + y - f.region.y) * f.region.w +
                            (x0 - f.region.x);
        for (int x = 0; x < g.w; ++x) {
            const int v = force_encre(p[x], fr, fg, fb);
            g.px[y * GP_W + x] = (uint8_t)std::min(255, (v * 255) / fmax);
        }
    }
}

} // namespace

float ecart_glyphes(const Glyphe& a, const Glyphe& b) {
    if (a.w != b.w || a.h != b.h) return 1.0f;
    int somme = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            somme += std::abs((int)a.px[y * GP_W + x] - (int)b.px[y * GP_W + x]);
    return (float)somme / (float)(a.w * a.h * 255);
}

std::vector<Glyphe> extraire_glyphes(const Frame& f, const LigneTexte& ligne, uint32_t fond,
                                     int seuil) {
    std::vector<Glyphe> out;
    const Rect z = intersect(ligne.rect, f.region);
    if (z.empty()) return out;

    const int fr = (int)((fond >> 16) & 0xFF), fg = (int)((fond >> 8) & 0xFF), fb = (int)(fond & 0xFF);

    // Seuil de soudure, bas : les franges antialiasées tiennent une lettre entière ; ce
    // sont les composantes connexes qui séparent ensuite les lettres voisines.
    int force_max = 0;
    for (int y = z.y; y < z.bottom(); ++y) {
        const uint32_t* p = f.px.data() + (size_t)(y - f.region.y) * f.region.w + (z.x - f.region.x);
        for (int x = 0; x < z.w; ++x) force_max = std::max(force_max, force_encre(p[x], fr, fg, fb));
    }
    const int seuil_seg = std::max(seuil, (int)(0.15f * (float)force_max));

    // 1. masque d'encre
    const int bw = z.w, bh = z.h;
    std::vector<uint8_t> masque((size_t)bw * bh, 0);
    for (int y = 0; y < bh; ++y) {
        const uint32_t* p = f.px.data() + (size_t)(y + z.y - f.region.y) * f.region.w +
                            (z.x - f.region.x);
        for (int x = 0; x < bw; ++x)
            masque[(size_t)y * bw + x] = est_encre(p[x], fr, fg, fb, seuil_seg) ? 1u : 0u;
    }

    // 2. composantes connexes (4-voisins)
    std::vector<int32_t> pere((size_t)bw * bh, -1), rang((size_t)bw * bh, 1);
    auto trouver = [&pere](int32_t i) {
        while (pere[(size_t)i] != i) {
            pere[(size_t)i] = pere[(size_t)pere[(size_t)i]];
            i = pere[(size_t)i];
        }
        return i;
    };
    auto unir = [&pere, &rang, &trouver](int32_t a, int32_t b) {
        a = trouver(a); b = trouver(b);
        if (a == b) return;
        if (rang[(size_t)a] < rang[(size_t)b]) std::swap(a, b);
        pere[(size_t)b] = a;
        rang[(size_t)a] += rang[(size_t)b];
    };

    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t i = (size_t)y * bw + x;
            if (!masque[i]) continue;
            if (pere[i] < 0) pere[i] = (int32_t)i;
            if (x > 0 && masque[i - 1]) unir((int32_t)i, (int32_t)(i - 1));
            if (y > 0 && masque[i - bw]) unir((int32_t)i, (int32_t)(i - bw));
        }

    // 3. boîte de chaque composante
    struct Morceau { int x0, y0, x1, y1; };
    std::vector<int32_t> vers((size_t)bw * bh, -1);
    std::vector<Morceau> morceaux;
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t i = (size_t)y * bw + x;
            if (!masque[i]) continue;
            const int32_t r = trouver((int32_t)i);
            int32_t& k = vers[(size_t)r];
            if (k < 0) {
                k = (int32_t)morceaux.size();
                morceaux.push_back(Morceau{x, y, x + 1, y + 1});
            } else {
                Morceau& m = morceaux[(size_t)k];
                m.x0 = std::min(m.x0, x);
                m.x1 = std::max(m.x1, x + 1);
                m.y0 = std::min(m.y0, y);
                m.y1 = std::max(m.y1, y + 1);
            }
        }

    // 4. regroupement par recouvrement horizontal : le point du « i » rejoint sa hampe
    std::sort(morceaux.begin(), morceaux.end(),
              [](const Morceau& a, const Morceau& b) { return a.x0 < b.x0; });
    std::vector<Morceau> lettres;
    for (const auto& m : morceaux) {
        if (!lettres.empty() && m.x0 < lettres.back().x1) {
            Morceau& l = lettres.back();
            l.x0 = std::min(l.x0, m.x0);
            l.x1 = std::max(l.x1, m.x1);
            l.y0 = std::min(l.y0, m.y0);
            l.y1 = std::max(l.y1, m.y1);
        } else {
            lettres.push_back(m);
        }
    }

    // 5. le bitmap de chaque lettre
    for (const auto& l : lettres) {
        if (l.x1 - l.x0 < 1 || l.y1 - l.y0 < 2) continue;
        if (l.x1 - l.x0 > GP_W || l.y1 - l.y0 > GP_H) continue;   // trop gros : hors d'un texte
        Glyphe g;
        g.rect = Rect{z.x + l.x0, z.y + l.y0, l.x1 - l.x0, l.y1 - l.y0};
        remplir_bitmap(f, g.rect.x, g.rect.y, g.rect.right(), g.rect.bottom(), fr, fg, fb, g);
        out.push_back(g);
    }
    return out;
}

} // namespace visio
