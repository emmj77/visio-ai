// src/perceive/text_lines.h — trouver le texte sans le lire.
//
// Un terminal, une page web, une boîte de dialogue : le texte est fait de petits
// fragments de couleur sur un fond uniforme. Le catalogue les écarte (ce ne sont pas
// des cibles de clic) ; ici on les regroupe en LIGNES. C'est le bandeau qu'on enverra
// à l'atlas de glyphes ou à l'OCR — jamais le pixel isolé.
#pragma once
#include "common/rect.h"
#include "core/frame.h"

#include <cstdint>
#include <vector>

namespace visio {

struct LigneTexte {
    Rect    rect{};        // boîte de la ligne, en coordonnées écran
    int64_t encre = 0;     // nombre de pixels différents du fond
    double  densite = 0;   // encre / aire — le texte est clairsemé, un aplat non
};

// Couleur de fond dominante de la frame (histogramme quantifié, pas de moyenne :
// une moyenne entre du noir et du blanc donne du gris, qui n'existe nulle part).
uint32_t couleur_fond(const Frame& f, int quant = 3);

// `fond` : couleur de référence. `seuil` : écart par canal au-delà duquel un pixel
// est de l'encre. `densite_max` : au-delà, c'est un aplat, pas du texte.
std::vector<LigneTexte> lignes_de_texte(const Frame& f, uint32_t fond, int seuil = 24,
                                        int hauteur_max = 48, double densite_max = 0.60);

} // namespace visio
