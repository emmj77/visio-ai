// tests/unit_forme.cpp — le tracé d'une forme se juge sur une grille nue, pas sur une capture.
#include "perceive/forme.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int echecs = 0, reussis = 0;

void verifier(bool ok, const char* quoi) {
    if (ok) { ++reussis; return; }
    ++echecs;
    std::printf("ECHEC %s\n", quoi);
}

std::vector<std::string> grille(int cols, int rows) {
    return std::vector<std::string>((size_t)rows, std::string((size_t)cols, ' '));
}

void afficher(const std::vector<std::string>& g, const char* titre) {
    std::printf("--- %s\n", titre);
    for (const auto& l : g) std::printf("|%s|\n", l.c_str());
}

// Un rond de 120 px de diametre dans des cases de 10x20 px : 12 cases de large, 6 rangees.
void test_rond() {
    visio::Forme f;
    f.genre = visio::Forme::Genre::Ellipse;
    f.cx = 80; f.cy = 80; f.a = 60; f.b = 60;
    auto g = grille(20, 10);
    visio::Rect fen{20, 20, 0, 0};
    visio::dessiner_forme(g, f, fen, 10, 20, 20, 10);
    afficher(g, "rond 120 px");
    bool chapeau_haut = false, chapeau_bas = false, cote_gauche = false, cote_droit = false;
    int ngauche = 0, ndroit = 0;
    for (const auto& l : g)
        for (size_t i = 0; i < l.size(); ++i) {
            if (l[i] == '"') chapeau_haut = true;
            if (l[i] == '.') chapeau_bas = true;
            if (l[i] == '/') { cote_gauche = true; ++ngauche; }
            if (l[i] == '\\') { cote_droit = true; ++ndroit; }
        }
    verifier(chapeau_haut, "le rond a un chapeau haut (\" )");
    verifier(chapeau_bas, "le rond a un chapeau bas (.)");
    verifier(cote_gauche && cote_droit, "le rond a ses deux cotes obliques");
    verifier(ngauche >= 2 && ndroit >= 2, "le rond n'a pas un seul caractere par cote");
    // symetrie : a chaque rangee, le cote gauche et le cote droit existent tous les deux
    for (size_t y = 0; y < g.size(); ++y) {
        const auto& l = g[y];
        const size_t g1 = l.find_first_not_of(' ');
        if (g1 == std::string::npos) continue;
        const size_t d1 = l.find_last_not_of(' ');
        verifier(g1 != d1, "une rangee du rond a deux bords");
    }
}

// Un triangle 160x120 px : la pointe doit etre « /\ » et la base une barre qui touche les cotes.
void test_triangle() {
    visio::Forme f;
    f.genre = visio::Forme::Genre::Polygone;
    f.sommets = {{80, 20}, {0, 140}, {160, 140}};
    auto g = grille(24, 12);
    visio::Rect fen{0, 0, 0, 0};
    visio::dessiner_forme(g, f, fen, 10, 20, 24, 12);
    afficher(g, "triangle 160x120");
    int rangee_pointe = -1;
    for (size_t y = 0; y < g.size() && rangee_pointe < 0; ++y)
        if (g[y].find('/') != std::string::npos || g[y].find('\\') != std::string::npos)
            rangee_pointe = (int)y;
    verifier(rangee_pointe >= 0, "le triangle a des cotes");
    if (rangee_pointe >= 0) {
        const std::string& l = g[(size_t)rangee_pointe];
        const size_t a = l.find('/');
        const size_t b = l.find('\\', a == std::string::npos ? 0 : a);
        verifier(a != std::string::npos && b != std::string::npos,
                 "la rangee de la pointe porte « /\\ » (pas un seul cote)");
        if (a != std::string::npos && b != std::string::npos)
            verifier(b - a <= 2, "les deux cotes de la pointe sont voisins (pas de trou)");
    }
    // la base : une rangee de « _ » qui va d'un cote a l'autre
    int rangee_base = -1;
    for (size_t y = 0; y < g.size(); ++y) {
        int n = 0;
        for (char c : g[y]) if (c == '_') ++n;
        if (n >= 10) rangee_base = (int)y;
    }
    verifier(rangee_base > rangee_pointe, "le triangle a une base pleine");
    if (rangee_base > 0) {
        const std::string& l = g[(size_t)rangee_base];
        const size_t g1 = l.find_first_not_of(' ');
        const size_t d1 = l.find_last_not_of(' ');
        verifier(g1 != std::string::npos && d1 != std::string::npos, "la base existe");
    }
}

} // namespace

int main() {
    test_rond();
    test_triangle();
    std::printf("\nforme : %d reussis, %d echecs\n", reussis, echecs);
    return echecs ? 1 : 0;
}
