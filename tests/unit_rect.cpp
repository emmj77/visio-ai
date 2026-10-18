// tests/unit_rect.cpp — les fondations géométriques, vérifiées sans écran.
// Aucun framework : un échec imprime la ligne et le compte d'échecs sort en code 1.
#include "common/rect.h"

#include <cstdio>

using visio::Rect;
using visio::intersect;
using visio::unite;
using visio::clamp;
using visio::quantize;
using visio::for_each_tile;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) { ++g_pass; }                                                   \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

#define CHECK_RECT(got, ex)                                                      \
    do {                                                                         \
        const Rect g_ = (got); const Rect e_ = (ex);                              \
        if (g_ == e_) { ++g_pass; }                                               \
        else {                                                                    \
            std::fprintf(stderr, "ECHEC ligne %d : %s = {%d,%d,%d,%d}, attendu {%d,%d,%d,%d}\n", \
                         __LINE__, #got, g_.x, g_.y, g_.w, g_.h, e_.x, e_.y, e_.w, e_.h); \
            ++g_fail;                                                             \
        }                                                                         \
    } while (0)

int main() {
    // --- vide et aire -----------------------------------------------------
    CHECK(Rect{}.empty());
    CHECK((Rect{0, 0, 0, 10}.empty()));
    CHECK((!Rect{0, 0, 1, 1}.empty()));
    CHECK(Rect{}.area() == 0);
    CHECK((Rect{0, 0, 100, 50}.area() == 5000));

    // --- appartenance : bornes incluses à gauche, exclues à droite --------
    const Rect r{10, 20, 100, 50};
    CHECK(r.contains(10, 20));
    CHECK(r.contains(109, 69));
    CHECK(!r.contains(110, 69));
    CHECK(!r.contains(109, 70));
    CHECK(r.contains(Rect{50, 30, 10, 10}));
    CHECK(!r.contains(Rect{50, 30, 100, 10}));

    // --- intersection -----------------------------------------------------
    CHECK_RECT(intersect(Rect{0, 0, 100, 100}, Rect{50, 50, 100, 100}), (Rect{50, 50, 50, 50}));
    CHECK_RECT(intersect(Rect{0, 0, 10, 10}, Rect{20, 20, 10, 10}), (Rect{}));
    CHECK_RECT(intersect(Rect{0, 0, 10, 10}, Rect{10, 0, 10, 10}), (Rect{}));   // se touchent
    CHECK_RECT(intersect(Rect{5, 5, 100, 100}, Rect{0, 0, 20, 20}), (Rect{5, 5, 15, 15}));
    CHECK_RECT(intersect(Rect{0, 0, 100, 100}, Rect{10, 10, 10, 10}), (Rect{10, 10, 10, 10}));

    // --- enveloppe --------------------------------------------------------
    CHECK_RECT(unite(Rect{0, 0, 10, 10}, Rect{20, 30, 10, 10}), (Rect{0, 0, 30, 40}));
    CHECK_RECT(unite(Rect{0, 0, 10, 10}, Rect{}), (Rect{0, 0, 10, 10}));
    CHECK_RECT(unite(unite(Rect{0, 0, 10, 10}, Rect{20, 0, 10, 10}), Rect{5, 50, 10, 10}),
               (Rect{0, 0, 30, 60}));

    // --- clamp ------------------------------------------------------------
    CHECK_RECT(clamp(Rect{-10, -10, 50, 50}, Rect{0, 0, 3600, 1080}), (Rect{0, 0, 40, 40}));
    CHECK_RECT(clamp(Rect{3500, 1000, 500, 500}, Rect{0, 0, 3600, 1080}), (Rect{3500, 1000, 100, 80}));
    CHECK_RECT(clamp(Rect{5000, 5000, 10, 10}, Rect{0, 0, 3600, 1080}), (Rect{}));

    // --- quantification ---------------------------------------------------
    CHECK_RECT(quantize(Rect{0, 0, 100, 50}, 4), (Rect{0, 0, 100, 52}));
    CHECK_RECT(quantize(Rect{101, 99, 50, 20}, 4), (Rect{100, 96, 52, 24}));
    CHECK_RECT(quantize(Rect{0, 0, 10, 10}, 1), (Rect{0, 0, 10, 10}));
    CHECK(quantize(quantize(Rect{101, 99, 50, 20}, 4), 4) == quantize(Rect{101, 99, 50, 20}, 4));

    // stabilité : un déplacement de 1 px ne change l'origine que s'il franchit un multiple de q
    CHECK(quantize(Rect{101, 99, 50, 20}, 4).x == quantize(Rect{102, 99, 50, 20}, 4).x);
    CHECK(quantize(Rect{103, 99, 50, 20}, 4).x != quantize(Rect{104, 99, 50, 20}, 4).x);

    // --- tuilage : couvre exactement, sans recouvrement -------------------
    {
        int64_t sum = 0, n = 0;
        for_each_tile(Rect{0, 0, 100, 50}, 32, [&](const Rect& t) { sum += t.area(); ++n; });
        CHECK(n == 8);
        CHECK(sum == 5000);
    }
    {
        int64_t sum = 0;
        for_each_tile(Rect{13, 7, 400, 300}, 32, [&](const Rect& t) {
            sum += t.area();
            CHECK(t.x >= 13 && t.y >= 7 && t.right() <= 413 && t.bottom() <= 307);
        });
        CHECK(sum == 400 * 300);
    }

    std::printf("unit_rect : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
