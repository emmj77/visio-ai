// src/perceive/fonte.cpp — rendu des caractères par FreeType, puis comparaison.
#include "perceive/fonte.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

    // Le gras d'une famille doit être essayé JUSTE APRÈS son régulier, sinon il tombe
    // après quatre cents polices et n'est jamais atteint : un titre en gras ne se lirait
    // jamais. D'où le rang par famille, puis par graisse.
    auto rang = [](const std::string& chemin) {
        const std::string n = minuscule(std::filesystem::path(chemin).stem().string());
        const bool gras = est_gras_ou_italique(n);
        const bool italique = n.find("italic") != std::string::npos || n.find("oblique") != std::string::npos;
        for (size_t i = 0; i < sizeof(PREFEREES) / sizeof(PREFEREES[0]); ++i)
            if (n.find(PREFEREES[i]) != std::string::npos)
                return (int)i * 4 + (italique ? 2 : 0) + (gras ? 1 : 0);
        return gras ? 4000 : 2000;
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
    // TAUX DE RECOUVREMENT, pas une différence absolue :
    //     taux = somme des minimums / somme des maximums, pixel à pixel
    // 1,00 = même lettre au même crénelage ; 0,60 = même forme un peu décalée ou plus
    // épaisse ; 0,20 = rien à voir. C'est tolérant là où la différence absolue ne l'est
    // pas (graisse, sous-pixel), et c'est ce que demande une comparaison de forme.
    // On rend 1 - taux pour garder partout la convention « petit = meilleur ».
    if (std::abs((int)a.w - (int)b.w) > 2 || std::abs((int)a.h - (int)b.h) > 2) return 1.0f;

    float meilleur = 0.0f;   // meilleur taux
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int bas = 0, haut = 0;
            for (int y = 0; y < a.h; ++y)
                for (int x = 0; x < a.w; ++x) {
                    const int xb = x + dx, yb = y + dy;
                    const int vb = (xb >= 0 && xb < b.w && yb >= 0 && yb < b.h)
                                       ? b.px[yb * GP_W + xb] : 0;
                    const int va = a.px[y * GP_W + x];
                    bas += std::min(va, vb);
                    haut += std::max(va, vb);
                }
            if (haut > 0) meilleur = std::max(meilleur, (float)bas / (float)haut);
        }
    return 1.0f - meilleur;
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
        // 0,40 d'écart au taux = 60 % de recouvrement : le seuil de « cette police
        // explique cette lettre » avec la nouvelle mesure.
        if (d_min < 0.40f) { ++expliquees; somme += d_min; }
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

bool bande_monospace(const std::vector<Glyphe>& glyphes, int* pas) {
    if (glyphes.size() < 6) return false;
    std::unordered_map<int, int> hist;      // pas -> combien de fois
    for (size_t i = 0; i + 1 < glyphes.size(); ++i) {
        const int d = glyphes[i + 1].rect.x - glyphes[i].rect.x;
        if (d > 0 && d < 64) ++hist[d];
    }
    if (hist.empty()) return false;

    int mode = 0, vus = 0, total = 0;
    for (const auto& [d, n] : hist) {
        total += n;
        if (n > vus) { vus = n; mode = d; }
    }
    if (pas) *pas = mode;

    // Deux lettres voisines sont à un pas ; un espace fait deux pas. On compte les deux.
    int reguliers = 0, distincts = 0;
    for (const auto& [d, n] : hist) {
        if (std::abs(d - mode) <= 1 || std::abs(d - 2 * mode) <= 1) reguliers += n;
        if (n >= 2) ++distincts;
    }
    // Deux conditions, sinon le texte proportionnel passe pour du monospace : presque
    // tous les écarts au pas, et très peu de valeurs d'écart différentes.
    return reguliers * 20 >= total * 17 && distincts <= 4;
}

bool Fonte::monospace(int taille) {
    if (!face_) return false;
    FT_Face f = (FT_Face)face_;
    if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)taille) != 0) return false;

    long avance = -1;
    for (char c : {'i', 'l', 'm', 'W', 'M', '.'}) {
        if (FT_Load_Char(f, (FT_ULong)(unsigned char)c, FT_LOAD_DEFAULT) != 0) continue;
        const long a = f->glyph->advance.x >> 6;
        if (avance < 0) avance = a;
        else if (a != avance) return false;   // « i » et « m » n'ont pas la même avance
    }
    return avance > 0;
}

int Fonte::avance(int taille) {
    if (!face_) return 0;
    FT_Face f = (FT_Face)face_;
    if (FT_Set_Pixel_Sizes(f, 0, (FT_UInt)taille) != 0) return 0;
    if (FT_Load_Char(f, (FT_ULong)'i', FT_LOAD_DEFAULT) != 0) return 0;
    return (int)(f->glyph->advance.x >> 6);
}

Fonte* Lecteur::fonte_pour(size_t p) {
    if (p >= polices_.size()) return nullptr;
    while (ouvertes_.size() <= p) ouvertes_.push_back(std::make_unique<Fonte>());
    Fonte& f = *ouvertes_[p];
    if (!f.valide() && !f.ouvrir(polices_[p])) return nullptr;
    return &f;
}

