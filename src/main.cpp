// src/main.cpp — point d'entrée.
//
//   visio                          ce que l'écran est, et les fenêtres clientes
//   visio --regions x,y,w,h        segmente une région, et dit ce qu'elle contient
//   visio --win <morceau de titre> segmente la fenêtre dont le titre contient ce morceau
#include "capture/screen_info.h"
#include "capture/shm_capture.h"
#include "common/log.h"
#include "gestes/gestes.h"
#include "core/frame.h"
#include "perceive/catalog.h"
#include "perceive/fonte.h"
#include "perceive/regions.h"
#include "perceive/text_lines.h"
#include "perceive/windows.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cmath>
#include <map>
#include <fstream>
#include <filesystem>
#include <tuple>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool g_lire = false;       // --lire : reconnaître les caractères par leur forme
bool g_etiquettes = false; // --etiquettes : nommer chaque élément par son texte
bool g_cibles = false;     // --cibles : les cibles cliquables, un mot = une cible
std::string g_clic;        // --clic <texte> : cliquer la cible dont l'etiquette contient ce texte
std::string g_taper;       // --taper <texte> : ecrire au clavier, caractere par caractere
bool g_delta = false;      // --delta : ne rendre que ce qui a change depuis le dernier scan
bool g_formes = false;     // --formes : les cibles NON textuelles (icones, cases, curseurs)
std::string g_dump;        // --dump "x,y,w,h fichier.ppm" : sortir les pixels d'une zone
std::string g_gestes;      // --gestes "x,y,texte" : un geste constate, pour les tests
std::string g_motif;       // --win <morceau de titre> : la fenetre visee par un geste
bool g_ascii = false;      // --ascii : un schema ASCII de la page, pour l'agent
bool g_boites = false;     // --boites : les cadres fermes (boutons, cartes, panneaux)
int  g_cote_min = 10;      // plus petit cote d'un cadre accepte
int  g_pas = 10;           // taille d'une case du plan ASCII, en pixels (1 caractere = 10 px)
int  g_aire_min = 150;     // aire minimale de la boite (pas du trait) : 150 px = une case 16x16

// LE CONTOUR D'UNE FORME, PUIS SON TRACE EN CARACTERES.
// Pourquoi vectoriser au lieu de lire la densite case par case : a 10x20 px par case, un rond de
// 122 px ne fait que 12x6 caracteres, et un choix par case ne sait pas qu'un caractere doit
// toucher le suivant. Un dessinateur ASCII suit le CONTOUR et enchaine des caracteres qui se
// raccordent (Rowan Crawford : « every line must connect up, no large gaps »). On fait pareil :
// contour radial, puis polyligne en coordonnees de cases, avec la regle de HAUTEUR de l'ASCII
// art pour les lignes horizontales — « _ » quand la ligne est en bas de la case, « - » au milieu,
// « ' » et « , » aux extremites (alt.ascii-art : high ` ' ", middle - =, low . , bottom _).
static std::vector<std::pair<double, double>> contour_radial(const visio::Frame& f, visio::Rect r,
                                                             uint32_t fond, int pas_deg = 3) {
    const int fr = (int)((fond >> 16) & 0xFF), fg = (int)((fond >> 8) & 0xFF),
              fb = (int)(fond & 0xFF);
    auto encre = [&](int x, int y) {
        const uint32_t c = f.px[(size_t)(y - f.region.y) * f.region.w + (size_t)(x - f.region.x)];
        return std::abs((int)((c >> 16) & 0xFF) - fr) > 16 ||
               std::abs((int)((c >> 8) & 0xFF) - fg) > 16 ||
               std::abs((int)(c & 0xFF) - fb) > 16;
    };
    double sx = 0, sy = 0;
    long n = 0;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            if (encre(x, y)) { sx += x; sy += y; ++n; }
    if (n < 20) return {};
    const double cx = sx / n, cy = sy / n;
    std::vector<std::pair<double, double>> pts;
    for (int a = 0; a < 360; a += pas_deg) {
        const double t = (double)a * 3.14159265358979 / 180.0;
        const double dx = std::cos(t), dy = std::sin(t);
        const double rmax = std::sqrt((double)(r.w * r.w + r.h * r.h));
        // Le MILIEU de la derniere plage d'encre, pas son dernier pixel : le trait fait 2 a 4 px,
        // prendre le bord exterieur fait vibrer le rayon d'un pixel et le contour devient
        // hache. On note les plages, puis on garde le milieu de la derniere.
        double debut_plage = -1, fin_plage = -1;
        double dernier_debut = -1, dernier_fin = -1;
        for (double d = 1.0; d < rmax; d += 0.5) {
            const int x = (int)std::lround(cx + dx * d), y = (int)std::lround(cy + dy * d);
            if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) break;
            if (encre(x, y)) {
                if (debut_plage < 0) debut_plage = d;
                fin_plage = d;
            } else if (debut_plage >= 0) {
                if (fin_plage - debut_plage >= 0.5) { dernier_debut = debut_plage; dernier_fin = fin_plage; }
                debut_plage = -1;
            }
        }
        if (debut_plage >= 0 && fin_plage - debut_plage >= 0.5) {
            dernier_debut = debut_plage; dernier_fin = fin_plage;
        }
        if (dernier_debut > 0) {
            const double dmid = (dernier_debut + dernier_fin) / 2.0;
            pts.emplace_back(cx + dx * dmid, cy + dy * dmid);
        }
    }
    // LISSAGE circulaire du rayon : le contour d'un rond est une suite de rayons presque egaux,
    // et 3 px de bruit suffisent a faire zigzaguer la polyligne en coordonnees de cases (une case
    // fait 10 px de large). On moyenne chaque rayon avec ses voisins.
    if (pts.size() >= 9) {
        std::vector<std::pair<double, double>> lis = pts;
        const int w = 2;
        for (size_t i = 0; i < pts.size(); ++i) {
            double sx = 0, sy = 0;
            int k = 0;
            for (int o = -w; o <= w; ++o) {
                const int j = ((int)i + o + (int)pts.size()) % (int)pts.size();
                sx += pts[(size_t)j].first;
                sy += pts[(size_t)j].second;
                ++k;
            }
            lis[i] = {sx / k, sy / k};
        }
        pts.swap(lis);
    }
    return pts;
}

