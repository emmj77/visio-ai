// src/perceive/windows.h — quelles fenêtres existent, où, et comment elles s'appellent.
#pragma once
#include "common/rect.h"

#include <X11/Xlib.h>

#include <string>
#include <vector>

namespace visio {

struct WindowInfo {
    Window      id = 0;
    Rect        rect{};        // en coordonnées racine
    std::string title;
    std::string cls;           // classe WM (ex. "krita", "firefox")
    bool        viewable = false;
};

// Liste les fenêtres clientes. `ordre_pile` = true pour l'ordre d'empilement
// (_NET_CLIENT_LIST_STACKING), du bas vers le haut.
std::vector<WindowInfo> list_windows(Display* d, bool ordre_pile = false);

Rect        window_rect(Display* d, Window w);
std::string window_title(Display* d, Window w);
std::string window_class(Display* d, Window w);

// La fenêtre du dessus qui contient le point, ou None.
Window window_at(const std::vector<WindowInfo>& pile, int x, int y);

} // namespace visio
