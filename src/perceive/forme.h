// src/perceive/forme.h — de quoi est faite une forme fermee, avant de savoir la dessiner.
#pragma once

#include "common/rect.h"

#include <string>
#include <utility>
#include <vector>

namespace visio {

// Une forme lue sur le contour, ensuite DESSINEE DEPUIS SON MODELE.
//
// Pourquoi un modele et pas des largeurs de rangees : mesure faite sur la page temoin, un rond
// de 122 px dans une case de 10x20 px tient sur 12 caracteres de large et 6 lignes de haut.
// Dessine a partir de la largeur mesuree de chaque rangee, il sort en rond-point : la barre du
// haut prend 80 % de la largeur (c'est le vrai diametre de la corde), les cotes n'ont plus de
// place pour s'incliner, et le resultat ressemble a une boite arrondie. Un sommet de triangle,
// lui, mesure 5 cases de large sur sa premiere rangee — et sort en trapeze tronque.
// En modelisant (ellipse ou polygone), on connait la PENTE EXACTE de chaque rangee, donc le
// caractere juste (| / \ - _ " ' . `) et la position juste du sommet.
struct Point {
    double x = 0, y = 0;
};

struct Forme {
    enum class Genre { Aucune, Ellipse, Polygone };
    Genre genre = Genre::Aucune;

    // Ellipse : centre et demi-axes, en pixels.
    double cx = 0, cy = 0, a = 0, b = 0;

    // Polygone : sommets dans l'ordre du contour, en pixels.
    std::vector<Point> sommets;

    double erreur = 0;   // ecart moyen au modele, en pixels (0 = parfait)
};

// Analyse un contour ferme (points ordonnes) et rend le modele qui le decrit le mieux.
Forme analyser_forme(const std::vector<std::pair<double, double>>& contour);

// Dessine la forme depuis son MODELE dans la grille de caracteres. `fen` est le rectangle
// capture, `cellx`/`celly` la taille d'une case en pixels.
void dessiner_forme(std::vector<std::string>& gr, const Forme& f, visio::Rect fen, int cellx,
                    int celly, int cols, int rows);

} // namespace visio
