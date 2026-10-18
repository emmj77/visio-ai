#include "perceive/regions.h"

#include "perceive/text_lines.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace visio {

namespace {

// Un segment horizontal uniforme. C'est la bonne unité de travail : une barre
// d'outils fait une vingtaine de segments par ligne, pas mille pixels. La version « un
// nœud par pixel » coûtait 38 ms pour 1 Mpx, mesuré sur une fenêtre Firefox.
struct Seg {
    int      y = 0, x0 = 0, x1 = 0;   // [x0, x1)
    uint32_t cle = 0;                 // couleur quantifiée, ou encre 0/1
    int32_t  parent = -1;
    int32_t  taille = 1;
};

// Deux façons de nommer un pixel, et c'est tout ce qui diffère entre les deux passes :
//  - par COULEUR (quantifiée) : « qu'est-ce qui est plein, de la même couleur ? »
//  - par ENCRE (écart au fond) : « qu'est-ce qui est tracé ? ». Une bordure de 1 px est
//    antialiasée sur ses coins : ses pixels d'arc ont chacun une couleur différente, et une
//    lecture par couleur coupe le cadre en morceaux. Par encre, l'arc reste de l'encre et
//    le trait tient d'un bout à l'autre. Mesuré sur la page témoin : coins arrondis, une
//    boîte trouvée par encre, zéro par couleur.
struct ModeCle {
    int      quant = 5;
    bool     binaire = false;
    uint32_t fond = 0;
    int      seuil = 8;
};

inline uint32_t cle_couleur(uint32_t argb, const ModeCle& m) {
    if (m.binaire) {
        const int dr = std::abs((int)((argb >> 16) & 0xFF) - (int)((m.fond >> 16) & 0xFF));
        const int dg = std::abs((int)((argb >> 8) & 0xFF) - (int)((m.fond >> 8) & 0xFF));
        const int db = std::abs((int)(argb & 0xFF) - (int)(m.fond & 0xFF));
        return (dr > m.seuil || dg > m.seuil || db > m.seuil) ? 1u : 0u;
    }
    const uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return ((r >> m.quant) << 16) | ((g >> m.quant) << 8) | (b >> m.quant);
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

struct Boite { int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; int64_t n = 0; };

// La découpe commune aux deux lectures de l'écran : segments, réunion des lignes voisines,
// une boîte par composante. `regions_uniformes` garde ce qui est PLEIN ; `boites_fermees`
// garde ce qui SE REFERME. Une seule mécanique, deux filtres.
struct Decoupe {
    std::vector<Seg>     seg;
    std::vector<Boite>   boites;   // une par composante
    std::vector<int32_t> index;    // racine -> indice de boîte (-1 si la racine n'en a pas)
};

Decoupe decouper(const Frame& f, const ModeCle& mode, bool huit = false) {
    Decoupe d;
    const int W = f.region.w, H = f.region.h;
    if (W <= 0 || H <= 0) return d;

    // ---- 1. segments par ligne, et réunion avec la ligne du dessus --------------
    d.seg.reserve((size_t)H * 16 + 32);
    std::vector<int32_t> prec, cour;

    for (int y = 0; y < H; ++y) {
        cour.clear();
        const uint32_t* p = f.px.data() + (size_t)y * W;
        int debut = 0;
        uint32_t cle = cle_couleur(p[0], mode);
        for (int x = 1; x <= W; ++x) {
            const uint32_t k = (x < W) ? cle_couleur(p[x], mode) : ~cle;
            if (k == cle && x < W) continue;
            const int32_t id = (int32_t)d.seg.size();
            d.seg.push_back(Seg{y, debut, x, cle, id, 1});
            cour.push_back(id);
            debut = x;
            cle = k;
        }

        // Réunion avec la ligne du dessus. Deux pointeurs, mais sur une FENÊTRE : `a` ne
        // recule jamais (les segments d'une ligne sont triés), et pour chaque segment du
        // dessous on ne regarde que ceux qui peuvent encore le toucher. La version
        // précédente jetait un segment du dessus dès que ses x étaient plus à gauche, ce qui
        // perdait des paires dès qu'on tolérait le contact diagonal (un arc de coin).
        const int marge = huit ? 1 : 0;
        size_t a = 0;
        for (size_t b = 0; b < cour.size(); ++b) {
            const Seg& sb = d.seg[(size_t)cour[b]];
            while (a < prec.size() && d.seg[(size_t)prec[a]].x1 < sb.x0 + (1 - marge)) ++a;
            for (size_t k = a; k < prec.size(); ++k) {
                const Seg& sa = d.seg[(size_t)prec[k]];
                if (sa.x0 > sb.x1 + marge - 1) break;          // au-delà : plus rien à voir
                if (sa.cle != sb.cle) continue;
                const bool voisins = huit ? (sa.x1 >= sb.x0 - 1 && sb.x1 >= sa.x0 - 1)
                                          : (sa.x1 > sb.x0 && sb.x1 > sa.x0);
                if (voisins) unir(d.seg, prec[k], cour[b]);
            }
        }
        prec.swap(cour);
    }

    // ---- 2. une boîte par composante -------------------------------------------
    d.index.assign(d.seg.size(), -1);
    d.boites.reserve(d.seg.size() / 8 + 8);

    for (size_t i = 0; i < d.seg.size(); ++i) {
        const int32_t r = racine(d.seg, (int32_t)i);
        int32_t& k = d.index[(size_t)r];
        const Seg& s = d.seg[i];
        if (k < 0) {
            k = (int32_t)d.boites.size();
            d.boites.push_back(Boite{s.x0, s.y, s.x1, s.y + 1, s.x1 - s.x0});
            continue;
        }
        Boite& bb = d.boites[(size_t)k];
        bb.x0 = std::min(bb.x0, s.x0);
        bb.x1 = std::max(bb.x1, s.x1);
        bb.y0 = std::min(bb.y0, s.y);
        bb.y1 = std::max(bb.y1, s.y + 1);
        bb.n += s.x1 - s.x0;
    }
    return d;
}

} // namespace

std::vector<Region> regions_uniformes(const Frame& f, int quant, int64_t aire_min,
                                      double remplissage_min) {
    std::vector<Region> out;
    if (f.vide()) return out;
    Decoupe d = decouper(f, ModeCle{quant, false, 0, 0});
    if (d.boites.empty()) return out;

    const int W = f.region.w;

    // ---- filtre, puis couleur moyenne des seules composantes gardées -----------
    std::vector<int32_t> garde(d.boites.size(), -1);
    struct Cumul { int64_t r = 0, g = 0, b = 0; };
    std::vector<Cumul> cumuls;

    for (size_t k = 0; k < d.boites.size(); ++k) {
        const Boite& b = d.boites[k];
        const Rect rect{f.region.x + b.x0, f.region.y + b.y0, b.x1 - b.x0, b.y1 - b.y0};
        if (rect.area() < aire_min) continue;
        const double remplissage = (double)b.n / (double)rect.area();
        if (remplissage < remplissage_min) continue;
        garde[k] = (int32_t)cumuls.size();
        cumuls.push_back(Cumul{});
        out.push_back(Region{rect, 0, b.n, remplissage});
    }

    if (!out.empty()) {
        for (size_t i = 0; i < d.seg.size(); ++i) {
            const int32_t r = racine(d.seg, (int32_t)i);
            const int32_t k = garde[(size_t)d.index[(size_t)r]];
            if (k < 0) continue;
            const Seg& s = d.seg[i];
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

std::vector<Region> boites_fermees(const Frame& f, int seuil_encre, int cote_min,
                                   int64_t aire_min, double remplissage_max, uint32_t fond) {
    std::vector<Region> out;
    if (f.vide()) return out;
    if (fond == 0) fond = couleur_fond(f, 3);
    Decoupe d = decouper(f, ModeCle{0, true, fond, seuil_encre}, /*huit=*/true);
    if (d.boites.empty()) return out;

    const int W = f.region.w;

    // ---- 1. candidats : assez grands, et TROUÉS --------------------------------
    // Une bordure de 1 px sur une boîte 500x400 occupe 0,09 de son aire : c'est
    // exactement ce que `regions_uniformes` jette (remplissage 0,55). Un bouton plein a
    // un remplissage voisin de 1 et il est déjà une région : on ne le redemande pas.
    std::vector<int32_t> slot(d.boites.size(), -1);
    std::vector<int32_t> garde;                  // indice de boîte, par candidat
    std::vector<std::vector<uint8_t>> masque;    // 1 = pixel de la composante
    int64_t octets = 0;

    for (size_t k = 0; k < d.boites.size(); ++k) {
        const Boite& b = d.boites[k];
        const int w = b.x1 - b.x0, h = b.y1 - b.y0;
        if (w < cote_min || h < cote_min) continue;
        const int64_t aire = (int64_t)w * h;
        if (aire < aire_min) continue;
        if ((double)b.n > remplissage_max * (double)aire) continue;
        // Le FOND entoure les boîtes : sa boîte couvre la frame. C'est l'EXTÉRIEUR, pas une
        // forme — il ne se referme pas sur lui-même, il borde.
        if (aire * 100 >= (int64_t)f.region.w * f.region.h * 98) continue;
        if (octets + aire > 64 * 1024 * 1024) continue;      // garde-fou mémoire
        octets += aire;
        slot[k] = (int32_t)garde.size();
        garde.push_back((int32_t)k);
        masque.emplace_back((size_t)aire, (uint8_t)0);
    }
    if (std::getenv("VISIO_DEBUG_BOITES"))
        std::fprintf(stderr, "DBG boites %zu comp, %zu candidats\n", d.boites.size(), garde.size());
    if (garde.empty()) return out;

    // ---- 2. le masque de chaque candidat : une passe sur les segments ----------
    for (size_t i = 0; i < d.seg.size(); ++i) {
        const int32_t root = racine(d.seg, (int32_t)i);
        const int32_t k = d.index[(size_t)root];
        if (k < 0) continue;
        const int32_t s = slot[(size_t)k];
        if (s < 0) continue;
        const Boite& b = d.boites[(size_t)k];
        const Seg& sg = d.seg[i];
        auto& m = masque[(size_t)s];
        const int w = b.x1 - b.x0;
        const size_t base = (size_t)(sg.y - b.y0) * (size_t)w;
        std::fill(m.begin() + (std::ptrdiff_t)base + (sg.x0 - b.x0),
                  m.begin() + (std::ptrdiff_t)base + (sg.x1 - b.x0), (uint8_t)1);
    }

    // ---- 3. se referme-t-elle ? ------------------------------------------------
    // Le test ne regarde pas la FORME, il regarde la TOPOLOGIE : l'intérieur de la boîte
    // est-il séparé de l'extérieur ? On noie depuis le bord de la boîte ; ce que l'eau ne
    // touche pas est un intérieur. Un rond, un triangle, un cadre aux coins arrondis
    // passent ; une croix, un trait, une lettre laissent tout communiquer et échouent.
    // C'est ce qui rend le détecteur indépendant du genre de forme.
    std::vector<char> fermee(garde.size(), 0);
    std::vector<int32_t> pile;
    for (size_t c = 0; c < garde.size(); ++c) {
        const Boite& b = d.boites[(size_t)garde[c]];
        const int w = b.x1 - b.x0, h = b.y1 - b.y0;
        auto& m = masque[c];
        // 0 = libre, 1 = la forme, 2 = atteint par l'eau
        auto indice = [w](int x, int y) { return (size_t)y * (size_t)w + (size_t)x; };
        auto noyer = [&](int x, int y) {
            if (m[indice(x, y)] != 0) return;
            m[indice(x, y)] = 2;
            pile.push_back(y * w + x);
        };
        pile.clear();
        for (int x = 0; x < w; ++x) { noyer(x, 0); noyer(x, h - 1); }
        for (int y = 0; y < h; ++y) { noyer(0, y); noyer(w - 1, y); }
        while (!pile.empty()) {
            const int p = pile.back();
            pile.pop_back();
            const int x = p % w, y = p / w;
            if (x > 0) noyer(x - 1, y);
            if (x + 1 < w) noyer(x + 1, y);
            if (y > 0) noyer(x, y - 1);
            if (y + 1 < h) noyer(x, y + 1);
        }
        int64_t trou = 0;
        for (uint8_t v : m) if (v == 0) ++trou;
        if (trou < 25) continue;      // un trou d'antialiasing n'est pas un intérieur

        // Un rectangle pose son encre SUR son bord ; un rond ou un triangle ne le touche
        // qu'en quelques points. Le plan s'en sert pour choisir entre dessiner un cadre et
        // laisser la forme se voir.
        int64_t sur_bord = 0;
        for (int y = 0; y < h; ++y) {
            const bool lignebord = (y <= 1 || y >= h - 2);
            for (int x = 0; x < w; ++x) {
                if (m[(size_t)y * (size_t)w + (size_t)x] != 1) continue;
                if (lignebord || x <= 1 || x >= w - 2) ++sur_bord;
            }
        }

        fermee[c] = 1;
        const Rect rect{f.region.x + b.x0, f.region.y + b.y0, w, h};
        Region rg{rect, 0, b.n, (double)b.n / (double)rect.area()};
        rg.cadre = true;
        rg.sur_bord = b.n > 0 ? (double)sur_bord / (double)b.n : 0.0;
        out.push_back(rg);
    }
    if (out.empty()) return out;

    // ---- 4. la couleur d'une boîte, c'est celle de son trait -------------------
    std::vector<int32_t> vers_sortie(d.boites.size(), -1);
    {
        size_t n = 0;
        for (size_t c = 0; c < garde.size(); ++c)
            if (fermee[c]) vers_sortie[(size_t)garde[c]] = (int32_t)n++;
    }
    struct Cumul { int64_t r = 0, g = 0, b = 0; };
    std::vector<Cumul> cumuls(out.size());
    for (size_t i = 0; i < d.seg.size(); ++i) {
        const int32_t root = racine(d.seg, (int32_t)i);
        const int32_t k = d.index[(size_t)root];
        if (k < 0) continue;
        const int32_t o = vers_sortie[(size_t)k];
        if (o < 0) continue;
        const Seg& s = d.seg[i];
        Cumul& cu = cumuls[(size_t)o];
        const uint32_t* p = f.px.data() + (size_t)s.y * W;
        for (int x = s.x0; x < s.x1; ++x) {
            cu.r += (p[x] >> 16) & 0xFF;
            cu.g += (p[x] >> 8) & 0xFF;
            cu.b += p[x] & 0xFF;
        }
    }
    for (size_t k = 0; k < out.size(); ++k) {
        const auto& c = cumuls[k];
        const int64_t n = out[k].pixels > 0 ? out[k].pixels : 1;
        out[k].couleur = 0xFF000000u | ((uint32_t)(c.r / n) << 16) |
                         ((uint32_t)(c.g / n) << 8) | (uint32_t)(c.b / n);
    }

    std::sort(out.begin(), out.end(), [](const Region& p, const Region& q) {
        return p.rect.area() != q.rect.area() ? p.rect.area() > q.rect.area() : p.pixels > q.pixels;
    });
    return out;
}

} // namespace visio