// DESSINER UN CONTOUR COMME LE FERAIT UN DESSINATEUR : un caractere par ligne et par cote.
//
// Regles prises dans la doc ASCII art, pas inventees :
//  - « every line must connect up » (Rowan Crawford) : un cote ne peut bouger que d'UNE case
//    d'une ligne a l'autre. Sans cette contrainte, un bord a 51 degres saute deux cases et le
//    trait se troue ; c'est ce qui donnait des points inutiles et des cotes mal alignes.
//  - la hauteur du caractere dans la case porte le sens : « _ » est un trait BAS dans la case,
//    « - » un trait au milieu, « | » un trait vertical. On choisit donc d'apres la PENTE
//    LOCALE du contour, mesuree sur ses voisins — pas d'apres un seuil de largeur.
// Deux passes : la premiere mesure, pour chaque rangee de cases, le bord gauche, le bord droit
// et la pente du contour a chacun ; la seconde pose les caracteres.
static void dessiner_contour(std::vector<std::string>& gr,
                             const std::vector<std::pair<double, double>>& pts, visio::Rect fen,
                             int cellx, int celly, int cols, int rows) {
    if (pts.size() < 12) return;
    double miny = 1e18, maxy = -1e18;
    for (const auto& p : pts) { miny = std::min(miny, p.second); maxy = std::max(maxy, p.second); }
    const int ry0 = std::max(0, (int)std::floor((miny - fen.y) / celly));
    const int ry1 = std::min(rows - 1, (int)std::floor((maxy - fen.y) / celly));
    if (ry1 - ry0 < 1) return;

    // Pente locale du contour au point d'indice i : |dy| / |dx| sur ses voisins (9 degres de
    // chaque cote a l'echantillonnage de contour_radial). 0 = trait horizontal, grand = vertical.
    const size_t n = pts.size();
    auto pente = [&](size_t i) {
        const auto& a = pts[(i + n - 3) % n];
        const auto& b = pts[(i + 3) % n];
        const double dx = std::abs(b.first - a.first), dy = std::abs(b.second - a.second);
        return dy / std::max(1e-9, dx);
    };

    struct Rangee { int cy; double xg, xd, pg, pd; };
    std::vector<Rangee> rr;
    for (int cy = ry0; cy <= ry1; ++cy) {
        const double yb0 = fen.y + (double)cy * celly, yb1 = yb0 + celly;
        double xmin = 1e18, xmax = -1e18;
        size_t imin = 0, imax = 0;
        int dedans = 0;
        for (size_t k = 0; k < n; ++k) {
            if (pts[k].second >= yb0 && pts[k].second < yb1) {
                if (pts[k].first < xmin) { xmin = pts[k].first; imin = k; }
                if (pts[k].first > xmax) { xmax = pts[k].first; imax = k; }
                ++dedans;
            }
        }
        if (dedans < 2) {
            const double yc = (yb0 + yb1) / 2.0;
            double a1 = 1e18, a2 = -1e18;
            size_t i1 = 0, i2 = 0;
            for (size_t k = 0; k < n; ++k) {
                const auto& a = pts[k];
                const auto& b = pts[(k + 1) % n];
                if ((a.second <= yc && b.second > yc) || (b.second <= yc && a.second > yc)) {
                    const double u = (yc - a.second) / (b.second - a.second);
                    const double x = a.first + u * (b.first - a.first);
                    if (x < a1) { a1 = x; i1 = k; }
                    if (x > a2) { a2 = x; i2 = k; }
                }
            }
            if (a1 > a2) continue;
            xmin = a1; xmax = a2; imin = i1; imax = i2;
        }
        rr.push_back({cy, (xmin - fen.x) / cellx, (xmax - fen.x) / cellx, pente(imin), pente(imax)});
    }
    // Une LAMELLE en haut ou en bas (une rangee coupee par la frontiere des cases, large de
    // rien) n'est pas une rangee de la forme : elle ferait une barre trop large puis des cotes
    // verticaux. On la fond dans sa voisine.
    while (rr.size() > 3 && (rr[0].xd - rr[0].xg) * 2.0 < (rr[1].xd - rr[1].xg)) rr.erase(rr.begin());
    while (rr.size() > 3 && (rr[rr.size() - 1].xd - rr[rr.size() - 1].xg) * 2.0 <
                                (rr[rr.size() - 2].xd - rr[rr.size() - 2].xg))
        rr.pop_back();
    if (rr.size() < 3) return;

    auto poser = [&](int x, int y, char c) {
        if (x < 0 || y < 0 || x >= cols || y >= rows) return;
        const char vieux = gr[(size_t)y][(size_t)x];
        if (vieux == ' ' || vieux == '.' || vieux == ':' || vieux == '*' || vieux == '#')
            gr[(size_t)y][(size_t)x] = c;
    };

    double wmax = 1e-9;
    for (const auto& q : rr) wmax = std::max(wmax, q.xd - q.xg);
    int cg_prec = -1000, cd_prec = -1000;
    double xg_prec = 0, xd_prec = 0;
    for (size_t i = 0; i < rr.size(); ++i) {
        const int cy = rr[i].cy;
        // FLOOR et non arrondi : la case qui CONTIENT le bord. Arrondir decalait les deux cotes
        // d'une demi-case dans des sens differents (16,92 -> 17 mais 8,00 -> 8) et le rond
        // sortait asymetrique — un cote en « / », l'autre en « | ».
        int cg = (int)std::floor(rr[i].xg), cd = (int)std::floor(rr[i].xd);
        // « every line must connect up » : un cote ne se deplace que d'une case par rangee.
        if (cg_prec > -1000) {
            cg = std::max(cg_prec - 1, std::min(cg_prec + 1, cg));
            cd = std::max(cd_prec - 1, std::min(cd_prec + 1, cd));
        }
        // Un CHAPEAU plat (rond, bouton arrondi) occupe une bonne part de la largeur maximale
        // des sa premiere ou sa deuxieme rangee ; une POINTE de triangle n'en occupe qu'un
        // cinquieme. On regarde les deux rangees du chapeau, car le decoupage en cases coupe
        // parfois le sommet en deux (une lamelle de rien, puis la vraie rangee).
        const bool bout = (i == 0 || i + 1 == rr.size());
        // La largeur de SA PROPRE rangee decide, pas celle de la voisine : regarder la voisine
        // faisait passer la pointe du triangle (etroite) pour un chapeau, parce que la rangee
        // suivante est deja large. Mesure ici : rond 0,74 de la largeur max sur sa premiere
        // rangee, triangle 0,33. Seuil a 0,50.
        const bool plat = bout && (rr[i].xd - rr[i].xg) >= 0.50 * wmax;
        if (cd < cg) std::swap(cg, cd);
        if (std::getenv("VISIO_DEBUG_FORME"))
            std::fprintf(stderr, "FORME rangee %2d  x %6.2f..%6.2f  -> cg %3d cd %3d (prec %3d,%3d) plat %d\n",
                         cy, rr[i].xg, rr[i].xd, cg, cd, cg_prec, cd_prec, (int)plat);

        if (plat) {
            // La barre garde SA largeur : elargie jusqu'a la rangee voisine, elle collait un
            // « | » juste dessous et le rond ressemblait a une boite.
            for (int x = cg; x <= cd; ++x) poser(x, cy, '_');
            poser(cg, cy, ',');
            poser(cd, cy, '.');
        } else {
            // Le CARACTERE suit le deplacement REEL du bord (en cases, fractionnaire), pas le
            // saut entier : entre deux rangees, un bord qui glisse de 0,4 case n'a pas change de
            // case mais il PENDU — c'est un « / », pas un « | ». Lire le saut entier donnait un
            // cote en « / » et l'autre en « | » sur le meme rond.
            // Pour la PREMIERE rangee, pas de precedente : on lit la suivante. Sans cela la
            // pointe du triangle sortait en « |  | », deux barres verticales.
            const double dgf = (cg_prec > -1000) ? rr[i].xg - xg_prec : rr[1].xg - rr[0].xg;
            const double ddf = (cd_prec > -1000) ? rr[i].xd - xd_prec : rr[1].xd - rr[0].xd;
            const int dg = std::abs(dgf) < 0.35 ? 0 : (dgf > 0 ? 1 : -1);
            const int dd = std::abs(ddf) < 0.35 ? 0 : (ddf > 0 ? 1 : -1);
            if (cd == cg) {
                // Une pointe tient dans une case : on l'ecrit « /\\ » sur deux, sans trou.
                poser(cg, cy, '/');
                poser(cg + 1, cy, '\\');
                cd = cg + 1;
            } else {
                poser(cg, cy, dg > 0 ? '\\' : (dg < 0 ? '/' : '|'));
                poser(cd, cy, dd > 0 ? '\\' : (dd < 0 ? '/' : '|'));
            }
        }
        cg_prec = cg; cd_prec = cd;
        xg_prec = rr[i].xg; xd_prec = rr[i].xd;
    }
}

