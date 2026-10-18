// tests/integration_ocr.cpp — de la ligne de texte détectée au caractère lu.
// Les images témoins sont produites par tools/gen_fixtures.py (texte « Enregistrer sous... »).
#include "perceive/ocr.h"
#include "perceive/text_lines.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

static Frame lire_raw(const char* chemin) {
    Frame f;
    FILE* fh = std::fopen(chemin, "rb");
    if (!fh) return f;
    uint32_t w = 0, h = 0;
    if (std::fread(&w, 4, 1, fh) != 1 || std::fread(&h, 4, 1, fh) != 1) { std::fclose(fh); return f; }
    std::vector<uint8_t> gris((size_t)w * h);
    if (std::fread(gris.data(), 1, gris.size(), fh) != gris.size()) { std::fclose(fh); return f; }
    std::fclose(fh);
    f = Frame::pour(Rect{0, 0, (int)w, (int)h});
    for (size_t i = 0; i < gris.size(); ++i) {
        const uint32_t v = gris[i];
        f.px[i] = 0xFF000000u | (v << 16) | (v << 8) | v;
    }
    return f;
}

int main() {
    Ocr ocr;
    if (!ocr.dispo()) {
        std::printf("SKIP: %s\n", ocr.erreur());
        return 77;   // SKIP_RETURN_CODE dans CMakeLists
    }

    const char* fichiers[] = {"tests/data/texte_11.raw", "tests/data/texte_13.raw",
                              "tests/data/texte_16.raw"};
    for (const char* chemin : fichiers) {
        const Frame f = lire_raw(chemin);
        CHECK(!f.vide());
        if (f.vide()) continue;

        const uint32_t fond = couleur_fond(f, 3);
        const auto lignes = lignes_de_texte(f, fond);
        CHECK(lignes.size() == 1);
        if (lignes.empty()) continue;

        const std::string lu = ocr.lire(f, lignes[0].rect);
        std::printf("%-24s ligne %dx%d @%d,%d densite %.2f -> « %s »\n", chemin,
                    lignes[0].rect.w, lignes[0].rect.h, lignes[0].rect.x, lignes[0].rect.y,
                    lignes[0].densite, lu.c_str());
        CHECK(lu.find("nregistrer") != std::string::npos);
        CHECK(lu.find("sous") != std::string::npos);
    }

    std::printf("integration_ocr : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
