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
#include "perceive/regions.h"
#include "perceive/text_lines.h"
#include "perceive/ocr.h"
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

std::string g_clic;        // --clic <texte> : cliquer la cible dont l'etiquette contient ce texte
std::string g_taper;       // --taper <texte> : ecrire au clavier, caractere par caractere
std::string g_ecrire;      // --ecrire <id> <texte> : saisir dans un champ (clic + Ctrl+A + texte)
std::string g_ecrire_texte;
bool g_texte = false;      // --texte : lire l'ecriture de toute la fenetre, sans plan
bool g_verbeux = false;    // --verbose : les diagnostics (capture, segmentation, catalogue).
                           // Sans lui, un tool qui lit la page ne rend QUE ce qu'il a lu : le
                           // bruit de mesure coutait 8 ko sur un plan de 3 ko (mesure 18/10).
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
        if (ok2 && g_verbeux) std::printf("capture 2e   %.2f ms (tampon deja alloue)\n", t2);
    }

    t0 = std::chrono::steady_clock::now();
    const auto regions = visio::regions_uniformes(f, 5, 64, 0.55);
    const double t_seg = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();

    if (g_verbeux) {
    std::printf("zone         %s %dx%d en %d,%d\n", quoi, zone.w, zone.h, zone.x, zone.y);
    std::printf("capture      %.2f ms   segmentation %.2f ms   %zu regions retenues\n",
                t_cap, t_seg, regions.size());
    }

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
    if (g_verbeux)
        std::printf("boites       %zu cadres fermes en %.2f ms (dont aplats bordes %.2f ms)\n",
                    boites.size(), t_boites, t_bordes);
    if (g_boites) {
        for (const auto& b : boites)
            std::printf("  boite %4dx%-4d @ %4d,%-4d trait #%06x remplissage %.2f bord %.2f %6ld px\n",
                        b.rect.w, b.rect.h, b.rect.x, b.rect.y, b.couleur & 0xFFFFFF,
                        b.remplissage, b.sur_bord, (long)b.pixels);
    }

    const size_t max = g_verbeux ? (regions.size() < 20 ? regions.size() : 20) : 0;
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
    if (g_verbeux)
        std::printf("\ncatalogue    %zu elements en %.2f ms (%zu glyphes ecartes)\n",
                    elements.size(), t_cat, cat.glyphes_ecartes);

    // LES DEUX GESTES DE L'AGENT. L'identifiant est celui que le plan affiche dans la boite ;
    // un prefixe suffit (« a95 » trouve « a958c »).
    //   --clic <id>              cliquer au centre de la cible
    //   --ecrire <id> <texte>    cliquer dans le champ, selectionner, ecrire (input_text)
    if (!g_clic.empty() || !g_ecrire.empty()) {
        const std::string motif = g_ecrire.empty() ? g_clic : g_ecrire;
        const visio::Element* cible = nullptr;
        for (const auto& e : elements) {
            char id[12];
            std::snprintf(id, sizeof id, "%05lx", (unsigned long)(e.id & 0xFFFFF));
            if (motif.size() <= std::strlen(id) && std::strncmp(id, motif.c_str(), motif.size()) == 0) {
                cible = &e;
                break;
            }
        }
        if (!cible) {
            std::printf("geste        aucune cible ne commence par « %s »\n", motif.c_str());
            return 2;
        }
        const int cx = cible->rect.x + cible->rect.w / 2;
        const int cy = cible->rect.y + cible->rect.h / 2;
        const bool saisie = !g_ecrire.empty();
        visio::Contexte ctx(d);
        ctx.pointer(cible->fenetre);
        ctx.faire_le_geste(cx, cy, saisie ? g_ecrire_texte : "", 1, saisie);
        std::printf("geste        %s %04x centre %d,%d  pointeur %d,%d -> %d,%d  %s\n",
                    saisie ? "saisie" : "clic", (unsigned)(cible->id & 0xFFFFF), cx, cy,
                    ctx.pointeur_x(), ctx.pointeur_y(), ctx.pointeur_x_apres(),
                    ctx.pointeur_y_apres(),
                    saisie ? ("« " + g_ecrire_texte + " »").c_str() : "");
        return 0;
    }
    if (g_verbeux) {
        const std::string texte = cat.texte();
        size_t debut = 0, n = 0;
        while (debut < texte.size() && n < 20) {
            const size_t fin = texte.find('\n', debut);
            std::printf("  %s\n", texte.substr(debut, fin - debut).c_str());
            debut = fin + 1;
            ++n;
        }
    }

    // Les lignes de texte : ce que le catalogue écarte, la couche texte le regroupe.
    const uint32_t fond = visio::couleur_fond(f, 3);
    t0 = std::chrono::steady_clock::now();
    const auto lignes = visio::lignes_de_texte(f, fond);
    const double t_txt = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
    if (g_verbeux)
        std::printf("\ntexte        %zu lignes en %.2f ms (fond #%06x)\n", lignes.size(), t_txt,
                    fond & 0xFFFFFF);



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
    // --texte : TOUT ce qui est ecrit dans la fenetre, ligne par ligne, sans plan ni boites. C'est
    // la lecture nue : tesseract sur l'image entiere (~0,5 s pour 1870x1035), pour un texte long
    // (un journal, un article) la ou le plan par boites decouperait trop.
    if (g_texte) {
        // DEUX PASSES, parce qu'aucune ne suffit (mesure sur la page temoin) : la passe globale lit
        // les longues lignes (« booeba », l'adresse, les onglets) mais RATE les petits libelles
        // d'un bouton (« Valider », « Envoyer ») ; la passe par boite les lit, elle. On garde les
        // deux et on ecarte les doublons.
        const auto t1 = std::chrono::steady_clock::now();
        const auto global = visio::ocr_zone(f, f.region);
        std::vector<visio::LigneOcr> tas = global;

        std::vector<visio::Rect> zones;
        for (const auto& e : elements) {
            const visio::Rect r = e.rect;
            if (r.empty() || r.w <= 24 || r.h < 12) continue;
            if (r.w * 100 >= f.region.w * 98 && r.h * 100 >= f.region.h * 98) continue;
            bool doublon = false;
            for (const auto& z : zones)
                if (std::abs(z.x - r.x) <= 6 && std::abs(z.y - r.y) <= 6 &&
                    std::abs(z.w - r.w) <= 6 && std::abs(z.h - r.h) <= 6) {
                    doublon = true;
                    break;
                }
            if (!doublon) zones.push_back(r);
        }
        const auto par_boite = visio::ocr_zones(f, zones);
        for (size_t k = 0; k < par_boite.size(); ++k)
            for (auto l : par_boite[k]) {
                l.y += zones[k].y;      // l'ordonnee rendue est relative a la boite : on la remet
                                        // dans le repere de la fenetre.
                bool connu = false;
                for (const auto& g : global)
                    if (g.texte.find(l.texte) != std::string::npos ||
                        l.texte.find(g.texte) != std::string::npos) {
                        connu = true;
                        break;
                    }
                if (!connu) tas.push_back(l);
            }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t1).count();
        std::printf("texte        %zu lignes, %dx%d px, %zu boites, %.0f ms\n", tas.size(),
                    f.region.w, f.region.h, zones.size(), ms);
        for (const auto& l : tas) std::printf("%4d  %s\n", l.y, l.texte.c_str());
        return 0;
    }

    if (g_ascii) {
        // LE PLAN : les BOITES et ce qui est ECRIT dedans, rien d'autre.
        //
        // Histoire mesuree : le plan a porte une couche d'encre (la forme des pixels) et des
        // contours d'ASCII art. Resultat : 1 432 caracteres dont 1 081 tirets, une ligne de
        // texte de 20 px rendue en « \- /---------- \ ». Ce n'est pas ce qui sert a decider.
        // Ce qui sert : ou sont les boites, ce qu'elles disent, et par quel nom les cliquer.
        //
        // Un caractere de terminal est deux fois plus haut que large : la case vaut 10x20 px,
        // sinon tout sort etire (un rond devenait un oeuf).
        const int cellx = g_pas, celly = g_pas * 2;
        const int cols = std::max(1, f.region.w / cellx);
        const int rows = std::max(1, f.region.h / celly);
        std::vector<std::string> gr((size_t)rows, std::string((size_t)cols, ' '));

        // Les zones, de la plus grande a la plus petite : une petite boite se dessine par-dessus
        // une grande, et son texte ne se fait pas recouvrir.
        std::vector<const visio::Element*> zs;
        zs.reserve(elements.size());
        for (const auto& e : elements) zs.push_back(&e);
        std::sort(zs.begin(), zs.end(), [](const visio::Element* a, const visio::Element* b) {
            return a->rect.area() > b->rect.area();
        });

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

        struct Cible {
            visio::Rect r;
            std::string id;
            std::string genre;
            std::string texte;
        };
        std::vector<Cible> cibles;

        // PASSE 1 : quelles zones sont des cibles ? (les filtres, et pas encore de dessin)
        struct Choix {
            const visio::Element* e;
            visio::Rect r;
            visio::Rect dedans;     // rect recadre de 2 px : c'est lui qu'on donne a lire
            std::string id;         // 5 caracteres, la reference stable
            std::string affiche;    // ce que le plan montre (prefixe unique)
        };
        std::vector<Choix> choix;
        for (const auto* e : zs) {
            const visio::Rect r = e->rect;
            if (r.empty()) continue;
            // La fenetre entiere n'est pas une cible : elle contient tout le reste.
            if (r.w * 100 >= f.region.w * 98 && r.h * 100 >= f.region.h * 98) continue;
            // Les glyphes isoles (<= 24x24) sont du texte, pas des boites : ils sont lus avec la
            // ligne qui les porte, sinon chaque lettre devient une cible.
            if (r.w <= 24 && r.h <= 24) continue;
            // Le catalogue voit DEUX fois la meme boite quand la bordure et l'interieur donnent
            // deux regions (240x120 et 238x118). On garde la premiere, la plus grande.
            bool doublon = false;
            for (const auto& c : choix) {
                const int dh = std::abs(c.r.w - r.w), dv = std::abs(c.r.h - r.h);
                if (dh <= 6 && dv <= 6 && std::abs(c.r.x - r.x) <= 6 && std::abs(c.r.y - r.y) <= 6) {
                    doublon = true;
                    break;
                }
            }
            if (doublon) continue;
            // Identifiant court et STABLE (le catalogue le calcule sur la geometrie) : c'est
            // l'argument de l'outil de clic. On garde 5 caracteres en memoire, mais le plan n'en
            // affiche que ce qu'il faut pour rester unique : 3 en general (assez pour un bouton
            // de 11 cases qui doit montrer « Valider »), davantage si deux cibles commencent
            // pareil. L'outil de clic accepte n'importe quel prefixe.
            char id[12];
            std::snprintf(id, sizeof id, "%05lx", (unsigned long)(e->id & 0xFFFFF));
            const visio::Rect dedans{r.x + 2, r.y + 2, std::max(0, r.w - 4), std::max(0, r.h - 4)};
            choix.push_back({e, r, dedans, id});
        }

        // Longueur d'affichage : la plus courte qui reste unique dans ce plan.
        for (auto& c : choix) {
            size_t n = 3;
            while (n < c.id.size()) {
                bool collision = false;
                for (const auto& a : choix) {
                    if (&a == &c) continue;
                    if (a.id.compare(0, n, c.id, 0, n) == 0) { collision = true; break; }
                }
                if (!collision) break;
                ++n;
            }
            c.affiche = c.id.substr(0, n);
        }

        // PASSE 2 : le texte de toutes les cibles en UN appel au moteur.
        std::vector<visio::Rect> a_lire;
        for (const auto& c : choix) a_lire.push_back(c.dedans);
        const auto t1 = std::chrono::steady_clock::now();
        const auto textes = visio::ocr_zones(f, a_lire);
        const double t_ocr = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - t1).count();

        // PASSE 3 : dessiner les boites, poser l'identifiant et le texte.
        size_t lus = 0;
        for (size_t k = 0; k < choix.size(); ++k) {
            const visio::Rect& r = choix[k].r;
            const std::string& id = choix[k].affiche;
            const auto& ls = textes[k];
            std::string texte;
            for (const auto& l : ls) {
                if (!texte.empty()) texte += " / ";
                texte += l.texte;
            }
            if (!texte.empty()) ++lus;

            int x0 = (r.x - f.region.x) / cellx, y0 = (r.y - f.region.y) / celly;
            int x1 = (r.x + r.w - f.region.x) / cellx, y1 = (r.y + r.h - f.region.y) / celly;
            x0 = std::max(0, std::min(x0, cols - 1));
            x1 = std::max(0, std::min(x1, cols - 1));
            y0 = std::max(0, std::min(y0, rows - 1));
            y1 = std::max(0, std::min(y1, rows - 1));

            // Une boite d'une seule rangee (un champ de 23 px) n'a pas la place pour un cadre :
            // le cadre mangerait sa propre ligne et le texte avec. On la montre entre crochets.
            const bool mince = (y1 - y0) < 2;
            const bool cadre = (x1 - x0 >= 3 && y1 - y0 >= 1 && !mince);
            if (cadre) {
                for (int x = x0; x <= x1; ++x) {
                    gr[(size_t)y0][(size_t)x] = '-';
                    gr[(size_t)y1][(size_t)x] = '-';
                }
                for (int y = y0; y <= y1; ++y) {
                    gr[(size_t)y][(size_t)x0] = '|';
                    gr[(size_t)y][(size_t)x1] = '|';
                }
                gr[(size_t)y0][(size_t)x0] = '+';
                gr[(size_t)y0][(size_t)x1] = '+';
                gr[(size_t)y1][(size_t)x0] = '+';
                gr[(size_t)y1][(size_t)x1] = '+';
            }

            // L'identifiant, puis le texte, a l'interieur. Une zone ecrivable vide montre des
            // « _ » : c'est la qu'un input_text a du sens.
            const int lx = cadre ? x0 + 1 : x0;
            const int ly = cadre ? y0 + 1 : y0;
            const int place = cadre ? x1 - x0 - 1 : 4;
            if (place >= 5) {
                for (int i = 0; id[i] && lx + i < cols; ++i) gr[(size_t)ly][(size_t)lx + i] = id[i];
                const int tx = lx + (int)id.size() + 1;
                if (texte.empty() && choix[k].e->genre == visio::Genre::Champ) {
                    for (int x = tx; x < lx + place; ++x) gr[(size_t)ly][(size_t)x] = '_';
                } else {
                    for (const auto& l : ls) {
                        int yy = ly + l.y / celly;
                        if (yy <= y0 || yy >= y1 || yy >= rows) continue;
                        int xx = tx;
                        for (char ch : l.texte) {
                            if (xx >= x1 || xx >= cols) break;
                            gr[(size_t)yy][(size_t)xx++] = ch;
                        }
                    }
                }
            }
            if (mince) {
                // [identifiant texte] : la zone reste visible et cliquable sans cadre.
                int xx = x0;
                if (xx < cols) gr[(size_t)y0][(size_t)xx++] = '[';
                for (int i = 0; id[i] && xx < cols; ++i) gr[(size_t)y0][(size_t)xx++] = id[i];
                if (xx < cols) gr[(size_t)y0][(size_t)xx++] = ' ';
                // Le texte DEBORDE la boite si elle est etroite : « Envoyer » dans 6 cases ne
                // tient pas, mais le lire vaut mieux que le couper a « E ».
                for (char ch : texte) {
                    if (xx >= cols) break;
                    gr[(size_t)y0][(size_t)xx++] = ch;
                }
                if (xx < cols) gr[(size_t)y0][(size_t)xx] = ']';
            }
            cibles.push_back({r, id, nom(choix[k].e->genre), texte});
        }

        // Les lignes de texte qui ne sont dans aucune boite (un titre, une adresse, une ligne de
        // tableau) : lues elles aussi en un seul appel, puis posees a leur place.
        std::vector<visio::Rect> libres;
        for (const auto& l : lignes) {
            bool dedans = false;
            for (const auto& c : cibles)
                if (l.rect.x >= c.r.x && l.rect.y >= c.r.y && l.rect.x + l.rect.w <= c.r.x + c.r.w &&
                    l.rect.y + l.rect.h <= c.r.y + c.r.h) {
                    dedans = true;
                    break;
                }
            if (!dedans) libres.push_back(l.rect);
        }
        size_t hors = 0;
        if (!libres.empty()) {
            const auto tl = visio::ocr_zones(f, libres);
            for (size_t k = 0; k < libres.size(); ++k) {
                if (tl[k].empty()) continue;
                ++hors;
                const visio::Rect& r = libres[k];
                int y = (r.y - f.region.y) / celly, x = (r.x - f.region.x) / cellx;
                y = std::max(0, std::min(rows - 1, y));
                x = std::max(0, std::min(cols - 1, x));
                int xx = x;
                for (char ch : tl[k][0].texte) {
                    if (xx >= cols) break;
                    gr[(size_t)y][(size_t)xx++] = ch;
                }
            }
        }

        std::printf("\nplan         %dx%d cases de %dx%d px  %zu cibles  %zu avec texte  "
                    "%zu lignes hors boite\n",
                    cols, rows, cellx, celly, cibles.size(), lus, hors);
        std::printf("ocr          %.0f ms (tesseract, %s)\n", t_ocr,
                    visio::ocr_disponible() ? "present" : "ABSENT");
        std::printf("cible        id     genre      position        texte\n");
        for (const auto& c : cibles) {
            std::printf("             %-5s  %-9s  %4d,%-4d %3dx%-3d  %s\n", c.id.c_str(),
                        c.genre.c_str(), c.r.x, c.r.y, c.r.w, c.r.h,
                        c.texte.substr(0, 40).c_str());
        }
        std::printf("\n");
        for (const auto& l : gr) std::printf("|%s|\n", l.c_str());
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
        if (std::strcmp(argv[i], "--clic") == 0 && i + 1 < argc) g_clic = argv[++i];
        if (std::strcmp(argv[i], "--taper") == 0 && i + 1 < argc) g_taper = argv[++i];
        if (std::strcmp(argv[i], "--ecrire") == 0 && i + 2 < argc) {
            g_ecrire = argv[i + 1];
            g_ecrire_texte = argv[i + 2];
            i += 2;
        }
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) g_dump = argv[++i];
        if (std::strcmp(argv[i], "--texte") == 0) g_texte = true;
        if (std::strcmp(argv[i], "--verbose") == 0) g_verbeux = true;
        if (std::strcmp(argv[i], "--gestes") == 0 && i + 1 < argc) g_gestes = argv[++i];
        if (std::strcmp(argv[i], "--win") == 0 && i + 1 < argc) g_motif = argv[++i];
        if (std::strcmp(argv[i], "--plan") == 0 || std::strcmp(argv[i], "--ascii") == 0) {
            g_ascii = true;
            // --plan [pas] : la taille d'une case en pixels. Le plan est a l'echelle des pixels :
            // une case de 20 px donne 94x52 caracteres, une case de 10 px en donne 188x52 (deux
            // fois plus fin, quatre fois plus de caracteres). Le cout se paie en jetons de
            // contexte, pas en temps de calcul : a mesurer avant de choisir.
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
