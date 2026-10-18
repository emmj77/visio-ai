// probe_avances : la lecture par AVANCES, sans aucune extraction de lettres.
//
// Principe : on ne decoupe plus l'image en composantes connexes. On avance dans la ligne
// avec le pas que la POLICE donne pour chaque caractere, et on compare dans la case exacte.
//   x = 0
//   tant que x < largeur :
//       pour chacun des 95 caracteres : rendre c, comparer dans [x, x+avance(c))
//       retenir le meilleur, ecrire, x += avance(c)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static FT_Library g_lib = nullptr;

struct Forme {
    char c;
    int w = 0, h = 0;
    int gauche = 0, haut = 0;   // roulement : ou le dessin commence par rapport au curseur
    int avance = 0;             // pas de la police pour ce caractere
    std::vector<float> px;      // intensites 0..1
};

// Rendu de tout le jeu de caracteres, a une taille, avec ses metriques.
static const char* JEU = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

std::vector<Forme> rendre_jeu(const char* police, int taille) {
    std::vector<Forme> out;
    FT_Face face;
    if (FT_New_Face(g_lib, police, 0, &face)) { std::printf("police introuvable\n"); return out; }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)taille);
    for (const char* p = JEU; *p; ++p) {
        if (FT_Load_Char(face, (FT_ULong)(unsigned char)*p, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot s = face->glyph;
        Forme f;
        f.c = *p;
        f.w = (int)s->bitmap.width;
        f.h = (int)s->bitmap.rows;
        f.gauche = s->bitmap_left;
        f.haut = s->bitmap_top;
        f.avance = (int)(s->advance.x >> 6);
        f.px.resize((size_t)f.w * f.h);
        for (int y = 0; y < f.h; ++y)
            for (int x = 0; x < f.w; ++x)
                f.px[(size_t)y * f.w + x] = s->bitmap.buffer[(size_t)y * s->bitmap.pitch + x] / 255.0f;
        out.push_back(std::move(f));
    }
    FT_Done_Face(face);
    return out;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: probe_avances <police.ttf> <taille> <texte>\n"); return 1; }
    const char* police = argv[1];
    const int taille = std::atoi(argv[2]);
    const std::string texte = argv[3];
    if (FT_Init_FreeType(&g_lib)) return 1;

    auto jeu = rendre_jeu(police, taille);
    std::printf("jeu : %zu caracteres a %d px\n", jeu.size(), taille);

    // 1. On FABRIQUE une ligne, comme le ferait une application : caracteres poses cote a
    //    cote selon leurs avances.
    int largeur = 0, haut_ligne = 0, base = 0;
    for (char ch : texte) {
        for (const auto& f : jeu) if (f.c == ch) { largeur += f.avance; haut_ligne = std::max(haut_ligne, f.haut); break; }
    }
    largeur += 4;
    const int hauteur = taille + 6;
    base = taille;
    std::vector<float> image((size_t)largeur * hauteur, 0.0f);
    int x = 2;
    for (char ch : texte) {
        for (const auto& f : jeu) {
            if (f.c != ch) continue;
            for (int y = 0; y < f.h; ++y)
                for (int xx = 0; xx < f.w; ++xx) {
                    const int ix = x + f.gauche + xx;
                    const int iy = base - f.haut + y;
                    if (ix < 0 || ix >= largeur || iy < 0 || iy >= hauteur) continue;
                    image[(size_t)iy * largeur + ix] += f.px[(size_t)y * f.w + xx];
                }
            x += f.avance;
            break;
        }
    }

    // 2. On RELIT cette ligne par les avances, sans jamais chercher de composantes.
    auto cout = [&](const Forme& f, int pos) {
        float c = 0.0f;
        int n = 0;
        const int larg = std::max(f.avance, 1);
        for (int y = 0; y < hauteur; ++y)
            for (int xx = 0; xx < larg; ++xx) {
                const int ix = pos + xx;
                const float reel = (ix >= 0 && ix < largeur) ? image[(size_t)y * largeur + ix] : 0.0f;
                const int gx = xx - f.gauche;
                const int gy = y - (base - f.haut);
                const float attendu = (gx >= 0 && gx < f.w && gy >= 0 && gy < f.h)
                                          ? f.px[(size_t)gy * f.w + gx] : 0.0f;
                // Normalisation par l'UNION, pas par la taille de la fenetre : sans ca, un
                // caractere etroit (i, .) gagne toujours, car il laisse moins de pixels a
                // expliquer. C'est ce qui faisait deriver la lecture des le premier mot.
                c += std::fabs(reel - attendu);
                n += (int)((reel > attendu ? reel : attendu) * 100.0f);
            }
        return n ? c / (float)n : 1e9f;
    };

    std::string lu;
    int pos = 0;
    while (pos < largeur - 2 && lu.size() < 200) {
        float meilleur = 1e9f;
        const Forme* gagnant = nullptr;
        for (const auto& f : jeu) {
            if (f.avance <= 0) continue;
            const float c = cout(f, pos);
            if (c < meilleur) { meilleur = c; gagnant = &f; }
        }
        if (!gagnant) break;
        lu += gagnant->c;
        pos += gagnant->avance;
        if (lu.size() > texte.size() + 6) break;
    }
    std::printf("ecrit : « %s »\n", texte.c_str());
    std::printf("lu    : « %s »\n", lu.c_str());
    int bons = 0;
    for (size_t i = 0; i < std::min(lu.size(), texte.size()); ++i) if (lu[i] == texte[i]) ++bons;
    std::printf("justesse : %d/%zu\n", bons, texte.size());
    FT_Done_FreeType(g_lib);
    return 0;
}
