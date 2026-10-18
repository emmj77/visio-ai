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

    // Ce que le lecteur a fini par choisir, pour le journal.
    const std::string& police() const noexcept { return police_choisie_; }
    int taille() const noexcept { return taille_choisie_; }

private:
    struct Table {                       // les caractères rendus d'une (police, taille)
        std::vector<Glyphe> formes;      // 95 entrées, indexées par caractère - 32
        std::vector<char>   lettres;
    };

    const Table* table_pour(size_t indice_police, int taille);

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

// Liste des polices installées, les plus probables d'abord : les polices d'interface
// courantes passent avant le reste, pour que l'identification s'arrête vite.
std::vector<std::string> polices_candidates();

} // namespace visio
