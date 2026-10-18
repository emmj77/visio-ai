// src/perceive/fonte.cpp — rendu des caractères par FreeType, puis comparaison.
#include "perceive/fonte.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>

namespace visio {

namespace {

FT_Library biblio() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        if (FT_Init_FreeType(&l) != 0) return (FT_Library) nullptr;
        return l;
    }();
    return lib;
}

std::string minuscule(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Les polices d'interface les plus courantes d'abord : l'identification s'arrête à la
// première qui colle, donc l'ordre est ce qui rend la lecture rapide.
const char* PREFEREES[] = {
    "dejavusansmono", "dejavusans", "liberationmono", "liberationsans",
    "notosans-regular", "notosansmono", "adwaitasans", "cantarell",
    "ubuntumono", "ubuntu-r", "firasans-regular", "jetbrainsmono-regular",
    "dejavuserif", "liberationserif", "bitstreamverasans", "carlito", "sourcecodepro",
};

bool est_gras_ou_italique(const std::string& nom) {
    const std::string n = minuscule(nom);
    for (const char* m : {"bold", "italic", "oblique", "black", "light", "thin", "medium",
                          "semibold", "extrabold", "condensed", "narrow", "monooblique"})
        if (n.find(m) != std::string::npos) return true;
    return false;
}

} // namespace

std::vector<std::string> polices_candidates() {
    namespace fs = std::filesystem;
    std::vector<std::string> trouvees;
    const char* dossiers[] = {"/usr/share/fonts", "/usr/local/share/fonts", "/home/moussa/.local/share/fonts",
                              "/home/moussa/.fonts"};

    for (const char* d : dossiers) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string chemin = it->path().string();
            const std::string ext = minuscule(it->path().extension().string());
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            // Gras et italique sont des typos à part entière : les exclure, c'est ne
            // jamais savoir lire un titre en gras. Ils passent après les régulières.
            trouvees.push_back(chemin);
        }
    }

    auto rang = [](const std::string& chemin) {
        const std::string n = minuscule(std::filesystem::path(chemin).stem().string());
        for (size_t i = 0; i < sizeof(PREFEREES) / sizeof(PREFEREES[0]); ++i)
            if (n.find(PREFEREES[i]) != std::string::npos)
            return est_gras_ou_italique(n) ? 200 + (int)i : (int)i;
        return est_gras_ou_italique(n) ? 1500 : 1000;
    };
    std::stable_sort(trouvees.begin(), trouvees.end(),
                     [&](const std::string& a, const std::string& b) { return rang(a) < rang(b); });
    return trouvees;
}

Fonte::~Fonte() {
    if (face_) FT_Done_Face((FT_Face)face_);
    face_ = nullptr;
}

bool Fonte::ouvrir(const std::string& chemin) {
    if (!biblio()) return false;
    FT_Face f = nullptr;
    if (FT_New_Face(biblio(), chemin.c_str(), 0, &f) != 0) return false;
    face_ = f;
    nom_ = std::filesystem::path(chemin).stem().string();
    return true;
}

bool Fonte::rendre(char c, int taille, Glyphe& sortie) {
    if (!face_) return false;
    FT_Face f = (FT_Face)face_;
    if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)taille) != 0) return false;
    if (FT_Load_Char(f, (FT_ULong)(unsigned char)c, FT_LOAD_RENDER) != 0) return false;

    const FT_Bitmap& bm = f->glyph->bitmap;
    if (bm.width == 0 || bm.rows == 0 || bm.buffer == nullptr) return false;

    // Copie en intensité, avec la même convention que la lecture d'écran : 255 = plein
    // encre.
    std::vector<uint8_t> pix((size_t)bm.width * bm.rows, 0);
    for (unsigned y = 0; y < bm.rows; ++y) {
        const uint8_t* ligne = bm.buffer + (ptrdiff_t)y * bm.pitch;
        for (unsigned x = 0; x < bm.width; ++x) {
            if (bm.pixel_mode == FT_PIXEL_MODE_MONO)
                pix[(size_t)y * bm.width + x] = (ligne[x / 8] & (0x80u >> (x % 8))) ? 255 : 0;
            else
                pix[(size_t)y * bm.width + x] = ligne[x];
        }
    }

    // Coupe à l'encre : les pixels de bord (sous 24) ne sont que du bruit de crénelage.
    int x0 = (int)bm.width, x1 = -1, y0 = (int)bm.rows, y1 = -1;
    for (unsigned y = 0; y < bm.rows; ++y)
        for (unsigned x = 0; x < bm.width; ++x)
            if (pix[(size_t)y * bm.width + x] > 24) {
                x0 = std::min(x0, (int)x); x1 = std::max(x1, (int)x);
                y0 = std::min(y0, (int)y); y1 = std::max(y1, (int)y);
            }
    if (x1 < x0 || y1 < y0) return false;

    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w > GP_W || h > GP_H) return false;

    int fmax = 1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) fmax = std::max(fmax, (int)pix[(size_t)y * bm.width + x]);

    std::memset(sortie.px, 0, GP_PIX);
    sortie.w = (uint8_t)w;
    sortie.h = (uint8_t)h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int v = pix[(size_t)(y0 + y) * bm.width + (x0 + x)];
            sortie.px[y * GP_W + x] = (uint8_t)std::min(255, (v * 255) / fmax);
        }
    return true;
}

