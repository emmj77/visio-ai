#include "perceive/text_lines.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace visio {

namespace {

inline uint32_t cle(uint32_t c, int quant) {
    return (((c >> 16) & 0xFF) >> quant) << 16 | (((c >> 8) & 0xFF) >> quant) << 8 |
           ((c & 0xFF) >> quant);
}

// On accumule les TROIS CANAUX séparément. Sommer des couleurs empaquetées puis
// diviser est faux : les retenues du canal rouge débordent sur le vert, celles du vert
// sur le bleu. Mesuré : un fond blanc rendait #fffe17.
struct Tab { uint32_t cle; int64_t n; int64_t r, g, b; };

} // namespace

uint32_t couleur_fond(const Frame& f, int quant) {
    if (f.vide()) return 0xFF000000u;
    // échantillonnage : un pixel sur quatre suffit pour trouver le fond
    std::vector<Tab> tables;
    tables.reserve(64);
    for (size_t i = 0; i < f.px.size(); i += 4) {
        const uint32_t c = f.px[i];
        const uint32_t k = cle(c, quant);
        auto it = std::find_if(tables.begin(), tables.end(), [k](const Tab& t) { return t.cle == k; });
        const int64_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        if (it == tables.end()) tables.push_back(Tab{k, 1, r, g, b});
        else { ++it->n; it->r += r; it->g += g; it->b += b; }
    }
    if (tables.empty()) return 0xFF000000u;
    auto best = std::max_element(tables.begin(), tables.end(),
                                 [](const Tab& a, const Tab& b) { return a.n < b.n; });
    const uint32_t r = (uint32_t)(best->r / best->n);
    const uint32_t g = (uint32_t)(best->g / best->n);
    const uint32_t b = (uint32_t)(best->b / best->n);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

std::vector<LigneTexte> lignes_de_texte(const Frame& f, uint32_t fond, int seuil,
                                        int hauteur_max, double densite_max) {
    std::vector<LigneTexte> out;
    if (f.vide()) return out;
    const int W = f.region.w, H = f.region.h;

    const int fr = (int)((fond >> 16) & 0xFF), fg = (int)((fond >> 8) & 0xFF), fb = (int)(fond & 0xFF);
    std::vector<int> encre_par_ligne((size_t)H, 0);

    // 1. projection horizontale : combien de pixels d'encre par ligne de pixels
    for (int y = 0; y < H; ++y) {
        const uint32_t* p = f.px.data() + (size_t)y * W;
        int n = 0;
        for (int x = 0; x < W; ++x) {
            const int dr = std::abs((int)((p[x] >> 16) & 0xFF) - fr);
            const int dg = std::abs((int)((p[x] >> 8) & 0xFF) - fg);
            const int db = std::abs((int)(p[x] & 0xFF) - fb);
            if (dr > seuil || dg > seuil || db > seuil) ++n;
        }
        encre_par_ligne[(size_t)y] = n;
    }

    // 2. bandes : les lignes de pixels qui portent de l'encre, séparées par du vide
    int y = 0;
    while (y < H) {
        while (y < H && encre_par_ligne[(size_t)y] == 0) ++y;
        if (y >= H) break;
        const int debut = y;
        while (y < H && encre_par_ligne[(size_t)y] > 0) ++y;
        const int fin = y;

        const int h = fin - debut;
        if (h < 4 || h > hauteur_max) continue;

        // 3. enveloppe horizontale et densité de la bande
        int x0 = W, x1 = 0;
        int64_t encre = 0;
        for (int yy = debut; yy < fin; ++yy) {
            const uint32_t* p = f.px.data() + (size_t)yy * W;
            for (int x = 0; x < W; ++x) {
                const int dr = std::abs((int)((p[x] >> 16) & 0xFF) - fr);
                const int dg = std::abs((int)((p[x] >> 8) & 0xFF) - fg);
                const int db = std::abs((int)(p[x] & 0xFF) - fb);
                if (dr > seuil || dg > seuil || db > seuil) {
                    if (x < x0) x0 = x;
                    if (x + 1 > x1) x1 = x + 1;
                    ++encre;
                }
            }
        }
        if (x1 <= x0 || encre < 8) continue;

        const Rect rect{f.region.x + x0, f.region.y + debut, x1 - x0, h};
        const double densite = (double)encre / (double)rect.area();
        if (densite > densite_max) continue;   // aplat, pas du texte
        out.push_back(LigneTexte{rect, encre, densite});
    }

    return out;
}

} // namespace visio
