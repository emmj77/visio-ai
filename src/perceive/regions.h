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
    // Renseigné par `boites_fermees` seulement : la région est une forme FERMÉE.
    bool     cadre = false;
    // Part de l'encre posée sur le bord de la boîte : 1,0 = un rectangle, 0,3 = un rond
    // ou un triangle (le trait ne touche le bord qu'en quelques points). C'est ce qui
    // permet au plan de dessiner un cadre en rectangle et un rond... en rond.
    double   sur_bord = 1.0;
};

// `quant` : bits ignorés par canal (5 = on regroupe les couleurs proches).
// `aire_min` : on jette les régions dont la boîte est plus petite.
// `remplissage_min` : on jette les régions trop trouées (texte, dégradés).
// Le résultat est trié par aire décroissante.
std::vector<Region> regions_uniformes(const Frame& f, int quant = 5,
                                      int64_t aire_min = 64,
                                      double remplissage_min = 0.55);

// Les BOÎTES FERMÉES : un bouton, une carte, un panneau, un champ, un rond, un triangle.
// L'idée est topologique, pas géométrique : une forme est « fermée » quand elle sépare un
// intérieur de l'extérieur. On la lit comme de l'ENCRE (pixel qui s'écarte du fond), on
// noie l'extérieur depuis le bord de sa boîte, et ce que l'eau ne touche pas est un
// intérieur. Un cadre, un rond, un triangle passent ; un trait, une croix, une lettre
// laissent tout communiquer et échouent.
// Pourquoi l'encre et non la couleur : une bordure de 1 px est antialiasée sur ses coins,
// chaque pixel d'arc a sa propre couleur, et une lecture par couleur casse le cadre en
// quatre morceaux (mesuré : coins arrondis = 0 boîte par couleur, 1 par encre).
//   seuil_encre     : écart par canal au-delà duquel un pixel est de l'encre
//   cote_min        : plus petit côté accepté (une lettre fait moins)
//   aire_min        : aire de la boîte (pas du trait)
//   remplissage_max : au-delà, c'est une région pleine, déjà vue par `regions_uniformes`
//   fond            : 0 = couleur dominante de la frame, calculée ici
//   bord_min_petit  : pour une petite forme (aire < 600 px), part minimale d'encre sur son
//                     bord. Une case à cocher est un cadre (1,0) ; une lettre fermée « o »
//                     est un anneau courbe (0,45). 0 = pas de filtre : une pastille RONDE
//                     de 14 px redevient une forme, exactement comme la lettre « o » — la
//                     géométrie ne les sépare pas, seul le contexte le ferait.
// `couleur` est celle du trait, `pixels` celui du trait. Trié par aire décroissante.
std::vector<Region> boites_fermees(const Frame& f, int seuil_encre = 8, int cote_min = 10,
                                   int64_t aire_min = 150, double remplissage_max = 0.65,
                                   uint32_t fond = 0, double bord_min_petit = 0.80);

// Les APLATS BORDÉS : un bouton dont le fond diffère du fond de page n'est pas un cadre —
// c'est une région pleine, et `boites_fermees` l'écarte (remplissage 0,95). Mais si son bord
// porte une teinte distincte et que son intérieur est UNI, la bordure est réelle et mérite
// d'être dans le plan.
// Discriminant : intérieur uniforme (>= 90 % des pixels à ±6 de la moyenne) ET une BORDURE
// réelle — la bande à 1 px du rect diffère de celle à 4 px (le fond de la page). Un aplat
// qui contraste simplement avec le fond n'a pas de trait : il reste un aplat. Une ligne de
// texte a un intérieur bigarré et tombe d'elle-même.
std::vector<Region> aplats_bordes(const Frame& f, const std::vector<Region>& aplats,
                                  int epaisseur = 1, int ecart_min = 10,
                                  int64_t aire_min = 150);

} // namespace visio