int mode_regions(Display* d, visio::Rect zone, const char* quoi, Window fenetre) {
    visio::ShmCapture cap(d);
    if (!cap.valide()) { VISIO_ERROR("capture SHM indisponible"); return 2; }

    visio::Frame f;
    auto t0 = std::chrono::steady_clock::now();
    if (!cap.get(zone, f)) { VISIO_ERROR("capture impossible de %s", quoi); return 3; }
    const double t_cap = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();

    // deuxième capture : le tampon partagé existe déjà, c'est le vrai coût de régime
    {
        visio::Frame encore;
        const auto t1 = std::chrono::steady_clock::now();
        const bool ok2 = cap.get(zone, encore);
        const double t2 = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t1).count();
        if (ok2) std::printf("capture 2e   %.2f ms (tampon deja alloue)\n", t2);
    }

    t0 = std::chrono::steady_clock::now();
    const auto regions = visio::regions_uniformes(f, 5, 64, 0.55);
    const double t_seg = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();

    std::printf("zone         %s %dx%d en %d,%d\n", quoi, zone.w, zone.h, zone.x, zone.y);
    std::printf("capture      %.2f ms   segmentation %.2f ms   %zu regions retenues\n",
                t_cap, t_seg, regions.size());

    // Les CADRES : une bordure continue qui se referme. Une bordure de 1 px sur une boite
    // 500x400 occupe 9 % de son aire, donc `regions_uniformes` la jette : c'est pour cela
    // que le plan ASCII ne montrait aucun bouton. On les demande separement.
    t0 = std::chrono::steady_clock::now();
    std::vector<visio::Region> boites =
        visio::boites_fermees(f, 8, g_cote_min, g_aire_min, 0.65);
    const double t_boites = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0).count();
    // Les APLATS BORDES : un bouton a fond teinte est une region pleine, pas un cadre. Si
    // son interieur est UNI et que son bord porte une autre teinte, la bordure est reelle.
    // Mais l'interieur d'un cadre est lui aussi un aplat borde (sa bordure est celle du
    // cadre) : on l'ecarte par inclusion, sinon chaque boite sort deux fois.
    auto dedans = [](const visio::Rect& petit, const visio::Rect& grand) {
        return petit.x >= grand.x - 1 && petit.y >= grand.y - 1 &&
               petit.x + petit.w <= grand.x + grand.w + 1 &&
               petit.y + petit.h <= grand.y + grand.h + 1;
    };
    t0 = std::chrono::steady_clock::now();
    for (auto& b : visio::aplats_bordes(f, regions)) {
        bool deja = false;
        for (const auto& r : boites)
            if (dedans(b.rect, r.rect)) { deja = true; break; }
        if (!deja) boites.push_back(b);
    }
    std::sort(boites.begin(), boites.end(), [](const visio::Region& p, const visio::Region& q) {
        return p.rect.area() > q.rect.area();
    });
    const double t_bordes = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0).count();
    std::printf("boites       %zu cadres fermes en %.2f ms (dont aplats bordes %.2f ms)\n",
                boites.size(), t_boites, t_bordes);
    if (g_boites) {
        for (const auto& b : boites)
            std::printf("  boite %4dx%-4d @ %4d,%-4d trait #%06x remplissage %.2f bord %.2f %6ld px\n",
                        b.rect.w, b.rect.h, b.rect.x, b.rect.y, b.couleur & 0xFFFFFF,
                        b.remplissage, b.sur_bord, (long)b.pixels);
    }

    const size_t max = regions.size() < 20 ? regions.size() : 20;
    for (size_t i = 0; i < max; ++i) {
        const auto& r = regions[i];
        std::printf("  %2zu  %4dx%-4d @ %4d,%-4d  couleur #%06x  remplissage %.2f  %6ld px\n",
                    i + 1, r.rect.w, r.rect.h, r.rect.x, r.rect.y, r.couleur & 0xFFFFFF,
                    r.remplissage, (long)r.pixels);
    }

    // Le catalogue recoit les DEUX lectures : les aplats et les cadres. Sans les cadres, un
    // bouton se reduit a son interieur et le plan ASCII ne montre pas la boite. Dedoublonnage
    // sur le rectangle exact (un cadre et son aplat interieur n'ont jamais le meme rect).
    std::vector<visio::Region> toutes = regions;
    for (const auto& b : boites) {
        bool deja = false;
        for (const auto& r : regions)
            if (r.rect == b.rect) { deja = true; break; }
        if (!deja) toutes.push_back(b);
    }

    // Le catalogue : ce que le modèle lira. Identifiants stables, genres devinés.
    t0 = std::chrono::steady_clock::now();
    visio::Catalogue cat;
    const auto& elements = cat.maj(fenetre, toutes);
    const double t_cat = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
    std::printf("\ncatalogue    %zu elements en %.2f ms (%zu glyphes ecartes)\n",
                elements.size(), t_cat, cat.glyphes_ecartes);
    const std::string texte = cat.texte();
    size_t debut = 0, n = 0;
    while (debut < texte.size() && n < 20) {
        const size_t fin = texte.find('\n', debut);
        std::printf("  %s\n", texte.substr(debut, fin - debut).c_str());
        debut = fin + 1;
        ++n;
    }

    // --etiquettes : le pipeline complet, en passes successives.
    //   1. l'ecran est lu et decoupe en elements (catalogue) ;
    //   2. chaque element est recadre, et son texte lu par empreintes de formes (CPU) ;
    //   3. l'element sort avec son etiquette : c'est ce que l'agent recevra, avec un id.
    if (g_etiquettes) {
        const auto t3 = std::chrono::steady_clock::now();
        size_t nommes = 0;
        double pire = 0.0;
        visio::Lecteur lecteur;
        std::printf("\netiquettes   (recadrage + lecture, element par element)\n");
        for (const auto& e : elements) {
            const visio::Rect r = visio::intersect(e.rect, f.region);
            if (r.empty() || r.w < 20 || r.h < 8) continue;
            visio::Frame g = visio::Frame::pour(r);
            for (int y = 0; y < r.h; ++y)
                std::memcpy(g.px.data() + (size_t)y * r.w,
                            f.px.data() + (size_t)(y + r.y - f.region.y) * f.region.w +
                                (r.x - f.region.x),
                            (size_t)r.w * 4);
            const uint32_t fg_ = visio::couleur_fond(g, 3);
            const auto ls = visio::lignes_de_texte(g, fg_);
            const auto t4 = std::chrono::steady_clock::now();
            std::string txt;
            for (const auto& l : ls) {
                const std::string t = lecteur.lire(g, l, fg_);
                if (!t.empty()) { txt += t; txt += ' '; }
            }
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t4).count();
            pire = std::max(pire, ms);
            if (txt.empty()) continue;
            std::printf("  %4dx%-4d @%4d,%-4d  %6.2f ms  « %s »\n", r.w, r.h, r.x, r.y, ms,
                        txt.substr(0, 46).c_str());
            ++nommes;
        }
        std::printf("etiquettes   %zu nommees, pire cas %.2f ms, total %.1f ms\n", nommes, pire,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t3).count());
    }

    // Les lignes de texte : ce que le catalogue écarte, la couche texte le regroupe.
    const uint32_t fond = visio::couleur_fond(f, 3);
    t0 = std::chrono::steady_clock::now();
    const auto lignes = visio::lignes_de_texte(f, fond);
    const double t_txt = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
    std::printf("\ntexte        %zu lignes en %.2f ms (fond #%06x)\n", lignes.size(), t_txt,
                fond & 0xFFFFFF);

    visio::Lecteur lecteur;   // rend les caractères avec FreeType, comme les applications


    // --dump "x,y,w,h fichier.ppm" : sortir les pixels bruts d'une zone. Sert a confronter
    // une cible reelle a une banque d'images (les icones du systeme, par exemple).
    if (!g_dump.empty()) {
        int dx = 0, dy = 0, dw = 0, dh = 0;
        char fic[512] = {0};
        if (std::sscanf(g_dump.c_str(), "%d,%d,%d,%d %511s", &dx, &dy, &dw, &dh, fic) == 5) {
            std::ofstream out(fic, std::ios::binary);
            out << "P6\n" << dw << " " << dh << "\n255\n";
            size_t ecrits = 0;
            for (int yy = dy; yy < dy + dh; ++yy) {
                const int ly = yy - f.region.y;
                if (ly < 0 || ly >= f.region.h) continue;
                const uint32_t* row = f.px.data() + (size_t)ly * f.region.w;
                for (int xx = dx; xx < dx + dw; ++xx) {
                    const int lx = xx - f.region.x;
                    if (lx < 0 || lx >= f.region.w) continue;
                    const uint32_t p = row[lx];
                    const char rgb[3] = {(char)((p >> 16) & 0xFF), (char)((p >> 8) & 0xFF),
                                         (char)(p & 0xFF)};
                    out.write(rgb, 3);
                    ++ecrits;
                }
            }
            std::printf("dump         %zu pixels -> %s\n", ecrits, fic);
        } else {
            std::printf("dump         format attendu : \"x,y,w,h fichier.ppm\"\n");
        }
    }


    // --ascii : LA PAGE EN PLAN. Deux couches. D'abord l'ENCRE, case par case : c'est ce
    // qui est DESSINE, et c'est la seule couche qui montre qu'un rond est rond (un cadre
    // rectangulaire ne le dit pas). Puis la STRUCTURE : les zones du catalogue avec leur
    // cadre et leur nom, la plus grande d'abord, les plus petites par-dessus.
    if (g_ascii) {
        // L'ECHELLE. Un caractere de terminal est DEUX fois plus haut que large : avec des
        // cases carrees en pixels (10x10), la page sortait etiree en hauteur et un rond
        // ressemblait a un oeuf. Une case doit valoir UN caractere a l'ecran, donc 2 fois
        // plus haute que large : 10 x 20 px. Le plan garde alors les proportions de l'ecran.
        const int cellx = g_pas, celly = g_pas * 2;
        const int cols = std::max(1, f.region.w / cellx);
        const int rows = std::max(1, f.region.h / celly);
        // Les cases couvertes par une LIGNE DE TEXTE : le texte s'y dessine en densite, jamais
        // en orientation. Sans cela les futs des glyphes sortaient en « | » et « - », et une
        // ligne de texte devenait un hachis de tirets au lieu de « ::: ».
        std::vector<uint8_t> dans_texte((size_t)cols * (size_t)rows, 0);
        for (const auto& l : lignes) {
            const int ax = std::max(0, (l.rect.x - f.region.x) / cellx);
            const int bx = std::min(cols - 1, (l.rect.x + l.rect.w - f.region.x) / cellx);
            const int ay = std::max(0, (l.rect.y - f.region.y) / celly);
            const int by = std::min(rows - 1, (l.rect.y + l.rect.h - f.region.y) / celly);
            for (int y = ay; y <= by; ++y)
                for (int x = ax; x <= bx; ++x) dans_texte[(size_t)y * (size_t)cols + (size_t)x] = 1;
        }
        std::vector<std::string> gr((size_t)rows, std::string((size_t)cols, ' '));
        auto nom = [](visio::Genre x) -> const char* {
            switch (x) {
                case visio::Genre::Fenetre: return "fenetre";
                case visio::Genre::Panneau: return "panneau";
                case visio::Genre::Barre:   return "barre";
                case visio::Genre::Bouton:  return "bouton";
                case visio::Genre::Champ:   return "champ";
                case visio::Genre::Texte:   return "texte";
                case visio::Genre::Cadre:   return "cadre";
                default: return "zone";
            }
        };

        // 1. LA COUCHE D'ENCRE. Deux mesures par case : la DENSITE (aplats, texte) et le
        //    SQUELETTE (traits). Un trait fin recoit un caractere d'ORIENTATION — « - »,
        //    « | », « / », « \ » — donc un trait continu au lieu d'un chapelet de points ;
        //    une zone epaisse garde sa densite.
        std::vector<int64_t> ncar((size_t)cols * (size_t)rows, 0);
        {
            const int fr = (int)((fond >> 16) & 0xFF), fg = (int)((fond >> 8) & 0xFF),
                      fb = (int)(fond & 0xFF);
            // Moments de l'ENCRE dans chaque case, en pixels depuis le coin de la case :
            // l'ecart-type perpendiculaire a l'axe principal dit si la case est traversee par
            // un TRAIT (encre serree, 1-4 px) ou par du TEXTE (encre etalee sur la case).
            std::vector<double> sx((size_t)cols * (size_t)rows, 0), sy = sx, sxx = sx,
                                syy = sx, sxy = sx;
            for (int y = 0; y < f.region.h; ++y) {
                const uint32_t* p = f.px.data() + (size_t)y * f.region.w;
                for (int x = 0; x < f.region.w; ++x) {
                    const int dr = std::abs((int)((p[x] >> 16) & 0xFF) - fr);
                    const int dg = std::abs((int)((p[x] >> 8) & 0xFF) - fg);
                    const int db = std::abs((int)(p[x] & 0xFF) - fb);
                    if (dr <= 16 && dg <= 16 && db <= 16) continue;
                    const size_t k = (size_t)(y / celly) * (size_t)cols + (size_t)(x / cellx);
                    const double u = (double)(x % cellx), v = (double)(y % celly);
                    ++ncar[k];
                    sx[k] += u; sy[k] += v;
                    sxx[k] += u * u; syy[k] += v * v; sxy[k] += u * v;
                }
            }
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx) {
                    const size_t k = (size_t)cy * (size_t)cols + (size_t)cx;
                    const double n = (double)ncar[k];
                    const double d = n / (double)(cellx * celly);
                    char c = ' ';
                    if (n >= 4 && !dans_texte[k]) {
                        const double ux = sx[k] / n, uy = sy[k] / n;
                        const double vx = sxx[k] / n - ux * ux, vy = syy[k] / n - uy * uy;
                        const double vxy = sxy[k] / n - ux * uy;
                        const double tr = vx + vy, det = vx * vy - vxy * vxy;
                        const double disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
                        const double l2 = tr / 2 - disc;            // variance perpendiculaire
                        if (l2 <= 12.0) {                           // sigma_perp <= ~3,5 px : un trait
                            const double th = 0.5 * std::atan2(2 * vxy, vx - vy);
                            // angle VU a l'ecran : la case fait cellx de large et celly de haut
                            double a = std::atan2(std::sin(th) / celly, std::cos(th) / cellx) *
                                       57.29577951308232;
                            if (a > 90) a -= 180;
                            if (a < -90) a += 180;
                            const double aa = std::abs(a);
                            if (aa <= 22.5) {
                                // REGLE DE HAUTEUR : « _ » est en bas de la case, « - » au
                                // milieu. Une ligne horizontale basse s'ecrit « _ », sinon les
                                // lignes de deux cases voisines ne se raccordent pas.
                                const double fy = uy / (double)celly;
                                c = fy > 0.60 ? '_' : '-';
                            } else if (aa >= 67.5) {
                                c = '|';
                            } else {
                                c = a > 0 ? '\\' : '/';
                            }
                        }
                    }
                    if (c == ' ') {
                        if (d >= 0.70) c = '#';
                        else if (d >= 0.45) c = '*';
                        else if (d >= 0.12) c = ':';
                        else if (d > 0.01) c = '.';
                    }
                    gr[(size_t)cy][(size_t)cx] = c;
                }
        }

        std::vector<const visio::Element*> zs;
        zs.reserve(elements.size());
        for (const auto& e : elements) zs.push_back(&e);
        std::sort(zs.begin(), zs.end(),
                  [](const visio::Element* a, const visio::Element* b) {
                      return a->rect.area() > b->rect.area();
                  });
        size_t formes = 0;
        std::vector<visio::Rect> interieurs;   // les cadres déjà dessinés
        for (const auto* e : zs) {
            const visio::Rect& r = e->rect;
            // Ce qui est DEDANS une forme fermée n'est pas redessiné : l'intérieur d'un
            // rond est un rectangle de la même taille, et le dessiner transforme le rond en
            // carré. Une ligne de texte non plus : l'encre la montre déjà.
            if (!e->cadre) {
                if (e->genre == visio::Genre::Texte) continue;
                bool dedans = false;
                for (const auto& c : interieurs)
                    if (r.x > c.x && r.y > c.y && r.x + r.w < c.x + c.w && r.y + r.h < c.y + c.h) {
                        dedans = true;
                        break;
                    }
                if (dedans) continue;
            } else {
                interieurs.push_back(r);
            }
            int x0 = (r.x - f.region.x) / cellx, y0 = (r.y - f.region.y) / celly;
            int x1 = (r.x + r.w - f.region.x) / cellx, y1 = (r.y + r.h - f.region.y) / celly;
            x0 = std::max(0, std::min(x0, cols - 1)); x1 = std::max(0, std::min(x1, cols - 1));
            y0 = std::max(0, std::min(y0, rows - 1)); y1 = std::max(0, std::min(y1, rows - 1));
            char id[12];
            std::snprintf(id, sizeof id, "%06lx", (unsigned long)(e->id & 0xFFFFFF));

            // Une forme fermee qui n'est PAS rectangulaire ne recoit pas de cadre : le
            // dessiner en rectangle mentirait sur sa forme. C'est l'encre qui la montre ;
            // on ne pose que son nom, sur sa premiere ligne.
            // Une forme fermee non rectangulaire n'est dessinee au contour que si son contour
            // RADIAL est lisse. Un bloc de texte pris pour une forme a un rayon qui saute d'une
            // lettre a l'autre : le dessiner au contour ne donne que du bruit. Mesure : la
            // variation moyenne du rayon d'un tour a l'autre, rapportee au rayon moyen — 0,05
            // pour un rond, plus de 0,3 pour du texte.
            bool forme_lisible = false;
            std::vector<std::pair<double, double>> contour;
            if (e->cadre && e->sur_bord < 0.75) {
                visio::Rect bb0{e->rect.x, e->rect.y, e->rect.w, e->rect.h};
                contour = contour_radial(f, bb0, fond, 3);
                if (contour.size() >= 20) {
                    double cxm = 0, cym = 0;
                    for (const auto& q : contour) { cxm += q.first; cym += q.second; }
                    cxm /= (double)contour.size();
                    cym /= (double)contour.size();
                    double smoy = 0, dvar = 0, prec = 0;
                    for (size_t i = 0; i < contour.size(); ++i) {
                        const double rr2 = std::hypot(contour[i].first - cxm, contour[i].second - cym);
                        smoy += rr2;
                        if (i) dvar += std::abs(rr2 - prec);
                        prec = rr2;
                    }
                    smoy /= (double)contour.size();
                    dvar /= (double)contour.size();
                    if (smoy > 1.0 && dvar / smoy < 0.12) forme_lisible = true;
                }
            }
            if (e->cadre && e->sur_bord < 0.75 && forme_lisible) {
                ++formes;
                // On TRACE LE CONTOUR : c'est la forme elle-meme, pas une case d'encre. La
                // couche d'encre est effacee dans la boite, sinon le contour se superpose a
                // l'encre deja posee et la forme ressort doublee.
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) gr[(size_t)y][(size_t)x] = ' ';
                dessiner_contour(gr, contour, f.region, cellx, celly, cols, rows);
                std::string lab = std::string(nom(e->genre)) + " " + id;
                const int place = std::max(0, x1 - x0);
                // etiquette sous la forme, pour ne pas la recouvrir
                const int yl = std::min(rows - 1, y1 + 1);
                if (place > 3) {
                    if ((int)lab.size() > place) lab = lab.substr(0, (size_t)place);
                    for (size_t k = 0; k < lab.size(); ++k) gr[(size_t)yl][(size_t)x0 + k] = lab[k];
                }
                continue;
            }
            // Un bouton de 32 px de haut ne fait pas deux cases de plan : pas de cadre
            // possible. On lui donne une ligne d'encre rehaussee et son nom, sinon la zone
            // est visible dans les pixels mais son identifiant n'apparait nulle part et
            // l'agent ne peut pas la nommer.
            if (x1 - x0 < 3 || y1 - y0 < 2) {
                const int y = (y0 + y1) / 2;
                for (int x = x0; x <= x1; ++x) gr[(size_t)y][(size_t)x] = '=';
                const std::string lab = std::string(" ") + nom(e->genre) + " " + id;
                const int depart = x1 + 1;
                if (depart + (int)lab.size() < cols)
                    for (size_t k = 0; k < lab.size(); ++k) gr[(size_t)y][(size_t)depart + k] = lab[k];
                continue;
            }
            // Les traits du cadre sont STRUCTURELS : ils passent sur l'encre (sinon un
            // point d'encre coupe la bordure et le plan devient illisible).
            for (int x = x0; x <= x1; ++x) {
                gr[(size_t)y0][(size_t)x] = '-';
                gr[(size_t)y1][(size_t)x] = '-';
            }
            for (int y = y0; y <= y1; ++y) {
                gr[(size_t)y][(size_t)x0] = '|';
                gr[(size_t)y][(size_t)x1] = '|';
            }
            gr[(size_t)y0][(size_t)x0] = '+'; gr[(size_t)y0][(size_t)x1] = '+';
            gr[(size_t)y1][(size_t)x0] = '+'; gr[(size_t)y1][(size_t)x1] = '+';
            // L'encre du meme trait tombe souvent dans la case VOISINE (le trait de 1 px est a
            // cheval sur la frontiere) : on la blanchit, sinon le plan montre des bordures
            // doubles « || » qui n'existent pas dans la page.
            for (int y = y0; y <= y1; ++y) {
                if (x0 > 0) gr[(size_t)y][(size_t)x0 - 1] = ' ';
                if (x1 + 1 < cols) gr[(size_t)y][(size_t)x1 + 1] = ' ';
            }
            for (int x = x0; x <= x1; ++x) {
                if (y0 > 0) gr[(size_t)y0 - 1][(size_t)x] = ' ';
                if (y1 + 1 < rows) gr[(size_t)y1 + 1][(size_t)x] = ' ';
            }
            // Et l'encre tombe aussi a l'INTERIEUR du rect (le rect est la boite du trait, donc
            // le trait y est contenu sur 1-2 px) : on blanchit une case a l'interieur des bords.
            if (x1 - x0 >= 5 && y1 - y0 >= 3) {
                for (int y = y0 + 1; y < y1; ++y) {
                    gr[(size_t)y][(size_t)x0 + 1] = ' ';
                    gr[(size_t)y][(size_t)x1 - 1] = ' ';
                }
                for (int x = x0 + 1; x < x1; ++x) {
                    gr[(size_t)y0 + 1][(size_t)x] = ' ';
                    gr[(size_t)y1 - 1][(size_t)x] = ' ';
                }
            }
            // Etiquette : genre + debut d'identifiant, TOUJOURS rognee a la largeur du cadre,
            // sinon le plan devient illisible (la bordure disparait).
            std::string lab = std::string(" ") + nom(e->genre) + " " + id + " ";
            const int place = x1 - x0 - 1;
            if (y0 + 1 < y1 && place > 3) {
                if ((int)lab.size() > place) lab = lab.substr(0, (size_t)place);
                for (size_t k = 0; k < lab.size(); ++k) gr[(size_t)y0 + 1][(size_t)x0 + 1 + k] = lab[k];
            }
        }
        // UN SEUL CARACTERE PAR RUN DE TRAIT, PAR LIGNE. Un cote de triangle a 51° traverse
        // 1,3 case par ligne (13 px pour une case de 20 px de haut) : il allumait donc deux
        // caracteres par ligne et ressortait double. On regroupe les cases contigues portant
        // un trait non horizontal et on ne garde que celle qui porte le plus d'encre : le
        // cote devient une suite de « / » d'un caractere, comme un dessin a la main.
        // Les traits horizontaux ne sont pas touches : une ligne de « - » est deja continue.
        for (int cy = 0; cy < rows; ++cy) {
            int x = 0;
            while (x < cols) {
                const char c0 = gr[(size_t)cy][(size_t)x];
                if (c0 != '|' && c0 != '/' && c0 != '\\') { ++x; continue; }
                int fin = x;
                while (fin + 1 < cols) {
                    const char c1 = gr[(size_t)cy][(size_t)fin + 1];
                    if (c1 != '|' && c1 != '/' && c1 != '\\') break;
                    ++fin;
                }
                if (fin > x) {
                    int garde = x;
                    for (int i = x + 1; i <= fin; ++i)
                        if (ncar[(size_t)cy * (size_t)cols + (size_t)i] >
                            ncar[(size_t)cy * (size_t)cols + (size_t)garde])
                            garde = i;
                    for (int i = x; i <= fin; ++i)
                        if (i != garde) gr[(size_t)cy][(size_t)i] = ' ';
                }
                x = fin + 1;
            }
        }
        std::printf("\nplan         %dx%d cases de %dx%d px (1 caractere = 1 case), %zu zones dont "
                    "%zu formes non rectangulaires\n", cols, rows, cellx, celly, elements.size(),
                    formes);
        std::printf("encre        ' ' rien  '.' trait  ':' clairseme  '*' dense  '#' plein\n");
        std::printf("             - | / \\ trait fin oriente (un rond, un triangle, une diagonale)\n");
        for (const auto& l : gr) std::printf("|%s|\n", l.c_str());
    }

    // --cibles : la decoupe FINE. Un mot = une cible ; chaque cible porte un identifiant et
    // son etiquette. C'est ce que l'agent recoit : il choisit un id, il ne voit pas de pixels.
    if (g_cibles) {
        const auto t5 = std::chrono::steady_clock::now();
        bool deja_clique = false;
        std::vector<std::tuple<uint64_t, std::string, visio::Rect>> vus;
        visio::Lecteur lecteur;
        size_t total = 0;
        std::printf("\ncibles       (un mot = une cible, lecture mot par mot)\n");

        // OPTIMISATION : une cible deja vue (meme id, donc meme geometrie) a deja ete lue.
        // Son etiquette est dans l'etat du scan precedent : on ne relit pas un pixel.
        std::map<std::string, std::string> memo;
        size_t evitees = 0;
        {
            const char* h0 = std::getenv("HOME");
            std::ifstream in(std::string(h0 ? h0 : ".") + "/.cache/visio/" +
                             std::to_string((int)fenetre) + ".txt");
            std::string l3;
            while (std::getline(in, l3)) {
                const auto p = l3.find(' ');
                const auto b = l3.find('|');
                if (p == std::string::npos || b == std::string::npos) continue;
                memo[l3.substr(0, p)] = l3.substr(b + 1);
            }
        }

        for (const auto& l : lignes) {
            const auto gs = visio::extraire_glyphes(f, l, fond);
            if (gs.empty()) continue;
            size_t debut = 0;
            for (size_t k = 1; k <= gs.size(); ++k) {
                bool coupe = (k == gs.size());
                if (!coupe) {
                    const int ecart = gs[k].rect.x - gs[k - 1].rect.right();
                    // Un espace separe deux mots ; sous 45 % de la hauteur, c'est la meme
                    // cible (lettres soudées ou kerning serre).
                    coupe = ecart > 0 && (float)ecart > 0.45f * (float)gs[k - 1].rect.h;
                }
                if (!coupe) continue;
                const visio::Rect r = visio::Rect{gs[debut].rect.x, l.rect.y,
                                                  gs[k - 1].rect.right() - gs[debut].rect.x,
                                                  l.rect.h};
                // L'identifiant se calcule AVANT de lire : geometrie + empreinte des PIXELS.
                // Sans l'empreinte, une horloge ou un compteur garderait son ancienne
                // etiquette (meme place, meme taille, texte different). Avec elle, un mot qui
                // change est une cible neuve : relu une fois, puis mis en cache.
                uint64_t sig = 1469598103934665603ull;
                for (int yy = r.y; yy < r.y + r.h; ++yy) {
                    const int ly = yy - f.region.y;
                    if (ly < 0 || ly >= f.region.h) continue;
                    const uint32_t* row = f.px.data() + (size_t)ly * f.region.w;
                    for (int xx = r.x; xx < r.x + r.w; ++xx) {
                        const int lx = xx - f.region.x;
                        if (lx < 0 || lx >= f.region.w) continue;
                        sig ^= (row[lx] & 0xFFFFFFu);
                        sig *= 1099511628211ull;
                    }
                }
                uint64_t id = 1469598103934665603ull;
                const std::string gk = std::to_string((int)fenetre) + "|" +
                                       std::to_string(r.x) + "," + std::to_string(r.y) + "," +
                                       std::to_string(r.w) + "x" + std::to_string(r.h) + "|" +
                                       std::to_string(sig);
                for (unsigned char ch : gk) { id ^= ch; id *= 1099511628211ull; }
                char cle[32];
                std::snprintf(cle, sizeof cle, "%016lx", id);

                std::string txt;
                const auto mi = memo.find(cle);
                if (mi != memo.end()) {
                    txt = mi->second;      // deja lue : cout nul
                    ++evitees;
                } else {
                    visio::LigneTexte mot{r, 0, 1.0};
                    txt = lecteur.lire(f, mot, fond);
                }
                if (!txt.empty() && txt != " ") {
                    vus.emplace_back(id, txt, r);
                    if (!g_delta)
                        std::printf("  %016lx  cliquer  %4dx%-3d @%5d,%-4d  « %s »\n", id, r.w,
                                    r.h, r.x, r.y, txt.c_str());
                    ++total;

                    // --clic : le geste lui-meme. On vise le CENTRE de la cible, on
                    // deplace, on appuie, on relache. Rien d'autre n'est n
                    if (!g_clic.empty() && !deja_clique && txt.find(g_clic) != std::string::npos) {
                        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                        // Le geste passe par visio::Contexte : focus donne a la cible sans la
                        // lever, clic, puis pointeur et focus REMIS en place (docs/GESTES.md).
                        visio::Contexte ctx(d);
                        ctx.pointer(fenetre);
                        ctx.faire_le_geste(cx, cy, "");
                        std::printf("clic         sur « %s » au centre %d,%d (id %016lx) ; pointeur "
                                    "rendu a %d,%d\n", txt.c_str(), cx, cy, id,
                                    ctx.pointeur_x_apres(), ctx.pointeur_y_apres());
                        deja_clique = true;
                    }
                }
                debut = k;
            }
        }

        {   // l'etat est toujours ecrit : c'est aussi le cache des etiquettes
            const char* home = std::getenv("HOME");
            const std::string base = std::string(home ? home : ".") + "/.cache/visio";
            const std::string chemin = base + "/" + std::to_string((int)fenetre) + ".txt";
            std::map<std::string, std::string> avant;
            {
                std::ifstream in(chemin);
                std::string l2;
                while (std::getline(in, l2)) {
                    const auto p = l2.find(' ');
                    if (p == std::string::npos) continue;
                    avant[l2.substr(0, p)] = l2.substr(p + 1);
                }
            }
            std::map<std::string, std::string> apres;
            size_t nouveaux = 0, changes = 0;
            for (const auto& v : vus) {
                char id_hex[32], etat[192];
                std::snprintf(id_hex, sizeof id_hex, "%016lx", std::get<0>(v));
                const visio::Rect& r = std::get<2>(v);
                std::snprintf(etat, sizeof etat, "%d,%d,%dx%d|%s", r.x, r.y, r.w, r.h,
                              std::get<1>(v).c_str());
                apres[id_hex] = etat;
                const auto it = avant.find(id_hex);
                if (it == avant.end()) {
                    if (g_delta) std::printf("  +  %s  %s\n", id_hex, etat);
                    ++nouveaux;
                } else if (it->second != etat) {
                    if (g_delta) std::printf("  ~  %s  %s\n", id_hex, etat);
                    ++changes;
                }
            }
            size_t partis = 0;
            for (const auto& a : avant)
                if (!apres.count(a.first)) {
                    if (g_delta) std::printf("  -  %s\n", a.first.c_str());
                    ++partis;
                }
            std::error_code ec;
            std::filesystem::create_directories(base, ec);
            std::ofstream out(chemin, std::ios::trunc);
            for (const auto& a : apres) out << a.first << ' ' << a.second << '\n';
            if (g_delta)
                std::printf("delta        %zu nouveaux, %zu modifies, %zu disparus (%zu cibles)\n",
                            nouveaux, changes, partis, apres.size());
        }
        std::printf("cibles       %zu dont %zu relues depuis le cache, en %.1f ms\n", total,
                    evitees,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t5).count());
    }


    // --formes : les cibles NON textuelles. Une icone, une case, un curseur ne portent pas de
    // texte : on les trouve par leur forme (petit bloc compact, de couleur, hors des lignes
    // de texte) et on les etiquette par leur geometrie, faute de mieux.
    if (g_formes) {
        const auto t6 = std::chrono::steady_clock::now();
        // Un petit bloc est "compact" : son aire est proche de sa boite. C'est ce qui
        // distingue une icone d'un simple trait ou d'un aplat de fond.
        const auto petites = visio::regions_uniformes(f, 5, 40, 0.55);
        size_t nf = 0;
        for (const auto& rg : petites) {
            const visio::Rect& r = rg.rect;
            if (r.w < 8 || r.h < 8 || r.w > 160 || r.h > 160) continue;
            // Une icone est a peu pres carree ; une bande de couleur ne l'est pas. C'est ce
            // qui separe un bouton d'icone d'un simple aplat de fond.
            const double ratio = (double)r.w / (double)r.h;
            if (ratio < 0.5 || ratio > 2.0) continue;
            if (r.w * r.h > 4096) continue;
            bool sur_texte = false;
            for (const auto& l : lignes)
                if (!visio::intersect(r, l.rect).empty()) { sur_texte = true; break; }
            if (sur_texte) continue;
            uint64_t id = 1469598103934665603ull;
            const std::string gk = "f|" + std::to_string((int)fenetre) + "|" + std::to_string(r.x) +
                                   "," + std::to_string(r.y) + "," + std::to_string(r.w) + "x" +
                                   std::to_string(r.h);
            for (unsigned char ch : gk) { id ^= ch; id *= 1099511628211ull; }
            std::printf("  %016lx  forme    %4dx%-3d @%5d,%-4d  « zone sans texte »\n", id, r.w,
                        r.h, r.x, r.y);
            ++nf;
        }
        std::printf("formes       %zu cibles non textuelles en %.1f ms\n", nf,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t6).count());
    }

    if (g_lire && !lignes.empty()) {
        // Une fois par fenêtre : on apprend sa typo sur les bandes les plus fournies.
        const auto t1 = std::chrono::steady_clock::now();
        lecteur.calibrer(f, lignes, fond);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t1).count();
        if (lecteur.police_connue())
            std::printf("typo         %s, %.2f ms (une fois pour la fenetre)\n",
                        lecteur.police().c_str(), ms);
        else
            std::printf("typo         non identifiee, %.2f ms (chaque bande cherchera)\n", ms);
    }
    const size_t maxl = lignes.size() < 12 ? lignes.size() : 12;
    for (size_t i = 0; i < maxl; ++i) {
        const auto& l = lignes[i];
        const auto t1 = std::chrono::steady_clock::now();
        const std::string lu = g_lire ? lecteur.lire(f, l, fond) : std::string();
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t1).count();
        if (g_lire)
            std::printf("  %4dx%-3d @ %4d,%-4d  %6.2f ms  « %s »\n", l.rect.w, l.rect.h, l.rect.x,
                        l.rect.y, ms, lu.c_str());
        else
            std::printf("  %4dx%-3d @ %4d,%-4d  densite %.2f\n", l.rect.w, l.rect.h, l.rect.x,
                        l.rect.y, l.densite);
    }

    cap.release();
    return 0;
}

