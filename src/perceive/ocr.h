// src/perceive/ocr.h — lire les caractères d'un bandeau, avec tesseract.
//
// Chargé par `dlopen` : le binaire ne dépend pas de tesseract au link, et tourne
// sans lui (il dit simplement qu'il ne sait pas lire). Mesuré : 5,6 ms par bandeau
// de 400x25 une fois le moteur initialisé (init : 87 ms, une seule fois) — contre
// 95 à 155 ms pour le CLI, qui repaie le lancement à chaque appel.
#pragma once
#include "common/rect.h"
#include "core/frame.h"

#include <string>

namespace visio {

class Ocr {
public:
    Ocr();
    ~Ocr();

    Ocr(const Ocr&) = delete;
    Ocr& operator=(const Ocr&) = delete;

    bool dispo() const noexcept { return api_ != nullptr; }
    const char* erreur() const noexcept { return erreur_; }

    // Lit la zone `zone` (coordonnées écran) dans `f`. Une seule ligne de texte en sort,
    // les espaces multiples sont réduits. Renvoie "" si rien n'est lisible.
    std::string lire(const Frame& f, const Rect& zone);

private:
    bool charger();

    using TessAPI = void*;
    using FnCreate = TessAPI (*)();
    using FnInit = int (*)(TessAPI, const char*, const char*);
    using FnSetImage = void (*)(TessAPI, const unsigned char*, int, int, int, int);
    using FnText = char* (*)(TessAPI);
    using FnDelete = void (*)(TessAPI);
    using FnDeleteText = void (*)(char*);
    using FnSegMode = int (*)(TessAPI, int);   // TessBaseAPISetPageSegMode

    void* lib_ = nullptr;
    void* api_ = nullptr;
    const char* erreur_ = "";
    std::string tampon_gris_;

    FnCreate creer_ = nullptr;
    FnInit init_ = nullptr;
    FnSetImage setimage_ = nullptr;
    FnText lire_ = nullptr;
    FnDelete detruire_ = nullptr;
    FnDeleteText detruire_texte_ = nullptr;
    FnSegMode mode_segmentation_ = nullptr;
};

} // namespace visio
