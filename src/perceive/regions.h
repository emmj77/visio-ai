// src/perceive/regions.h — « quoi est où » sans rien savoir de l'application :
// on segmente par couleur uniforme. Un bouton plat, un panneau, une barre d'outils
// sont des régions de couleur constante ; leurs bords donnent la géométrie exacte.
#pragma once
#include "common/rect.h"
#include "core/frame.h"

#include <cstdint>
#include <vector>

namespace visio {

struct Region {
    Rect     rect{};          // en coordonnées de l'écran
    uint32_t couleur = 0;     // couleur moyenne de la région (ARGB)
    int64_t  pixels = 0;      // nombre de pixels de la région
    double   remplissage = 0; // pixels / aire de rect : 1.0 = rectangle plein
};

// `quant` : bits ignorés par canal (5 = on regroupe les couleurs proches).
// `aire_min` : on jette les régions dont la boîte est plus petite.
// `remplissage_min` : on jette les régions trop trouées (texte, dégradés).
// Le résultat est trié par aire décroissante.
std::vector<Region> regions_uniformes(const Frame& f, int quant = 5,
                                      int64_t aire_min = 64,
                                      double remplissage_min = 0.55);

} // namespace visio
