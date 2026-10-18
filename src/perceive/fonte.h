// src/perceive/fonte.h — lire du texte en rendant les caractères avec FreeType.
//
// Le principe est celui de MOUSSA : connaître la typo et la taille, puis comparer les
// empreintes de pixels. La banque figée ne marchait qu'avec les polices et les réglages
// de l'outil qui l'avait générée (PIL). FreeType est le moteur que TOUTES les
// applications utilisent : on lui demande de rendre le caractère dans les mêmes
// conditions, et les pixels tombent juste — y compris pour une police qu'on n'avait pas
// prévue.
#pragma once
#include "common/rect.h"
#include "core/frame.h"
#include "perceive/glyph.h"
#include "perceive/text_lines.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace visio {

// Une police ouverte par FreeType, rendue à une taille en pixels.
class Fonte {
public:
    Fonte() = default;
    ~Fonte();
    Fonte(const Fonte&) = delete;
    Fonte& operator=(const Fonte&) = delete;

    bool ouvrir(const std::string& chemin);
    // Rend un caractère à `taille` pixels, coupé à son encre, intensités normalisées.
    bool rendre(char c, int taille, Glyphe& sortie);

    // Une monospace donne à « i » et à « m » la même avance. C'est une signature qui ne
    // trompe pas, et elle vaut de l'or : un terminal, un tableau, du code.
    bool monospace(int taille);

    const std::string& nom() const noexcept { return nom_; }
    bool valide() const noexcept { return face_ != nullptr; }

private:
    void*   face_ = nullptr;      // FT_Face
    void*   biblio_ = nullptr;    // FT_Library, partagée entre instances
    std::string nom_;
};

// Le lecteur : identifie (police, taille) sur une ligne, puis lit toutes les lignes avec
// cette identification — c'est le seul travail coûteux, et il n'est fait qu'une fois par
// hauteur de texte.
class Lecteur {
public:
    Lecteur();

    std::string lire(const Frame& f, const LigneTexte& ligne, uint32_t fond);

    // Apprend la typo de la fenêtre UNE fois : on échantillonne les lignes les plus
    // fournies, on identifie (police, taille), et toutes les bandes suivantes n'ont plus
    // qu'à chercher leur taille. C'est ce qui fait tomber le coût d'identification de
    // « à chaque bande » à « une fois par fenêtre ».
    void calibrer(const Frame& f, const std::vector<LigneTexte>& lignes, uint32_t fond);
    bool police_connue() const noexcept { return police_fixee_; }

    // Ce que le lecteur a fini par choisir, pour le journal.
    const std::string& police() const noexcept { return police_choisie_; }
    int taille() const noexcept { return taille_choisie_; }

private:
    struct Table {                       // les caractères rendus d'une (police, taille)
        std::vector<Glyphe> formes;      // 95 entrées, indexées par caractère - 32
        std::vector<char>   lettres;
    };

    const Table* table_pour(size_t indice_police, int taille);

    // Identifie (police, taille) sur un échantillon de lettres, et le retient.
    // `monospace` restreint la recherche aux polices à chasse fixe.
    void identifier(const std::vector<Glyphe>& echantillon, int hauteur, bool monospace = false);

    // Vrai si la police `p` est à chasse fixe (calculé une fois par police).
    bool est_monospace(size_t p);
    std::unordered_map<size_t, bool> mono_;

    size_t police_ = 0;
    bool   police_fixee_ = false;
    float  score_ = 1e9f;      // qualité de la dernière identification

    // Note le premier glyphe lisible trouvé et compare : renvoie l'écart moyen des
    // meilleures correspondances, 1,0 si rien ne colle.
    float noter(const Table& t, const std::vector<Glyphe>& glyphes, size_t a_partir_de);

    std::vector<std::string> polices_;                 // chemins de polices candidates
    std::vector<std::unique_ptr<Fonte>> ouvertes_;     // une FT_Face par police ouverte
    std::unordered_map<uint64_t, Table> tables_;       // clé : indice police, taille
    std::unordered_map<int, std::pair<size_t, int>> trouvees_;   // hauteur -> (police, taille)

    std::string police_choisie_;
    int         taille_choisie_ = 0;
};

// Une bande est monospace quand les lettres se suivent toujours du même pas. `pas` sort
// la distance entre deux lettres, en pixels.
bool bande_monospace(const std::vector<Glyphe>& glyphes, int* pas = nullptr);

// Liste des polices installées, les plus probables d'abord : les polices d'interface
// courantes passent avant le reste, pour que l'identification s'arrête vite.
std::vector<std::string> polices_candidates();

} // namespace visio