Lecteur::Lecteur() : polices_(polices_candidates()) {}

// Deux découpes du même glyphe peuvent différer d'un pixel : la boîte de l'écran est
// prise à un seuil, celle de FreeType à un autre. On essaie donc les neuf décalages et
// on garde le meilleur, au lieu de refuser la comparaison.
float ecart_avec_decalage(const Glyphe& a, const Glyphe& b) {
    if (a.w == b.w && a.h == b.h) return ecart_glyphes(a, b);
    if (std::abs((int)a.w - (int)b.w) > 2 || std::abs((int)a.h - (int)b.h) > 2) return 1.0f;

    float meilleur = 1.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int somme = 0, n = 0;
            for (int y = 0; y < a.h; ++y)
                for (int x = 0; x < a.w; ++x) {
                    const int xb = x + dx, yb = y + dy;
                    const int vb = (xb >= 0 && xb < b.w && yb >= 0 && yb < b.h)
                                       ? b.px[yb * GP_W + xb] : 0;
                    somme += std::abs((int)a.px[y * GP_W + x] - vb);
                    ++n;
                }
            meilleur = std::min(meilleur, (float)somme / (float)(n * 255));
        }
    return meilleur;
}

const Lecteur::Table* Lecteur::table_pour(size_t indice_police, int taille) {
    if (indice_police >= polices_.size()) return nullptr;
    const uint64_t cle = ((uint64_t)indice_police << 32) | (uint32_t)taille;
    auto it = tables_.find(cle);
    if (it != tables_.end()) return &it->second;

    while (ouvertes_.size() <= indice_police) ouvertes_.push_back(std::make_unique<Fonte>());
    Fonte& f = *ouvertes_[indice_police];
    if (!f.valide() && !f.ouvrir(polices_[indice_police])) return nullptr;

    Table t;
    for (char c = 33; c < 127; ++c) {
        Glyphe g;
        if (!f.rendre(c, taille, g)) continue;
        t.formes.push_back(g);
        t.lettres.push_back(c);
    }
    if (t.formes.empty()) return nullptr;
    auto [pos, ok] = tables_.emplace(cle, std::move(t));
    return &pos->second;
}

float Lecteur::noter(const Table& t, const std::vector<Glyphe>& glyphes, size_t a_partir_de) {
    // Une police qui explique la ligne doit d'abord tomber juste sur les DIMENSIONS de
    // chaque lettre ; l'écart de pixels ne sert qu'à départager deux polices qui tombent
    // juste toutes les deux (Noto Sans et DejaVu Sans se ressemblent beaucoup).
    int vus = 0, expliquees = 0;
    float somme = 0.0f;
    for (size_t i = a_partir_de; i < glyphes.size() && vus < 10; ++i) {
        if (glyphes[i].w < 3 || glyphes[i].h < 4) continue;   // points et accents : muets
        ++vus;
        float d_min = 1.0f;
        for (const Glyphe& r : t.formes) {
            if (std::abs((int)r.w - (int)glyphes[i].w) > 1 ||
                std::abs((int)r.h - (int)glyphes[i].h) > 1) continue;
            d_min = std::min(d_min, ecart_avec_decalage(glyphes[i], r));
        }
        if (d_min < 0.25f) { ++expliquees; somme += d_min; }
    }
    if (!vus) return 1.0f;
    const float taux = (float)expliquees / (float)vus;      // 1,0 = tout est expliqué
    const float moyenne = expliquees ? somme / (float)expliquees : 1.0f;
    return 2.0f * (1.0f - taux) + moyenne;
}

