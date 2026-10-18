// src/perceive/catalog.h — la liste nommée de ce qui est cliquable.
//
// C'est la pièce qui transforme « voici 95 rectangles » en « voici 12 boutons ».
// L'agent ne reçoit jamais que ce texte-là : un identifiant, un type, une géométrie.
#pragma once
#include "common/rect.h"
#include "perceive/regions.h"
#include "perceive/windows.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace visio {

enum class Genre : uint8_t {
    Fenetre = 0,
    Panneau,
    Barre,
    Bouton,
    Champ,
    Texte,
    Cadre,     // une forme fermée : bouton bordé, carte, rond, triangle
    Inconnu,
};

const char* nom_genre(Genre g);

struct Element {
    uint64_t id = 0;
    Genre    genre = Genre::Inconnu;
    Rect     rect{};
    uint32_t couleur = 0;
    Window   fenetre = 0;
    int      seq = 0;       // change quand l'élément change
    int64_t  pixels = 0;
    bool     cadre = false; // vient de `boites_fermees`
    double   sur_bord = 1.0;
};

// Catalogue persistant : il se souvient, d'une image à l'autre, de ce qu'il a déjà vu,
// pour que les identifiants restent stables. Un identifiant stable, c'est la différence
// entre « clique sur le bouton » et « clique sur le troisième rectangle ».
class Catalogue {
public:
    // Met à jour la fenêtre et ses régions. Renvoie les éléments de cette fenêtre,
    // triés de haut en bas puis de gauche à droite.
    const std::vector<Element>& maj(Window fenetre, const std::vector<Region>& regions);

    // Tous les éléments connus, toutes fenêtres confondues.
    const std::map<uint64_t, Element>& tous() const noexcept { return elements_; }

    const Element* par_id(uint64_t id) const;

    // Le plus petit élément qui contient le point — celui qu'un clic atteindrait.
    const Element* sous_point(int x, int y) const;

    // Rendu compact, une ligne par élément : c'est le texte que lit le modèle.
    std::string texte() const;

    // Nombre de régions écartées parce qu'elles ont la taille d'un glyphe : sur un
    // terminal, chaque lettre est une région de couleur uniforme. Ce n'est pas une cible
    // de clic — c'est le travail de la couche texte (M3-2), pas du catalogue.
    size_t glyphes_ecartes = 0;

    void oublier_fenetre(Window w);
    void vider();

private:
    std::map<uint64_t, Element> elements_;
    std::vector<Element> courants_;
    int seq_ = 1;
};

// Identifiant stable : fonction de la fenêtre, du genre et de la géométrie.
uint64_t identifiant(Window fenetre, Genre genre, const Rect& r);

// Le genre se déduit de la forme et de la place — c'est une proposition, pas une certitude.
Genre deviner_genre(const Rect& r, const Rect& fenetre, double remplissage);

} // namespace visio