visio::Rect parse_rect(const char* s) {
    visio::Rect r{};
    if (std::sscanf(s, "%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h) != 4) return {};
    return r;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lire") == 0) g_lire = true;
        if (std::strcmp(argv[i], "--etiquettes") == 0) { g_etiquettes = true; g_lire = true; }
        if (std::strcmp(argv[i], "--cibles") == 0) { g_cibles = true; g_lire = true; }
        if (std::strcmp(argv[i], "--clic") == 0 && i + 1 < argc) { g_clic = argv[++i]; g_cibles = true; g_lire = true; }
        if (std::strcmp(argv[i], "--taper") == 0 && i + 1 < argc) g_taper = argv[++i];
        if (std::strcmp(argv[i], "--delta") == 0) { g_delta = true; g_cibles = true; g_lire = true; }
        if (std::strcmp(argv[i], "--formes") == 0) { g_formes = true; g_lire = true; }
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) g_dump = argv[++i];
        if (std::strcmp(argv[i], "--gestes") == 0 && i + 1 < argc) g_gestes = argv[++i];
        if (std::strcmp(argv[i], "--win") == 0 && i + 1 < argc) g_motif = argv[++i];
        if (std::strcmp(argv[i], "--ascii") == 0) {
            g_ascii = true;
            // --ascii [pas] : la taille d'une case en pixels. Le plan est a l'echelle des
            // pixels : une case de 20 px donne 94x52 caracteres, une case de 10 px en donne
            // 188x104 (deux fois plus fin, quatre fois plus de caracteres). Le cout se paie
            // en jetons de contexte, pas en temps de calcul : a mesurer avant de choisir.
            if (i + 1 < argc) {
                int p = 0;
                if (std::sscanf(argv[i + 1], "%d", &p) == 1 && p >= 1 && p <= 40) {
                    g_pas = p;
                    ++i;
                }
            }
        }
        if (std::strcmp(argv[i], "--boites") == 0) {
            g_boites = true;
            // --boites [cote_min,aire_min] : les seuils, pour mesurer au lieu de deviner.
            if (i + 1 < argc) {
                int c = 0, a = 0;
                if (std::sscanf(argv[i + 1], "%d,%d", &c, &a) == 2 && c > 0 && a > 0) {
                    g_cote_min = c;
                    g_aire_min = a;
                    ++i;
                }
            }
        }
    }

    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        VISIO_ERROR("impossible d'ouvrir le serveur X (%s)", XDisplayName(nullptr));
        return 1;
    }
    // La fenetre visee par un geste, pour lui donner le focus sans la lever.
    Window fenetre_visee = 0;
    if (!g_motif.empty())
        for (const auto& w : visio::list_windows(d, true))
            if (w.title.find(g_motif) != std::string::npos) { fenetre_visee = w.id; break; }

    // --taper : le geste clavier, DANS UN CONTEXTE REVERSIBLE (pointeur et focus rendus a la
    // fin). La frappe part vers la fenetre au focus : c'est la limite de XTest, documentee
    // dans docs/GESTES.md, et uinput ne la contourne pas.
    if (!g_taper.empty()) {
        visio::Contexte ctx(d);
        ctx.pointer(fenetre_visee);
        ctx.faire_le_geste(-1, -1, g_taper);
        std::printf("taper        %zu caracteres ; pointeur rendu a %d,%d (avant %d,%d) ; focus rendu\n",
                    g_taper.size(), ctx.pointeur_x_apres(), ctx.pointeur_y_apres(), ctx.pointeur_x(),
                    ctx.pointeur_y());
        XCloseDisplay(d);
        return 0;
    }

    // --gestes "x,y,texte" : le meme geste, mais on CONSTATE au lieu de supposer. Sert au test
    // sous Xvfb (tests/gestes_xvfb.sh) : les evenements doivent arriver, le pointeur revenir et
    // le focus etre rendu.
    if (!g_gestes.empty()) {
        int x = -1, y = -1;
        std::string t;
        {
            const size_t p1 = g_gestes.find(',');
            const size_t p2 = (p1 == std::string::npos) ? std::string::npos : g_gestes.find(',', p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos) {
                x = std::atoi(g_gestes.substr(0, p1).c_str());
                y = std::atoi(g_gestes.substr(p1 + 1, p2 - p1 - 1).c_str());
                t = g_gestes.substr(p2 + 1);
            }
        }
        const int lus = (x >= 0 && y >= 0) ? 3 : 0;
        visio::Contexte ctx(d);
        ctx.sauver();
        const int avant_x = ctx.pointeur_x(), avant_y = ctx.pointeur_y();
        const unsigned long focus_avant = (unsigned long)ctx.focus_avant();
        // Sans point de clic, on vise la FENETRE : le focus lui est donne le temps de la
        // frappe, puis rendu. C'est le cas d'usage reel — taper dans une fenetre qui n'a pas
        // le focus, sans le lui laisser.
        if (lus < 2 && fenetre_visee) ctx.pointer(fenetre_visee);
        ctx.faire_le_geste((lus >= 2) ? x : -1, (lus >= 2) ? y : -1, t);
        const unsigned long focus_apres = (unsigned long)ctx.focus_apres();
        std::printf("gestes       pointeur %d,%d -> %d,%d  focus %#lx -> %#lx  texte « %s »\n",
                    avant_x, avant_y, ctx.pointeur_x_apres(), ctx.pointeur_y_apres(), focus_avant,
                    focus_apres, t.c_str());
        const bool ok = (avant_x == ctx.pointeur_x_apres() && avant_y == ctx.pointeur_y_apres());
        std::printf("gestes       %s\n", ok ? "pointeur rendu : OUI" : "pointeur rendu : NON");
        XCloseDisplay(d);
        return ok ? 0 : 5;
    }


    if (argc >= 2 && std::strcmp(argv[1], "--regions") == 0) {
        const visio::ScreenInfo si = visio::probe_screen(d);
        const visio::Rect zone = (argc >= 3) ? parse_rect(argv[2]) : si.screen;
        if (zone.empty()) { VISIO_ERROR("rectangle attendu : x,y,w,h"); XCloseDisplay(d); return 2; }
        const int rc = mode_regions(d, zone, "demandee", DefaultRootWindow(d));
        XCloseDisplay(d);
        return rc;
    }

    if (argc >= 3 && std::strcmp(argv[1], "--win") == 0) {
        const std::string motif = argv[2];
        for (const auto& w : visio::list_windows(d, true)) {
            if (w.title.find(motif) == std::string::npos) continue;
            std::printf("fenetre      %08lx  %s\n", (unsigned long)w.id, w.title.c_str());
            const int rc = mode_regions(d, w.rect, "fenetre", w.id);
            XCloseDisplay(d);
            return rc;
        }
        VISIO_ERROR("aucune fenetre dont le titre contient « %s »", motif.c_str());
        XCloseDisplay(d);
        return 4;
    }

    const visio::ScreenInfo si = visio::probe_screen(d);
    visio::print_screen_info(si);

    if (!si.shm)    VISIO_WARN("MIT-SHM absent : la capture sera lente");
    if (!si.damage) VISIO_ERROR("DAMAGE absent : aucune perception incrementale possible");
    if (si.compositor)
        VISIO_WARN("composition active : DAMAGE sur la racine ne localise pas (M 34)");

    const auto fenetres = visio::list_windows(d, true);
    std::printf("\nfenetres     %zu (ordre de pile, bas -> haut)\n", fenetres.size());
    for (const auto& w : fenetres) {
        std::printf("  %08lx  %-14.14s %4dx%-4d @ %4d,%-4d  %s%s\n", (unsigned long)w.id,
                    w.cls.empty() ? "?" : w.cls.c_str(), w.rect.w, w.rect.h, w.rect.x, w.rect.y,
                    w.viewable ? "" : "(non affichee) ", w.title.c_str());
    }

    XCloseDisplay(d);
    return 0;
}