namespace {

// Meilleur caractère de la table pour un glyphe : renvoie l'écart et le caractère.
float meilleur_caractere(const std::vector<Glyphe>& formes, const std::vector<char>& lettres,
                         const Glyphe& g, char* sortie) {
    float d_min = 1.0f;
    *sortie = 0;
    for (size_t k = 0; k < formes.size(); ++k) {
        const Glyphe& r = formes[k];
        const float d = ecart_avec_decalage(g, r);
        if (d < d_min) { d_min = d; *sortie = lettres[k]; }
    }
    return *sortie ? d_min : 1.0f;
}

// Colonnes [x0,x1) d'un glyphe, pour lire un segment soudé comme deux ou trois lettres.
Glyphe decouper(const Glyphe& g, int x0, int x1) {
    Glyphe r;
    r.w = (uint8_t)(x1 - x0);
    r.h = g.h;
    std::memset(r.px, 0, GP_PIX);
    for (int y = 0; y < g.h; ++y)
        for (int x = x0; x < x1; ++x) r.px[y * GP_W + (x - x0)] = g.px[y * GP_W + x];

    // Les pixels faibles à la coupure sont le pont d'antialiasing : on les éteint, sinon
    // chaque moitié garde une queue qui déforme sa forme.
    int fmax = 1;
    for (int i = 0; i < (int)r.w * (int)r.h; ++i)
        fmax = std::max(fmax, (int)r.px[(i / r.w) * GP_W + (i % r.w)]);
    const int plancher = std::max(24, (int)(0.35f * (float)fmax));
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x)
            if (r.px[y * GP_W + x] < plancher) r.px[y * GP_W + x] = 0;
    return r;
}

} // namespace

void Lecteur::identifier(const std::vector<Glyphe>& ech, int hauteur) {
    if (ech.empty()) return;

    // D'abord la taille avec la première police : la taille change peu le dessin d'une
    // police, la police change tout.
    int taille_0 = 0;
    float score_0 = 1e9f;
    for (int t = std::max(6, hauteur); t <= (int)(1.55f * (float)hauteur); ++t) {
        const Table* tab = table_pour(0, t);
        if (!tab) continue;
        const float s = noter(*tab, ech, 0);
        if (s < score_0) { score_0 = s; taille_0 = t; }
    }
    if (!taille_0) {
        taille_0 = hauteur;
        score_0 = 1e9f;
    }

    // Puis la police, à cette taille.
    size_t meilleure_police = 0;
    float meilleur_score = score_0;
    for (size_t p = 0; p < polices_.size() && p < 16; ++p) {
        const Table* tab = table_pour(p, taille_0);
        if (!tab) continue;
        const float s = noter(*tab, ech, 0);
        if (s < meilleur_score) { meilleur_score = s; meilleure_police = p; }
        if (meilleur_score < 0.06f) break;   // assez sûr : inutile d'essayer les suivantes
    }

    // Enfin la taille, pour la police retenue : un pixel d'écart fait passer un « s » pour
    // un trou.
    int taille = taille_0;
    float best = meilleur_score;
    for (int t = std::max(6, hauteur - 1); t <= (int)(1.55f * (float)hauteur) + 1; ++t) {
        const Table* tab = table_pour(meilleure_police, t);
        if (!tab) continue;
        const float s = noter(*tab, ech, 0);
        if (s < best) { best = s; taille = t; }
    }

    score_ = best;
    police_ = meilleure_police;
    police_fixee_ = true;
    trouvees_[hauteur] = {meilleure_police, taille};
    police_choisie_ = polices_[meilleure_police];
    taille_choisie_ = taille;
}

