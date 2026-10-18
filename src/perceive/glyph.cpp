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

std::vector<Glyphe> extraire_glyphes_pas(const Frame& f, const LigneTexte& ligne, uint32_t fond,
                                         int pas, std::vector<int>* cellules) {
    std::vector<Glyphe> out;
    if (cellules) cellules->clear();
    if (pas < 3 || pas > 48) return out;

    const int fr = (fond >> 16) & 255, fg = (fond >> 8) & 255, fb = fond & 255;
    auto encre = [&](int x, int y) {
        return est_encre(f.px[(size_t)(y - f.region.y) * f.region.w + (x - f.region.x)],
                         fr, fg, fb, 24);
    };

    // Première colonne d'encre : c'est l'origine de la grille.
    int x_min = -1;
    for (int x = ligne.rect.x; x < ligne.rect.right() && x_min < 0; ++x)
        for (int y = ligne.rect.y; y < ligne.rect.bottom(); ++y)
            if (encre(x, y)) { x_min = x; break; }
    if (x_min < 0) return out;

    // La grille ne part pas forcément de la première encre : chaque lettre a son propre
    // talon (bearing) à gauche. On cale la phase sur la position qui coupe le MOINS
    // d'encre — une frontière qui traverse une lettre est une erreur de découpe.
    int phase = x_min, cout_min = 1 << 30;
    for (int p = x_min - pas + 1; p <= x_min; ++p) {
        int cout = 0;
        for (int x = p; x < ligne.rect.right(); x += pas)
            if (x >= ligne.rect.x)
                for (int y = ligne.rect.y; y < ligne.rect.bottom(); ++y)
                    if (encre(x, y)) ++cout;
        if (cout < cout_min) { cout_min = cout; phase = p; }
    }
    x_min = phase;

    for (int k = 0;; ++k) {
        const int cx0 = x_min + k * pas;
        if (cx0 >= ligne.rect.right()) break;
        const int cx1 = std::min(cx0 + pas, ligne.rect.right());

        int ix0 = cx1, iy0 = ligne.rect.bottom(), ix1 = -1, iy1 = -1;
        for (int y = ligne.rect.y; y < ligne.rect.bottom(); ++y)
            for (int x = cx0; x < cx1; ++x)
                if (encre(x, y)) {
                    ix0 = std::min(ix0, x); ix1 = std::max(ix1, x);
                    iy0 = std::min(iy0, y); iy1 = std::max(iy1, y);
                }
        if (ix1 < 0) continue;   // cellule vide : une espace

        Glyphe g;
        g.rect = Rect{ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1};
        remplir_bitmap(f, ix0, iy0, ix1 + 1, iy1 + 1, fr, fg, fb, g);
        out.push_back(g);
        if (cellules) cellules->push_back(k);
    }
    return out;
}

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
    // Le seuil décide si deux lettres se touchent, et il ne peut pas être absolu : à 11 px
    // l'encre est faible, à 17 px elle est pleine. Mesuré sur une bande réelle de 17 px :
    // à 15 % du contraste, les halos d'antialiasing comptent comme de l'encre et soudent
    // des paires de lettres (groupes de 15-17 px) ; à 40 %, chaque lettre retrouve sa
    // propre colonne (groupes de 7 px, 30 % de colonnes de fond contre 25 %).
    // Deux besions différents, un seul seuil — c'est le défaut identifié :
    //  - POUR DÉCOUPER, il faut un seuil haut : à 15 % du contraste, les halos
    //    d'antialiasing comptent comme de l'encre et soudent des paires de lettres
    //    (mesuré : groupes de 15-17 px au lieu de 7 px sur une bande de 17 px).
    //  - POUR EXTRAIRE, il faut un seuil bas : le rendu FreeType inclut ses propres
    //    bords antialiasés ; une boîte trop serrée ne tombe plus sur ses dimensions
    //    (mesuré : 17/19 -> 2/19 sur les témoins avec un seuil à 40 %).
    // Le seuil bas reste donc ici ; la découpe par le pas (extraire_glyphes_pas) traite
    // le cas monospace, et la découpe par vallées reste à faire pour le proportionnel.
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
