// bench/bench_ocr.cpp — tesseract EN BIBLIOTHÈQUE, sans libtesseract-dev.
//
// Le paquet -dev tirerait 44 paquets, dont des montées de version de libssl3t64,
// libcurl4t64 et liblzma5 : trop pour ce qu'on en fait. L'API C de tesseract est un ABI
// stable ; on déclare les cinq fonctions dont on a besoin et on lie directement la .so.5.
//
//   bench_ocr 20        -> 20 passes par image témoin (tests/data/*.raw)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
typedef void* TessBaseAPI;
TessBaseAPI* TessBaseAPICreate(void);
int  TessBaseAPIInit3(TessBaseAPI*, const char* datapath, const char* language);
void TessBaseAPISetImage(TessBaseAPI*, const unsigned char* imagedata, int width, int height,
                         int bytes_per_pixel, int bytes_per_line);
char* TessBaseAPIGetUTF8Text(TessBaseAPI*);
void TessBaseAPIDelete(TessBaseAPI*);
void TessDeleteText(char*);
}

namespace {

struct Image { int w = 0, h = 0; std::vector<uint8_t> px; };

Image lire_raw(const char* chemin) {
    Image img;
    FILE* f = std::fopen(chemin, "rb");
    if (!f) { std::fprintf(stderr, "illisible : %s\n", chemin); return img; }
    uint32_t w = 0, h = 0;
    if (std::fread(&w, 4, 1, f) != 1 || std::fread(&h, 4, 1, f) != 1) { std::fclose(f); return img; }
    img.w = (int)w; img.h = (int)h;
    img.px.resize((size_t)w * h);
    if (std::fread(img.px.data(), 1, img.px.size(), f) != img.px.size()) img = {};
    std::fclose(f);
    return img;
}

} // namespace

int main(int argc, char** argv) {
    const int passes = argc > 1 ? std::atoi(argv[1]) : 20;
    const char* fichiers[] = {"tests/data/texte_11.raw", "tests/data/texte_13.raw",
                              "tests/data/texte_16.raw"};

    auto t0 = std::chrono::steady_clock::now();
    TessBaseAPI* api = TessBaseAPICreate();
    if (TessBaseAPIInit3(api, nullptr, "fra") != 0) {
        std::fprintf(stderr, "init tesseract impossible (langue fra absente ?)\n");
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("init (une seule fois) : %.1f ms\n\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count());

    for (const char* chemin : fichiers) {
        const Image img = lire_raw(chemin);
        if (img.w == 0) return 2;

        // premier appel : tesseract construit ses structures internes pour cette taille
        TessBaseAPISetImage(api, img.px.data(), img.w, img.h, 1, img.w);
        char* premier = TessBaseAPIGetUTF8Text(api);
        std::string texte = premier ? premier : "";
        if (premier) TessDeleteText(premier);

        auto debut = std::chrono::steady_clock::now();
        for (int i = 0; i < passes; ++i) {
            TessBaseAPISetImage(api, img.px.data(), img.w, img.h, 1, img.w);
            char* t = TessBaseAPIGetUTF8Text(api);
            if (t) TessDeleteText(t);
        }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - debut).count() / passes;

        // nettoyage du texte pour l'affichage
        std::string propre;
        for (char c : texte) if (c != '\n' && c != '\r') propre += c;
        std::printf("%-22s %3dx%-3d  %6.2f ms  texte=%s\n", chemin, img.w, img.h, ms,
                    propre.c_str());
    }

    TessBaseAPIDelete(api);
    return 0;
}
