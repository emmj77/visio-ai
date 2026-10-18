// src/core/tile_diff.h — ce qui a changé entre deux frames, tuile par tuile.
#pragma once
#include "common/rect.h"
#include "core/frame.h"

#include <vector>

namespace visio {

struct TileHit {
    Rect tile;
    int  px_diff = 0;   // nombre de pixels différents dans la tuile
};

// Compare deux frames de MÊME région. Une région différente est une erreur d'appel :
// on rend alors une seule tuile couvrant tout, pour forcer une relecture complète.
// `seuil` : nombre de pixels différents en dessous duquel on ignore (bruit de curseur).
std::vector<TileHit> tile_diff(const Frame& a, const Frame& b, int tuile = 32, int seuil = 0);

// Vrai si les deux frames sont identiques octet pour octet (chemin rapide).
bool frames_identiques(const Frame& a, const Frame& b) noexcept;

} // namespace visio
