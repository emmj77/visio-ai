// tests/unit_text_lines.cpp — la découpe en lignes de texte, sur une image construite.
#include "perceive/text_lines.h"

#include <cstdio>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

int main() {
    Frame f = Frame::pour(Rect{0, 0, 400, 100});
    f.remplir(0xFFFFFFFFu);

    // trois « lignes » de six blocs de 8x10, comme des mots en gras
    const int y_debut[3] = {10, 40, 70};
    for (int l = 0; l < 3; ++l)
        for (int c = 0; c < 6; ++c)
            for (int y = 0; y < 10; ++y)
                for (int x = 0; x < 8; ++x)
                    f.set(10 + c * 20 + x, y_debut[l] + y, 0xFF000000u);

    CHECK(couleur_fond(f, 3) == 0xFFFFFFFFu);

    const auto lignes = lignes_de_texte(f, couleur_fond(f, 3));
    CHECK(lignes.size() == 3);
    if (lignes.size() == 3) {
        for (int l = 0; l < 3; ++l) {
            CHECK(lignes[(size_t)l].rect.y == y_debut[l]);
            CHECK(lignes[(size_t)l].rect.h == 10);
            CHECK(lignes[(size_t)l].rect.x == 10);
            CHECK(lignes[(size_t)l].rect.w == 108);      // 10 + 5*20 + 8 = 118, moins x0=10
            CHECK(lignes[(size_t)l].densite > 0.30);
            CHECK(lignes[(size_t)l].densite < 0.60);
        }
    }

    // un aplat plein n'est pas du texte
    {
        Frame g = Frame::pour(Rect{0, 0, 200, 80});
        g.remplir(0xFFFFFFFFu);
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 110; ++x) g.set(x, y, 0xFF000000u);
        const auto l = lignes_de_texte(g, couleur_fond(g, 3));
        CHECK(l.empty());
    }

    // un fond sombre : c'est le texte clair qui est de l'encre
    {
        Frame h = Frame::pour(Rect{0, 0, 200, 60});
        h.remplir(0xFF101010u);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 7; ++x)
                    h.set(8 + c * 14 + x, 20 + y, 0xFFE0E0E0u);
        const uint32_t fond = couleur_fond(h, 3);
        CHECK(fond == 0xFF101010u);
        const auto l = lignes_de_texte(h, fond);
        CHECK(l.size() == 1);
        if (l.size() == 1) {
            CHECK(l[0].rect.y == 20);
            CHECK(l[0].rect.h == 9);
        }
    }

    // les coordonnées rendues sont celles de l'écran, pas celles de la frame
    {
        Frame k = Frame::pour(Rect{600, 400, 200, 60});
        k.remplir(0xFFFFFFFFu);
        // cinq fragments de 4 px de large : du texte, pas un aplat (la densité le dit)
        for (int c = 0; c < 5; ++c)
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 4; ++x) k.set(620 + c * 12 + x, 415 + y, 0xFF000000u);
        const auto l = lignes_de_texte(k, couleur_fond(k, 3));
        CHECK(l.size() == 1);
        if (l.size() == 1) CHECK((l[0].rect == Rect{620, 415, 52, 9}));
    }

    std::printf("unit_text_lines : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
