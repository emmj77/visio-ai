// tests/unit_regions.cpp — la segmentation par couleur, sur des images connues.
#include "core/frame.h"
#include "perceive/regions.h"

#include <cstdio>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

static Region* trouver(std::vector<Region>& rs, uint32_t couleur, int tol = 8) {
    for (auto& r : rs) {
        const int dr = (int)abs((int)((r.couleur >> 16) & 0xFF) - (int)((couleur >> 16) & 0xFF));
        const int dg = (int)abs((int)((r.couleur >> 8) & 0xFF) - (int)((couleur >> 8) & 0xFF));
        const int db = (int)abs((int)(r.couleur & 0xFF) - (int)(couleur & 0xFF));
        if (dr <= tol && dg <= tol && db <= tol) return &r;
    }
    return nullptr;
}

int main() {
    // fond blanc, trois rectangles de couleurs franches
    Frame f = Frame::pour(Rect{0, 0, 200, 100});
    f.remplir(0xFFFFFFFFu);
    for (int y = 10; y < 40; ++y) for (int x = 20; x < 80; ++x) f.set(x, y, 0xFF000000u);   // 60x30 noir
    for (int y = 50; y < 70; ++y) for (int x = 120; x < 170; ++x) f.set(x, y, 0xFFFF0000u); // 50x20 rouge
    for (int y = 60; y < 90; ++y) for (int x = 20; x < 50; ++x) f.set(x, y, 0xFF0000FFu);   // 30x30 bleu

    auto rs = regions_uniformes(f, 5, 64, 0.55);

    CHECK(rs.size() >= 4);
    // le fond est la plus grande région
    CHECK(!rs.empty() && rs[0].rect.area() == 200 * 100);

    Region* noir = trouver(rs, 0xFF000000u);
    CHECK(noir != nullptr);
    if (noir) {
        CHECK((noir->rect == Rect{20, 10, 60, 30}));
        CHECK(noir->pixels == 60 * 30);
        CHECK(noir->remplissage > 0.99);
    }
    Region* rouge = trouver(rs, 0xFFFF0000u);
    CHECK(rouge != nullptr);
    if (rouge) CHECK((rouge->rect == Rect{120, 50, 50, 20}));
    Region* bleu = trouver(rs, 0xFF0000FFu);
    CHECK(bleu != nullptr);
    if (bleu) CHECK((bleu->rect == Rect{20, 60, 30, 30}));

    // les coordonnées sont celles de l'écran, pas celles de la frame
    {
        Frame g = Frame::pour(Rect{500, 300, 100, 100});
        g.remplir(0xFFFFFFFFu);
        for (int y = 10; y < 30; ++y) for (int x = 10; x < 40; ++x) g.set(500 + x, 300 + y, 0xFF00FF00u);
        auto r2 = regions_uniformes(g, 5, 16, 0.5);
        Region* vert = trouver(r2, 0xFF00FF00u);
        CHECK(vert != nullptr);
        if (vert) CHECK((vert->rect == Rect{510, 310, 30, 20}));
    }

    // aire_min jette les petites régions
    {
        Frame h = Frame::pour(Rect{0, 0, 100, 100});
        h.remplir(0xFFFFFFFFu);
        h.set(5, 5, 0xFF123456u);
        auto r3 = regions_uniformes(h, 5, 64, 0.0);
        CHECK(trouver(r3, 0xFF123456u) == nullptr);
    }

    // deux rectangles de même couleur qui se touchent : une seule région
    {
        Frame k = Frame::pour(Rect{0, 0, 100, 100});
        k.remplir(0xFFFFFFFFu);
        for (int y = 10; y < 30; ++y) for (int x = 10; x < 30; ++x) k.set(x, y, 0xFF000000u);
        for (int y = 10; y < 30; ++y) for (int x = 30; x < 50; ++x) k.set(x, y, 0xFF000000u);
        auto r4 = regions_uniformes(k, 5, 16, 0.5);
        Region* n = trouver(r4, 0xFF000000u);
        CHECK(n != nullptr);
        if (n) CHECK((n->rect == Rect{10, 10, 40, 20}));
    }

    std::printf("unit_regions : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
