// tests/unit_ocr.cpp — le texte d'une zone, lu par tesseract.
//
// L'image de reference est une VRAIE capture d'ecran (le bouton « Valider » de la page temoin,
// 118x30 px, texte antialiasé de 16 px) : c'est le cas qui faisait echouer le comparateur
// maison, qui rendait « Va][der ».
#include "perceive/ocr.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int echecs = 0, reussis = 0;

void verifier(bool ok, const char* quoi) {
    if (ok) { ++reussis; return; }
    ++echecs;
    std::printf("ECHEC %s\n", quoi);
}

// Charge un PPM binaire (P6) dans une Frame.
bool charger(const char* chemin, visio::Frame& f) {
    FILE* o = std::fopen(chemin, "rb");
    if (!o) return false;
    char magie[3] = {0};
    int w = 0, h = 0, max = 0;
    if (std::fscanf(o, "%2s %d %d %d", magie, &w, &h, &max) != 4 || std::strcmp(magie, "P6") ||
        w <= 0 || h <= 0) {
        std::fclose(o);
        return false;
    }
    std::fgetc(o);   // l'espace apres la valeur maximale
    f = visio::Frame::pour({0, 0, w, h});
    std::vector<unsigned char> buf((size_t)w * h * 3);
    if (std::fread(buf.data(), 1, buf.size(), o) != buf.size()) {
        std::fclose(o);
        return false;
    }
    std::fclose(o);
    for (int i = 0; i < w * h; ++i)
        f.px[(size_t)i] = 0xFF000000u | ((uint32_t)buf[(size_t)i * 3] << 16) |
                          ((uint32_t)buf[(size_t)i * 3 + 1] << 8) | buf[(size_t)i * 3 + 2];
    return true;
}

bool contient(const std::vector<visio::LigneOcr>& ls, const char* mot) {
    for (const auto& l : ls)
        if (l.texte.find(mot) != std::string::npos) return true;
    return false;
}

} // namespace

int main() {
    if (!visio::ocr_disponible()) {
        std::printf("tesseract absent : test saute\n");
        return 0;
    }
    visio::Frame f;
    const bool ok = charger("tests/data/valider.ppm", f);
    verifier(ok, "l'image de reference se charge");
    if (ok) {
        const auto ls = visio::ocr_zone(f, f.region);
        for (const auto& l : ls) std::printf("  lu : « %s » (y %d, h %d)\n", l.texte.c_str(), l.y, l.h);
        verifier(!ls.empty(), "la zone rend au moins une ligne");
        verifier(contient(ls, "Valider"), "le mot « Valider » est lu exactement");
        verifier(ls.size() == 1, "une seule ligne pour un bouton d'une ligne");
        if (!ls.empty()) verifier(ls[0].texte == "Valider", "aucun caractere parasite autour du mot");
    }
    std::printf("\nocr : %d reussis, %d echecs\n", reussis, echecs);
    return echecs ? 1 : 0;
}