bool Lecteur::est_monospace(size_t p) {
    auto it = mono_.find(p);
    if (it != mono_.end()) return it->second;
    if (p >= polices_.size()) return false;

    Fonte* f = fonte_pour(p);
    const bool r = f && f->monospace(14);
    mono_[p] = r;
    return r;
}

void Lecteur::identifier(const std::vector<Glyphe>& ech, int hauteur, bool monospace, int pas) {
    if (ech.empty()) return;

    // D'abord la taille avec la première police : la taille change peu le dessin d'une
    // police, la police change tout.
    // Pour une bande monospace, on mesure la taille avec une monospace : c'est la seule
    // façon d'être au bon pixel sur une chasse fixe.
    size_t p0 = 0;
    if (monospace) {
        while (p0 < polices_.size() && p0 < 60 && !est_monospace(p0)) ++p0;
        if (p0 >= polices_.size() || p0 >= 60) p0 = 0;
    }

    int taille_0 = 0;
    float score_0 = 1e9f;

    // Chasse fixe : le pas du texte EST l'avance de la police. Une équation, pas un
    // balayage — et la taille tombe juste au pixel près.
    if (monospace && pas > 0) {
        if (Fonte* f0 = fonte_pour(p0)) {
            int ecart_min = 1 << 30;
            for (int t = 6; t <= 40; ++t) {
                const int a = f0->avance(t);
                if (!a) continue;
                const int d = std::abs(a - pas);
                if (d < ecart_min) { ecart_min = d; taille_0 = t; }
                if (d == 0) break;
            }
            if (ecart_min > 1) taille_0 = 0;    // le pas ne vient pas de cette police
            else score_0 = 1e9f;                 // la taille est trouvée : reste la police
        }
    }

    if (!taille_0)
    for (int t = std::max(6, hauteur); t <= (int)(1.55f * (float)hauteur); ++t) {
        const Table* tab = table_pour(p0, t);
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
    for (size_t p = 0; p < polices_.size() && p < 24; ++p) {
        if (monospace && !est_monospace(p)) continue;
        const Table* tab = table_pour(p, taille_0);
        if (!tab) continue;
        const float s = noter(*tab, ech, 0);
        if (std::getenv("VISIO_DEBUG"))
            std::fprintf(stderr, "  [typ] %-28s t=%d score=%.3f\n",
                         std::filesystem::path(polices_[p]).stem().c_str(), taille_0, s);
        if (s < meilleur_score) { meilleur_score = s; meilleure_police = p; }
        if (meilleur_score < 0.02f) break;   // quasi identique : inutile d'essayer les suivantes
    }

    // Hypothèse inverse : un monospace GRAS n'a plus un pas régulier (les lettres se
    // touchent), donc il n'est pas détecté comme monospace. On essaie quand même les
    // chasses fixes avant de conclure.
    if (!monospace && meilleur_score > 0.10f) {
        for (size_t p = 0; p < polices_.size() && p < 40; ++p) {
            if (!est_monospace(p)) continue;
            const Table* tab = table_pour(p, taille_0);
            if (!tab) continue;
            const float s = noter(*tab, ech, 0);
            if (s < meilleur_score) { meilleur_score = s; meilleure_police = p; }
        }
    }

    // La chasse fixe est une hypothèse, pas une certitude : un texte gras la déclenche à
    // tort. Si aucune police à chasse fixe n'explique la bande, on recommence sans
    // l'hypothèse — plutôt que de lire faux avec la mauvaise contrainte.
    if (monospace && meilleur_score > 0.15f) {
        identifier(ech, hauteur, false, 0);
        return;
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
    trouvees_[cle_cache_ ? cle_cache_ : hauteur] = {meilleure_police, taille};
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
    int pas = 0;
    const bool mono = bande_monospace(ech, &pas);
    identifier(ech, hauteur, mono, pas);

    // Une typo qu'on n'explique pas bien ne doit pas être imposée : mieux vaut laisser
    // chaque bande se débrouiller (plus lent, mais juste) que lire faux partout.
    if (score_ > 0.20f) police_fixee_ = false;
}

std::string Lecteur::lire(const Frame& f, const LigneTexte& ligne, uint32_t fond) {
    auto glyphes = extraire_glyphes(f, ligne, fond);
    if (glyphes.empty()) return {};

    // Bande monospace : on redécoupe sur la grille du pas. Les lettres soudées par
    // l'antialiasing retrouvent alors des frontières exactes, et les espaces deviennent
    // des cellules vides — plus rien n'est deviné par la reconnaissance.
    std::vector<int> cellules;
    int pas_bande = 0;
    const bool mono_bande = bande_monospace(glyphes, &pas_bande);

    // 1. La typo de cette hauteur est-elle déjà connue ET explique-t-elle cette bande ?
    //    La même hauteur peut porter deux textes différents (un titre en gras de 17 px et
    //    du texte courant de 17 px) : on vérifie avant de faire confiance au cache.
    // Clé du cache : la hauteur ET la densité d'encre. Un titre en gras de 17 px et du
    // texte courant de 17 px ont la même hauteur mais pas la même graisse : sans la
    // densité, le second hériterait de la typo du premier et se lirait faux.
    int encre = 0, cases = 0;
    for (const auto& g : glyphes)
        for (int i = 0; i < (int)g.w * (int)g.h; ++i) {
            const int x = i % g.w, y = i / g.w;
            if (g.px[y * GP_W + x] > 128) ++encre;
            ++cases;
        }
    const float densite = cases ? (float)encre / (float)cases : 0.0f;
    const int hauteur = ligne.rect.h;
    const int cle = hauteur * 1000 + (int)(densite * 100.0f);
    bool sur = false;
    if (auto it = trouvees_.find(cle); it != trouvees_.end()) {
        const Table* tab = table_pour(it->second.first, it->second.second);
        sur = tab && noter(*tab, glyphes, 0) <= 0.10f;
    }
    cle_cache_ = cle;   // `identifier` inscrira son résultat sous cette clé
    if (!sur) {
        if (police_fixee_) {
            int taille = hauteur;
            float meilleur = 1e9f;
            for (int t = std::max(6, hauteur - 1); t <= (int)(1.55f * (float)hauteur) + 1; ++t) {
                const Table* tab = table_pour(police_, t);
                if (!tab) continue;
                const float s = noter(*tab, glyphes, 0);
                if (s < meilleur) { meilleur = s; taille = t; }
            }
            if (meilleur > 0.15f) {
                int pas = 0;
                const bool mono = bande_monospace(glyphes, &pas);
                identifier(glyphes, hauteur, mono, pas);
            } else {
                trouvees_[cle] = {police_, taille};
            }
        } else {
            int pas = 0;
            const bool mono = bande_monospace(glyphes, &pas);
            identifier(glyphes, hauteur, mono, pas);
        }
    }
    auto it = trouvees_.find(cle);
    if (it == trouvees_.end()) return std::string(glyphes.size(), '?');

    const Table* table = table_pour(it->second.first, it->second.second);
    if (!table) return std::string(glyphes.size(), '?');
    police_choisie_ = polices_[it->second.first];
    taille_choisie_ = it->second.second;

    // 2. Redécoupe sur la grille du pas, MAINTENANT que la typo est connue.
    //    L'identification se fait sur les composantes connexes — une grille mal calée
    //    fausserait les dimensions et donc le choix de la police. La grille, elle, sert à
    //    découper : les frontières sont exactes, les lettres soudées retrouvent leur
    //    place, et les espaces deviennent des cellules vides.
    if (mono_bande && pas_bande >= 3 && pas_bande <= 48) {
        int au_pas = 0, ecarts = 0;
        for (size_t i = 0; i + 1 < glyphes.size(); ++i) {
            const int d = glyphes[i + 1].rect.x - glyphes[i].rect.x;
            if (d > 0 && d < 64) { ++ecarts; if (d == pas_bande) ++au_pas; }
        }
        // 95 % au même pas : sans ça la grille dérive d'un pixel et coupe les lettres.
        if (ecarts && au_pas * 20 >= ecarts * 19) {
            auto g = extraire_glyphes_pas(f, ligne, fond, pas_bande, &cellules);
            if (g.size() >= glyphes.size()) glyphes = std::move(g);
            else cellules.clear();
        }
    }

    // 3. lecture, glyphe par glyphe
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

        // 0,30 plutôt que 0,20 : au-delà, mieux vaut rendre la lettre la plus proche que
        // perdre l'information. Un « ? » n'apprend rien à qui lit.
        const bool un_vaut = c && d_un <= 0.45f;
        const bool grille = !cellules.empty() && cellules.size() == glyphes.size();
        // Deux lettres soudées : si une seule n'explique rien (d_un > 0,30), l'hypothèse
        // « deux » est la bonne même si elle n'écrase pas l'autre — il n'y a pas d'autre.
        // Sur une grille monospace, une cellule EST une lettre : l'hypothèse « deux
        // lettres » n'a plus lieu d'être, et c'est elle qui fabriquait les « c: ».
        const bool deux_vaut = !grille && a && b && d_deux <= 0.35f &&
                               (d_deux < 0.85f * d_un || d_un > 0.30f);
        if (deux_vaut) {
            out += a;
            out += b;
        } else {
            out += un_vaut ? c : '?';
        }

        if (i + 1 < glyphes.size()) {
            if (grille) {
                const int saut = cellules[i + 1] - cellules[i];
                if (saut > 1) out.append((size_t)(saut - 1), ' ');
            } else {
                const int ecart_x = glyphes[i + 1].rect.x - glyphes[i].rect.right();
                const float h = (float)glyphes[i].rect.h;
                if (ecart_x > 0.45f * h) out += ' ';
            }
        }
    }
    return out;
}

} // namespace visio
