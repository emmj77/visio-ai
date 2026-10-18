// tests/unit_tile_diff.cpp — le diff par tuiles, sur des frames synthétiques.
#include "core/frame.h"
#include "core/tile_diff.h"

#include <cstdio>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

int main() {
    const Rect reg{100, 200, 128, 96};      // 4 x 3 tuiles de 32
    Frame a = Frame::pour(reg), b = Frame::pour(reg);
    a.remplir(0xFF000000u);
    b.remplir(0xFF000000u);

    CHECK(frames_identiques(a, b));
    CHECK(tile_diff(a, b, 32, 0).empty());

    // un pixel change : exactement une tuile
    b.set(101, 201, 0xFFFFFFFFu);
    CHECK(!frames_identiques(a, b));
    {
        auto h = tile_diff(a, b, 32, 0);
        CHECK(h.size() == 1);
        if (!h.empty()) {
            CHECK((h[0].tile == Rect{100, 200, 32, 32}));
            CHECK(h[0].px_diff == 1);
        }
    }

    // deux pixels dans deux tuiles opposées : deux tuiles
    b.set(227, 295, 0xFFFFFFFFu);   // dernière tuile
    {
        auto h = tile_diff(a, b, 32, 0);
        CHECK(h.size() == 2);
    }

    // un seuil au-dessus du nombre de pixels différents : plus rien
    CHECK(tile_diff(a, b, 32, 5).empty());

    // une tuile entièrement différente : px_diff = 1024
    {
        Frame c = Frame::pour(reg);
        c.remplir(0xFFFFFFFFu);
        auto h = tile_diff(a, c, 32, 0);
        CHECK(h.size() == 12);
        if (!h.empty()) CHECK(h[0].px_diff == 1024);
    }

    // régions différentes : une seule tuile qui couvre tout (relecture forcée)
    {
        Frame d = Frame::pour(Rect{0, 0, 32, 32});
        auto h = tile_diff(a, d, 32, 0);
        CHECK(h.size() == 1);
        CHECK((h[0].tile == reg));
    }

    // tuile plus grande que la région
    {
        Frame e = Frame::pour(reg), f = Frame::pour(reg);
        e.remplir(1);
        f.remplir(2);
        auto h = tile_diff(e, f, 64, 0);
        CHECK(h.size() == 4);   // 2 x 2 tuiles de 64 : 128x96 tient dans 2x2
    }

    std::printf("unit_tile_diff : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
