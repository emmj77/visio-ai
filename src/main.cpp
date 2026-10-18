// src/main.cpp — point d'entrée. M0 : dire ce que l'écran est.
#include "capture/screen_info.h"
#include "common/log.h"

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
        VISIO_WARN("composition active : DAMAGE sur la racine peut etre muet, voir plan §14");

    XCloseDisplay(d);
    return 0;
}
