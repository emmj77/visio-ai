// src/main.cpp — point d'entrée. M0 : dire ce que l'écran est.
#include "capture/screen_info.h"
#include "common/log.h"
#include "perceive/windows.h"

#include <X11/Xlib.h>

int main() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        VISIO_ERROR("impossible d'ouvrir le serveur X (%s)", XDisplayName(nullptr));
        return 1;
    }
    const visio::ScreenInfo si = visio::probe_screen(d);
    visio::print_screen_info(si);

    if (!si.shm)    VISIO_WARN("MIT-SHM absent : la capture sera lente");
    if (!si.damage) VISIO_ERROR("DAMAGE absent : aucune perception incrementale possible");
    if (si.compositor)
        VISIO_WARN("composition active : DAMAGE sur la racine ne localise pas (M 34)");

    // M2 : ce que le serveur dit des fenetres clientes
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
