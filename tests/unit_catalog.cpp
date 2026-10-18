// tests/unit_catalog.cpp — le catalogue nommé, sans écran : on lui donne des régions.
#include "perceive/catalog.h"

#include <cstdio>
#include <string>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

static const Element* chercher(const std::vector<Element>& v, Genre g) {
    for (const auto& e : v) if (e.genre == g) return &e;
    return nullptr;
}

int main() {
    const Window f = 0x1234;
    std::vector<Region> regions = {
        //  rect                    couleur      pixels   remplissage
        {{100, 100, 800, 600}, 0xFFFAFAFAu, 460000, 0.96},   // fond de fenêtre
        {{300, 200, 120, 32},  0xFF3B82F6u,   3840, 0.98},    // bouton
        {{110, 110, 200, 18},  0xFF202020u,   3600, 0.72},    // ligne de texte
        {{100, 300, 700, 20},  0xFFE5E7EBu,   7600, 0.54},    // ruban
        {{620, 400, 220, 44},  0xFFFFFFFFu,   9680, 0.94},    // zone de saisie
    };

    Catalogue cat;
    const auto& avant = cat.maj(f, regions);

    // 1. la fenêtre est le plus grand rectangle
    CHECK(!avant.empty());
    CHECK(avant.front().genre == Genre::Fenetre);
    CHECK((avant.front().rect == Rect{100, 100, 800, 600}));

    // 2. les genres devinés
    const Element* bouton = chercher(avant, Genre::Bouton);
    CHECK(bouton != nullptr);
    if (bouton) CHECK((bouton->rect == Rect{300, 200, 120, 32}));
    const Element* texte = chercher(avant, Genre::Texte);
    CHECK(texte != nullptr);
    const Element* barre = chercher(avant, Genre::Barre);
    CHECK(barre != nullptr);
    // le fond de fenêtre n'est pas répété en panneau : il est déjà décrit plus haut
    CHECK(chercher(avant, Genre::Panneau) == nullptr);

    // et le classifieur lui-même, sur des formes construites
    const Rect cadre{0, 0, 800, 600};
    CHECK(deviner_genre(Rect{0, 0, 800, 600}, cadre, 0.99) == Genre::Panneau);
    CHECK(deviner_genre(Rect{10, 10, 700, 500}, cadre, 0.95) == Genre::Panneau);
    CHECK(deviner_genre(Rect{10, 10, 500, 20}, cadre, 0.90) == Genre::Texte);
    CHECK(deviner_genre(Rect{10, 40, 800, 36}, cadre, 0.90) == Genre::Barre);
    CHECK(deviner_genre(Rect{10, 100, 120, 32}, cadre, 0.95) == Genre::Bouton);
    CHECK(deviner_genre(Rect{10, 200, 300, 300}, cadre, 0.90) == Genre::Panneau);

    // 3. le texte est un rendu compact, une ligne par élément
    const std::string t = cat.texte();
    size_t lignes = 0;
    for (char c : t) if (c == '\n') ++lignes;
    CHECK(lignes == avant.size());
    CHECK(t.find("bouton") != std::string::npos);

    // 4. stabilité : la même image redonne les mêmes identifiants et les mêmes seq
    const uint64_t id_bouton = bouton ? bouton->id : 0;
    const int seq_bouton = bouton ? bouton->seq : -1;
    {
        const auto& apres = cat.maj(f, regions);
        const Element* b2 = chercher(apres, Genre::Bouton);
        CHECK(b2 != nullptr);
        if (b2) {
            CHECK(b2->id == id_bouton);
            CHECK(b2->seq == seq_bouton);
        }
        CHECK(apres.size() == avant.size());
    }

    // 5. un déplacement change l'identifiant (l'élément n'est plus le même à cet endroit)
    {
        auto deplace = regions;
        deplace[1].rect = Rect{300, 240, 120, 32};
        const auto& v = cat.maj(f, deplace);
        const Element* b3 = chercher(v, Genre::Bouton);
        CHECK(b3 != nullptr);
        if (b3) CHECK(b3->id != id_bouton);
        // l'ancien emplacement a disparu du catalogue
        CHECK(cat.par_id(id_bouton) == nullptr || cat.par_id(id_bouton)->rect.y != 200);
    }

    // 6. sous_point rend le plus petit élément qui contient le point
    {
        const auto& v = cat.maj(f, regions);
        (void)v;
        const Element* e = cat.sous_point(350, 210);   // dans le bouton et dans la fenêtre
        CHECK(e != nullptr);
        if (e) CHECK(e->genre == Genre::Bouton);
        const Element* dehors = cat.sous_point(5000, 5000);
        CHECK(dehors == nullptr);
    }

    // 7. pas de doublon : la fenêtre n'est pas répétée, et les filets de 1 px sont écartés
    {
        auto avec_filets = regions;
        avec_filets.push_back(Region{Rect{700, 200, 2, 300}, 0xFF000000u, 600, 1.0});
        const auto& v = cat.maj(f, avec_filets);
        size_t nb_fenetre = 0, nb_filet = 0;
        for (const auto& e : v) {
            if (e.genre == Genre::Fenetre) ++nb_fenetre;
            if (e.rect.w <= 4 || e.rect.h <= 4) ++nb_filet;
        }
        CHECK(nb_fenetre == 1);
        CHECK(nb_filet == 0);
    }

    // 8. oublier une fenêtre ne laisse rien derrière
    {
        cat.oublier_fenetre(f);
        CHECK(cat.tous().empty());
        CHECK(cat.sous_point(350, 210) == nullptr);
    }

    std::printf("unit_catalog : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
