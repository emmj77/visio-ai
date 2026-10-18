// tests/unit_regions.cpp — la segmentation par couleur, sur des images connues.
#include "core/frame.h"
#include "perceive/regions.h"

#include <cstdio>
#include <algorithm>
#include <cmath>

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

    // ---- boites fermees ---------------------------------------------------------
    // Un cadre : un trait continu qui se referme. Une region pleine, une croix, un trait
    // isole et une lettre ne sont pas des cadres.
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 120});
        p.remplir(0xFFFFFFFFu);
        for (int x = 20; x < 100; ++x) { p.set(x, 10, 0xFF808080u); p.set(x, 49, 0xFF808080u); }
        for (int y = 10; y < 50; ++y) { p.set(20, y, 0xFF808080u); p.set(99, y, 0xFF808080u); }

        auto cadre = boites_fermees(p);          // seuils par defaut : 14 / 600 / 0,65
        CHECK(cadre.size() == 1);
        if (cadre.size() == 1) {
            CHECK((cadre[0].rect == Rect{20, 10, 80, 40}));
            CHECK(cadre[0].remplissage < 0.20);          // un trait, pas un aplat
            CHECK(cadre[0].pixels == 2 * 80 + 2 * 40 - 4);
        }
        // la meme chose vue par les regions uniformes : elle est jetee (trop trouee)
        auto pleines = regions_uniformes(p, 5, 64, 0.55);
        CHECK(trouver(pleines, 0xFF808080u) == nullptr);
    }

    // une region PLEINE n'est pas un cadre (elle est deja une region)
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 120});
        p.remplir(0xFFFFFFFFu);
        for (int y = 20; y < 80; ++y) for (int x = 30; x < 130; ++x) p.set(x, y, 0xFF808080u);
        CHECK(boites_fermees(p).empty());
    }

    // une CROIX touche les quatre bords de sa boite, mais ne se referme pas
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 120});
        p.remplir(0xFFFFFFFFu);
        for (int x = 20; x < 180; ++x) p.set(x, 60, 0xFF808080u);
        for (int y = 10; y < 110; ++y) p.set(100, y, 0xFF808080u);
        CHECK(boites_fermees(p).empty());
    }

    // coins ARRONDIS : le coeur de la boite est intact, les coins sortent du test
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 120});
        p.remplir(0xFFFFFFFFu);
        const int x0 = 20, y0 = 20, x1 = 139, y1 = 89, r = 6;
        // Un trait de 1 px : on marque les pixels dont la distance au bord du rectangle
        // arrondi tombe dans [-1, 0]. C'est un vrai contour continu, pas des points.
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double dx = std::max({(double)(x0 + r - x), (double)(x - (x1 - r)), 0.0});
                const double dy = std::max({(double)(y0 + r - y), (double)(y - (y1 - r)), 0.0});
                const double d = std::sqrt(dx * dx + dy * dy) - r;
                if (d > -1.0 && d <= 0.0) p.set(x, y, 0xFF808080u);
            }
        auto cadre = boites_fermees(p, 8, 14, 600, 0.65);
        CHECK(cadre.size() == 1);
        if (cadre.size() == 1) CHECK((cadre[0].rect == Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1}));
    }

    // le FOND n'est pas une boite : il entoure tout et ses murs passent
    {
        Frame p = Frame::pour(Rect{0, 0, 300, 200});
        p.remplir(0xFFFFFFFFu);
        for (int x = 10; x < 290; ++x) { p.set(x, 10, 0xFF404040u); p.set(x, 189, 0xFF404040u); }
        for (int y = 10; y < 190; ++y) { p.set(10, y, 0xFF404040u); p.set(289, y, 0xFF404040u); }
        auto cadres = boites_fermees(p);
        CHECK(cadres.size() == 1);
        if (cadres.size() == 1) CHECK((cadres[0].rect == Rect{10, 10, 280, 180}));
    }

    // une LETTRE fermee : un anneau COURBE, comme le « o ». Ce n'est pas un cadre.
    {
        Frame p = Frame::pour(Rect{0, 0, 100, 60});
        p.remplir(0xFFFFFFFFu);
        const double cx = 17.0, cy = 19.0, rx = 7.0, ry = 9.0;
        for (int y = 0; y < 60; ++y)
            for (int x = 0; x < 100; ++x) {
                const double dx = (x - cx) / rx, dy = (y - cy) / ry;
                const double d = dx * dx + dy * dy;
                if (d > 0.62 && d <= 1.0) p.set(x, y, 0xFF000000u);
            }
        // la lettre « o » : 15x18, donc AU-DESSUS des seuils par defaut (10,150), mais son
        // encre est un anneau courbe — sur_bord bas — et elle doit rester dehors
        CHECK(boites_fermees(p).empty());
        // sans le filtre des petites formes, c'est bien une forme FERMEE : le test
        // topologique la voit, c'est le filtre anti-texte qui la renvoie
        CHECK(boites_fermees(p, 8, 8, 100, 0.65, 0, 0.0).size() == 1);
    }

    // une CASE A COCHER 16x16 : meme taille qu'une lettre, mais son encre est sur son bord
    {
        Frame p = Frame::pour(Rect{0, 0, 120, 90});
        p.remplir(0xFFFFFFFFu);
        for (int x = 40; x < 56; ++x) { p.set(x, 40, 0xFF909090u); p.set(x, 55, 0xFF909090u); }
        for (int y = 40; y < 56; ++y) { p.set(40, y, 0xFF909090u); p.set(55, y, 0xFF909090u); }
        auto b = boites_fermees(p);                  // seuils par defaut : 10 px, 150 px
        CHECK(b.size() == 1);
        if (b.size() == 1) {
            CHECK((b[0].rect == Rect{40, 40, 16, 16}));
            CHECK(b[0].sur_bord > 0.95);             // le trait est tout sur le bord
        }
    }

    // un ROND : ce n'est pas un rectangle, le test est topologique et doit le voir
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 160});
        p.remplir(0xFFFFFFFFu);
        const int cx = 100, cy = 80, r = 40;
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 200; ++x) {
                const double d = std::sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (d > r - 1.0 && d <= r) p.set(x, y, 0xFF2050A0u);   // un anneau de 1 px
            }
        auto cadres = boites_fermees(p, 8, 14, 600, 0.65);
        CHECK(cadres.size() == 1);
        if (cadres.size() == 1) {
            CHECK(cadres[0].rect.w >= 2 * r - 2 && cadres[0].rect.w <= 2 * r + 1);
            CHECK(cadres[0].rect.h >= 2 * r - 2 && cadres[0].rect.h <= 2 * r + 1);
        }
        // un disque PLEIN est une region, pas un cadre
        Frame q = Frame::pour(Rect{0, 0, 200, 160});
        q.remplir(0xFFFFFFFFu);
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 200; ++x) {
                const double d = std::sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (d <= r) q.set(x, y, 0xFF2050A0u);
            }
        CHECK(boites_fermees(q, 8, 14, 600, 0.65).empty());
    }

    // un TRIANGLE : trois traits qui se rejoignent, aucun mur horizontal ni vertical
    {
        Frame p = Frame::pour(Rect{0, 0, 200, 160});
        p.remplir(0xFFFFFFFFu);
        const int ax = 100, ay = 20, bx = 20, by = 140, cx2 = 180, cy2 = 140;
        auto trait = [&p](int x0, int y0, int x1, int y1) {
            const int pas = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
            for (int i = 0; i <= pas; ++i) {
                const int x = x0 + (x1 - x0) * i / std::max(1, pas);
                const int y = y0 + (y1 - y0) * i / std::max(1, pas);
                p.set(x, y, 0xFF306030u);
            }
        };
        trait(ax, ay, bx, by); trait(bx, by, cx2, cy2); trait(cx2, cy2, ax, ay);
        auto cadres = boites_fermees(p, 8, 14, 600, 0.65);
        CHECK(cadres.size() == 1);
        if (cadres.size() == 1) CHECK((cadres[0].rect == Rect{bx, ay, cx2 - bx + 1, by - ay + 1}));
    }

    // boites IMBRIQUEES : la grande et la petite, de la plus grande a la plus petite
    {
        Frame p = Frame::pour(Rect{0, 0, 300, 200});
        p.remplir(0xFFFFFFFFu);
        for (int x = 10; x < 290; ++x) { p.set(x, 10, 0xFF404040u); p.set(x, 189, 0xFF404040u); }
        for (int y = 10; y < 190; ++y) { p.set(10, y, 0xFF404040u); p.set(289, y, 0xFF404040u); }
        for (int x = 50; x < 150; ++x) { p.set(x, 50, 0xFF404040u); p.set(x, 99, 0xFF404040u); }
        for (int y = 50; y < 100; ++y) { p.set(50, y, 0xFF404040u); p.set(149, y, 0xFF404040u); }
        auto cadres = boites_fermees(p);
        CHECK(cadres.size() == 2);
        if (cadres.size() == 2) {
            CHECK((cadres[0].rect == Rect{10, 10, 280, 180}));   // la plus grande d'abord
            CHECK((cadres[1].rect == Rect{50, 50, 100, 50}));
        }
    }

    std::printf("unit_regions : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}