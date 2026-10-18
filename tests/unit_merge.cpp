// tests/unit_merge.cpp — la fusion des rectangles abîmés, sans écran.
#include "common/rect.h"

#include <cstdio>
#include <vector>

using visio::Rect;
using visio::merge_rects;

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

int main() {
    // deux rectangles lointains restent deux
    {
        std::vector<Rect> in{{0, 0, 10, 10}, {100, 100, 10, 10}};
        auto out = merge_rects(in, 0);
        CHECK(out.size() == 2);
    }
    // deux rectangles qui se chevauchent n'en font qu'un
    {
        std::vector<Rect> in{{0, 0, 20, 20}, {10, 10, 20, 20}};
        auto out = merge_rects(in, 0);
        CHECK(out.size() == 1);
        CHECK((out[0] == Rect{0, 0, 30, 30}));
    }
    // une distance plus petite que le gap suffit à réunir
    {
        std::vector<Rect> in{{0, 0, 10, 10}, {14, 0, 10, 10}};
        CHECK(merge_rects(in, 0).size() == 2);
        CHECK(merge_rects(in, 4).size() == 1);
    }
    // la fusion est transitive
    {
        std::vector<Rect> in{{0, 0, 10, 10}, {12, 0, 10, 10}, {24, 0, 10, 10}};
        auto out = merge_rects(in, 2);
        CHECK(out.size() == 1);
        CHECK(out[0].area() == 34 * 10);
    }
    // l'ordre d'entrée ne change rien
    {
        std::vector<Rect> a{{0, 0, 10, 10}, {50, 50, 10, 10}, {5, 5, 10, 10}};
        std::vector<Rect> b{{5, 5, 10, 10}, {0, 0, 10, 10}, {50, 50, 10, 10}};
        auto oa = merge_rects(a, 0);
        auto ob = merge_rects(b, 0);
        CHECK(oa.size() == ob.size());
        for (size_t i = 0; i < oa.size() && i < ob.size(); ++i) CHECK(oa[i] == ob[i]);
    }
    // les rectangles vides sont ignorés
    {
        std::vector<Rect> in{{0, 0, 0, 0}, {10, 10, 5, 5}, {}};
        auto out = merge_rects(in, 0);
        CHECK(out.size() == 1);
    }
    // aucune perte d'aire : la fusion ne fait que grossir
    {
        std::vector<Rect> in{{0, 0, 10, 10}, {30, 0, 10, 10}, {60, 0, 10, 10}};
        int64_t avant = 0;
        for (auto& r : in) avant += r.area();
        int64_t apres = 0;
        for (auto& r : merge_rects(in, 0)) apres += r.area();
        CHECK(apres == avant);
    }

    std::printf("unit_merge : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
