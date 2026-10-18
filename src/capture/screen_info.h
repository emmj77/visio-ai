// src/capture/screen_info.h — ce que l'écran est, avant de le lire.
#pragma once
#include "common/rect.h"
#include <X11/Xlib.h>
#include <cstdio>

namespace visio {

struct ScreenInfo {
    Rect   screen{};          // tout l'écran virtuel
    int    depth = 0;
    int    dpi = 0;
    bool   compositor = false;   // _NET_WM_CM_Sn possédé : fenêtres redirigées
    bool   shm = false;          // MIT-SHM
    bool   damage = false;       // DAMAGE
    bool   composite = false;    // Composite
    bool   xfixes = false;       // XFixes (curseur, régions)
    bool   xtest = false;        // XTest (actionneur niveau 0)
    bool   xinput2 = false;      // XI2 / MPX (actionneur niveau 1)
    bool   xfixes_cursor = false;
    int    damage_opcode = -1;   // opcode, requis par quelques appels
};

// Interroge le serveur. Ne modifie rien, n'ouvre aucune connexion de plus.
ScreenInfo probe_screen(Display* d);

void print_screen_info(const ScreenInfo& si, std::FILE* out = stdout);

} // namespace visio
