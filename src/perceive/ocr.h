#pragma once
// Lecture du TEXTE d'une zone par un moteur OCR (tesseract).
//
// Pourquoi un moteur externe : le comparateur maison (rendu FreeType + recouvrement) sortait
// « Va][der » pour « Valider » et « Yr <V \L » pour « booeba » sur du texte d'ecran antialiasé,
// de police et de taille inconnues. Mesure sur la page temoin et sur une vraie page : tesseract
// rend les mots exacts, sur CPU, sans modele a telecharger.
//
// On appelle le BINAIRE (pas la bibliotheque) : libtesseract est presente mais sans en-tetes
// de developpement, et un appel par zone coute ~50 ms, ce qui est le prix de la simplicite.

#include <string>
#include <vector>

#include "common/rect.h"
#include "core/frame.h"

namespace visio {

struct LigneOcr {
    int y = 0;             // haut de la ligne, relatif au coin de la zone lue
    int h = 0;
    double conf = 0;       // confiance moyenne rendue par le moteur (0 a 100)
    std::string texte;
};

// Le texte d'une zone, ligne par ligne. `r` est en coordonnees de la frame.
// Renvoie un vecteur vide si la zone n'a pas de texte ou si tesseract n'est pas la.
std::vector<LigneOcr> ocr_zone(const Frame& f, Rect r, const char* langue = "fra+eng");

// PLUSIEURS zones en UN SEUL appel au moteur : les zones sont empilees dans une image avec
// une bande blanche entre elles, puis chaque ligne retrouve sa zone par son ordonnee.
// Mesure : 10 zones coutaient 2 337 ms en appels separes (le demarrage de tesseract domine) ;
// un seul appel passe sous 400 ms.
std::vector<std::vector<LigneOcr>> ocr_zones(const Frame& f, const std::vector<Rect>& zones,
                                             const char* langue = "fra+eng");

// Vrai si le binaire tesseract repond (teste une fois, resultat garde).
bool ocr_disponible();

} // namespace visio
