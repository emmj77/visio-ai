// src/main.cpp — point d'entrée.
//
//   visio                          ce que l'écran est, et les fenêtres clientes
//   visio --regions x,y,w,h        segmente une région, et dit ce qu'elle contient
//   visio --win <morceau de titre> segmente la fenêtre dont le titre contient ce morceau
#include "capture/screen_info.h"
#include "capture/shm_capture.h"
#include "common/log.h"
#include "core/frame.h"
#include "perceive/regions.h"
#include "perceive/windows.h"

#include <X11/Xlib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int mode_regions(Display* d, visio::Rect zone, const char* quoi) {
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
    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        VISIO_ERROR("impossible d'ouvrir le serveur X (%s)", XDisplayName(nullptr));
        return 1;
    }

    if (argc >= 2 && std::strcmp(argv[1], "--regions") == 0) {
        const visio::ScreenInfo si = visio::probe_screen(d);
        const visio::Rect zone = (argc >= 3) ? parse_rect(argv[2]) : si.screen;
        if (zone.empty()) { VISIO_ERROR("rectangle attendu : x,y,w,h"); XCloseDisplay(d); return 2; }
        const int rc = mode_regions(d, zone, "demandee");
        XCloseDisplay(d);
        return rc;
    }

    if (argc >= 3 && std::strcmp(argv[1], "--win") == 0) {
        const std::string motif = argv[2];
        for (const auto& w : visio::list_windows(d, true)) {
            if (w.title.find(motif) == std::string::npos) continue;
            std::printf("fenetre      %08lx  %s\n", (unsigned long)w.id, w.title.c_str());
            const int rc = mode_regions(d, w.rect, "fenetre");
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
