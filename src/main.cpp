// src/main.cpp — point d'entrée.
//
//   visio                          ce que l'écran est, et les fenêtres clientes
//   visio --regions x,y,w,h        segmente une région, et dit ce qu'elle contient
//   visio --win <morceau de titre> segmente la fenêtre dont le titre contient ce morceau
#include "capture/screen_info.h"
#include "capture/shm_capture.h"
#include "common/log.h"
#include "core/frame.h"
#include "perceive/catalog.h"
#include "perceive/fonte.h"
#include "perceive/regions.h"
#include "perceive/text_lines.h"
#include "perceive/windows.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

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
    const size_t max = regions.size() < 20 ? regions.size() : 20;
    for (size_t i = 0; i < max; ++i) {
        const auto& r = regions[i];
        std::printf("  %2zu  %4dx%-4d @ %4d,%-4d  couleur #%06x  remplissage %.2f  %6ld px\n",
                    i + 1, r.rect.w, r.rect.h, r.rect.x, r.rect.y, r.couleur & 0xFFFFFF,
                    r.remplissage, (long)r.pixels);
    }

    // Le catalogue : ce que le modèle lira. Identifiants stables, genres devinés.
    t0 = std::chrono::steady_clock::now();
    visio::Catalogue cat;
    const auto& elements = cat.maj(fenetre, regions);
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

    // --cibles : la decoupe FINE. Un mot = une cible ; chaque cible porte un identifiant et
    // son etiquette. C'est ce que l'agent recoit : il choisit un id, il ne voit pas de pixels.
    if (g_cibles) {
        const auto t5 = std::chrono::steady_clock::now();
        bool deja_clique = false;
        visio::Lecteur lecteur;
        size_t total = 0;
        std::printf("\ncibles       (un mot = une cible, lecture mot par mot)\n");
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
                visio::LigneTexte mot{r, 0, 1.0};
                const std::string txt = lecteur.lire(f, mot, fond);
                if (!txt.empty() && txt != " ") {
                    // Identifiant stable : empreinte de la fenetre et de la geometrie.
                    uint64_t id = 1469598103934665603ull;
                    const std::string gk = std::to_string((int)fenetre) + "|" +
                                           std::to_string(r.x) + "," + std::to_string(r.y) + "," +
                                           std::to_string(r.w) + "x" + std::to_string(r.h);
                    for (unsigned char ch : gk) { id ^= ch; id *= 1099511628211ull; }
                    std::printf("  %016lx  cliquer  %4dx%-3d @%5d,%-4d  « %s »\n", id, r.w, r.h,
                                r.x, r.y, txt.c_str());
                    ++total;

                    // --clic : le geste lui-meme. On vise le CENTRE de la cible, on
                    // deplace, on appuie, on relache. Rien d'autre n'est n
                    if (!g_clic.empty() && !deja_clique && txt.find(g_clic) != std::string::npos) {
                        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                        XTestFakeMotionEvent(d, -1, cx, cy, CurrentTime);
                        XTestFakeButtonEvent(d, 1, True, CurrentTime);
                        XTestFakeButtonEvent(d, 1, False, CurrentTime);
                        XFlush(d);
                        std::printf("clic         sur « %s » au centre %d,%d (id %016lx)\n",
                                    txt.c_str(), cx, cy, id);
                        deja_clique = true;
                    }
                }
                debut = k;
            }
        }
        std::printf("cibles       %zu en %.1f ms\n", total,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t5).count());
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
    }

    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        VISIO_ERROR("impossible d'ouvrir le serveur X (%s)", XDisplayName(nullptr));
        return 1;
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