void Lecteur::calibrer(const Frame& f, const std::vector<LigneTexte>& lignes, uint32_t fond) {
    if (lignes.empty()) return;

    // La hauteur la plus représentée : c'est le corps de texte de la fenêtre.
    std::unordered_map<int, int> compte;
    for (const auto& l : lignes) ++compte[l.rect.h];
    int hauteur = 0, frequent = 0;
    for (const auto& [h, n] : compte)
        if (n > frequent) { frequent = n; hauteur = h; }

    // Les bandes les plus larges à cette hauteur : beaucoup de lettres, donc peu de hasard.
    std::vector<const LigneTexte*> choisies;
    for (const auto& l : lignes)
        if (l.rect.h == hauteur) choisies.push_back(&l);
    std::sort(choisies.begin(), choisies.end(),
              [](const LigneTexte* a, const LigneTexte* b) { return a->rect.w > b->rect.w; });

    std::vector<Glyphe> ech;
    for (size_t i = 0; i < choisies.size() && i < 3 && ech.size() < 90; ++i) {
        const auto gs = extraire_glyphes(f, *choisies[i], fond);
        ech.insert(ech.end(), gs.begin(), gs.end());
    }
    if (ech.empty()) return;
    identifier(ech, hauteur);

    // Une typo qu'on n'explique pas bien ne doit pas être imposée : mieux vaut laisser
    // chaque bande se débrouiller (plus lent, mais juste) que lire faux partout.
    if (score_ > 0.20f) police_fixee_ = false;
}

std::string Lecteur::lire(const Frame& f, const LigneTexte& ligne, uint32_t fond) {
    const auto glyphes = extraire_glyphes(f, ligne, fond);
    if (glyphes.empty()) return {};

    // 1. la typo de la fenêtre est-elle connue ? Sinon on l'apprend sur cette bande.
    const int hauteur = ligne.rect.h;
    if (!trouvees_.count(hauteur)) {
        if (police_fixee_) {
            // Police connue : il ne reste que la taille à trouver, et c'est rapide.
            int taille = hauteur;
            float meilleur = 1e9f;
            for (int t = std::max(6, hauteur - 1); t <= (int)(1.55f * (float)hauteur) + 1; ++t) {
                const Table* tab = table_pour(police_, t);
                if (!tab) continue;
                const float s = noter(*tab, glyphes, 0);
                if (s < meilleur) { meilleur = s; taille = t; }
            }
            // La typo de la fenêtre ne suffit pas toujours : un titre en gras, une bande
            // d'une autre police. Si elle n'explique pas cette hauteur, on re-cherche.
            if (meilleur > 0.15f)
                identifier(glyphes, hauteur);
            else
                trouvees_[hauteur] = {police_, taille};
        } else {
            identifier(glyphes, hauteur);
        }
    }
    auto it = trouvees_.find(hauteur);

    const Table* table = table_pour(it->second.first, it->second.second);
    if (!table) return std::string(glyphes.size(), '?');
    police_choisie_ = polices_[it->second.first];
    taille_choisie_ = it->second.second;

    // 2. lecture, glyphe par glyphe
    std::string out;
    out.reserve(glyphes.size() + 4);
    for (size_t i = 0; i < glyphes.size(); ++i) {
        char c = 0;
        const float d_un = meilleur_caractere(table->formes, table->lettres, glyphes[i], &c);

        // Une lettre… ou deux soudées par l'antialiasing ? On compare les hypothèses.
        float d_deux = 2.0f;
        char a = 0, b = 0;
        if (glyphes[i].w >= 5) {
            for (int k = 2; k + 2 <= glyphes[i].w; ++k) {
                Glyphe g1 = decouper(glyphes[i], 0, k);
                Glyphe g2 = decouper(glyphes[i], k, glyphes[i].w);
                char c1 = 0, c2 = 0;
                const float d1 = meilleur_caractere(table->formes, table->lettres, g1, &c1);
                const float d2 = meilleur_caractere(table->formes, table->lettres, g2, &c2);
                if (!c1 || !c2) continue;
                if (d1 + d2 < d_deux) { d_deux = d1 + d2; a = c1; b = c2; }
            }
            d_deux *= 0.5f;
        }

        const bool un_vaut = c && d_un <= 0.20f;
        const bool deux_vaut = a && b && d_deux <= 0.20f && d_deux < 0.85f * d_un;
        if (deux_vaut) {
            out += a;
            out += b;
        } else {
            out += un_vaut ? c : '?';
        }

        if (i + 1 < glyphes.size()) {
            const int ecart_x = glyphes[i + 1].rect.x - glyphes[i].rect.right();
            const float h = (float)glyphes[i].rect.h;
            if (ecart_x > 0.45f * h) out += ' ';
        }
    }
    return out;
}

} // namespace visio
