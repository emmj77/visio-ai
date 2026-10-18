// src/perceive/glyph.h — découper une bande de texte en lettres.
//
// Le découpage est la seule partie qui ne peut pas être évitée : pour nommer une lettre,
// il faut savoir où elle commence et où elle finit. Il se fait sur les composantes
// connexes de l'encre, puis par regroupement horizontal (le point du « i » rejoint sa
// hampe). La reconnaissance, elle, est dans fonte.h.
#pragma once
#include "common/rect.h"
#include "core/frame.h"
#include "perceive/text_lines.h"

#include <cstdint>
#include <vector>

namespace visio {

constexpr int GP_W = 24;              // largeur maximale d'un glyphe
constexpr int GP_H = 24;              // hauteur maximale
constexpr int GP_PIX = GP_W * GP_H;

struct Glyphe {
    Rect    rect{};                 // boîte du glyphe, coordonnées écran
    uint8_t w = 0, h = 0;           // taille de l'encre, en pixels
    uint8_t px[GP_PIX] = {0};       // intensité d'encre, 0 à 255, ligne par ligne (GP_W de large)
};

// Découpe une bande MONOSPACE sur la grille du pas : les frontières des lettres sont à
// x_min + k·pas, donc il n'y a plus rien à deviner — les lettres soudées par
// l'antialiasing retrouvent leur place. `cellules` reçoit l'indice de cellule de chaque
// glyphe, ce qui donne les espaces (une cellule vide = une espace).
std::vector<Glyphe> extraire_glyphes_pas(const Frame& f, const LigneTexte& ligne, uint32_t fond,
                                         int pas, std::vector<int>* cellules = nullptr);

// Découpe une bande de texte en glyphes.
std::vector<Glyphe> extraire_glyphes(const Frame& f, const LigneTexte& ligne, uint32_t fond,
                                     int seuil = 24);

// Écart moyen par pixel entre deux glyphes de même taille, en fraction de 255.
// Les deux sont rangés en lignes de GP_W.
float ecart_glyphes(const Glyphe& a, const Glyphe& b);

} // namespace visio
