#include "perceive/ocr.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

namespace visio {

namespace {

using TessBaseAPI = void*;
using FnCreate  = TessBaseAPI (*)();
using FnInit    = int (*)(TessBaseAPI, const char*, const char*);
using FnSetImage = void (*)(TessBaseAPI, const unsigned char*, int, int, int, int);
using FnText    = char* (*)(TessBaseAPI);
using FnDelete  = void (*)(TessBaseAPI);
using FnDeleteText = void (*)(char*);

const char* const CANDIDATS[] = {"libtesseract.so.5", "libtesseract.so",
                                 "/usr/lib/x86_64-linux-gnu/libtesseract.so.5"};

} // namespace

Ocr::Ocr() { charger(); }

bool Ocr::charger() {
    for (const char* nom : CANDIDATS) {
        lib_ = dlopen(nom, RTLD_NOW);
        if (lib_) break;
    }
    if (!lib_) { erreur_ = "libtesseract introuvable"; return false; }

    const auto create = (FnCreate)dlsym(lib_, "TessBaseAPICreate");
    const auto init   = (FnInit)dlsym(lib_, "TessBaseAPIInit3");
    const auto setimg = (FnSetImage)dlsym(lib_, "TessBaseAPISetImage");
    const auto texte  = (FnText)dlsym(lib_, "TessBaseAPIGetUTF8Text");
    const auto detruire = (FnDelete)dlsym(lib_, "TessBaseAPIDelete");
    const auto detruire_texte = (FnDeleteText)dlsym(lib_, "TessDeleteText");
    const auto mode_seg = (FnSegMode)dlsym(lib_, "TessBaseAPISetPageSegMode");
    if (!create || !init || !setimg || !texte || !detruire || !detruire_texte) {
        erreur_ = "symboles tesseract incomplets";
        return false;
    }
    creer_ = create;
    init_ = init;
    setimage_ = setimg;
    lire_ = texte;
    detruire_ = detruire;
    detruire_texte_ = detruire_texte;
    mode_segmentation_ = mode_seg;

    api_ = creer_();
    if (!api_) { erreur_ = "TessBaseAPICreate a echoue"; return false; }
    if (init_(api_, nullptr, "fra") != 0) {
        erreur_ = "langue fra absente";
        detruire_(api_);
        api_ = nullptr;
        return false;
    }
    // PSM 7 = une seule ligne de texte. APRÈS Init3 : l'initialisation remet le mode à
    // PSM 3 (analyse de page), qui coûtait 240 ms sur un bandeau de 830x10.
    if (mode_segmentation_) mode_segmentation_(api_, 7);
    return true;
}

std::string Ocr::lire(const Frame& f, const Rect& zone) {
    if (!api_ || f.vide()) return "";
    const Rect z = intersect(zone, f.region);
    if (z.empty()) return "";

    // tesseract veut du 8 bits : on calcule la luminance, on ne copie pas la couleur
    const size_t n = (size_t)z.w * z.h;
    tampon_gris_.resize(n);
    for (int y = 0; y < z.h; ++y) {
        const uint32_t* p = f.px.data() + (size_t)(z.y - f.region.y + y) * f.region.w +
                            (z.x - f.region.x);
        uint8_t* q = (uint8_t*)tampon_gris_.data() + (size_t)y * z.w;
        for (int x = 0; x < z.w; ++x) {
            const uint32_t c = p[x];
            q[x] = (uint8_t)((77u * ((c >> 16) & 0xFF) + 150u * ((c >> 8) & 0xFF) +
                              29u * (c & 0xFF)) >> 8);
        }
    }

    // tesseract est entraîné sur du texte sombre sur fond clair. Un terminal, c'est
    // l'inverse : on inverse la bande quand elle est globalement sombre.
    {
        int64_t somme = 0;
        const uint8_t* q = (const uint8_t*)tampon_gris_.data();
        for (size_t i = 0; i < n; ++i) somme += q[i];
        if (somme / (int64_t)n < 110) {
            uint8_t* p = (uint8_t*)tampon_gris_.data();
            for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)(255 - p[i]);
        }
    }

    setimage_(api_, (const unsigned char*)tampon_gris_.data(), z.w, z.h, 1, z.w);
    char* brut = lire_(api_);
    std::string out;
    if (brut) {
        bool espace = false;
        for (const char* c = brut; *c; ++c) {
            if (*c == '\n' || *c == '\r') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
            const bool est_espace = (*c == ' ' || *c == '\t');
            if (est_espace) { espace = true; continue; }
            if (espace && !out.empty() && out.back() != ' ') out += ' ';
            espace = false;
            out += *c;
        }
        detruire_texte_(brut);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

Ocr::~Ocr() {
    if (api_ && detruire_) detruire_(api_);
    if (lib_) dlclose(lib_);
}

} // namespace visio
