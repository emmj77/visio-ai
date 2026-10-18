#include "capture/screen_info.h"

#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>

#include <cstring>

namespace visio {

static bool has_ext(Display* d, const char* name, int* opcode = nullptr, int* first_event = nullptr) {
    int op = 0, ev = 0, err = 0;
    if (!XQueryExtension(d, name, &op, &ev, &err)) return false;
    if (opcode) *opcode = op;
    if (first_event) *first_event = ev;
    return true;
}

static bool compositor_running(Display* d, int screen) {
    char name[64];
    std::snprintf(name, sizeof name, "_NET_WM_CM_S%d", screen);
    Atom a = XInternAtom(d, name, False);
    if (a == None) return false;
    return XGetSelectionOwner(d, a) != None;
}

ScreenInfo probe_screen(Display* d) {
    ScreenInfo si;
    const int screen = DefaultScreen(d);
    si.screen = Rect{0, 0, DisplayWidth(d, screen), DisplayHeight(d, screen)};
    si.depth = DefaultDepth(d, screen);

    // dpi : la taille physique en millimètres, arrondie.
    const int mm = DisplayWidthMM(d, screen);
    if (mm > 0) si.dpi = (int)((si.screen.w * 25.4 / mm) + 0.5);

    si.compositor  = compositor_running(d, screen);
    si.shm         = XShmQueryExtension(d) != 0;
    si.damage      = has_ext(d, DAMAGE_NAME, &si.damage_opcode);
    si.composite   = has_ext(d, "Composite");
    si.xfixes      = has_ext(d, "XFIXES");
    si.xtest       = has_ext(d, XTestExtensionName);
    si.xinput2     = has_ext(d, "XInputExtension");
    si.xfixes_cursor = si.xfixes;
    return si;
}

void print_screen_info(const ScreenInfo& si, std::FILE* out) {
    std::fprintf(out, "ecran       %dx%d  profondeur %d  %d dpi\n",
                 si.screen.w, si.screen.h, si.depth, si.dpi);
    std::fprintf(out, "composition %s\n", si.compositor ? "ACTIVE (fenetres redirigees)" : "desactivee (peinture directe)");
    std::fprintf(out, "SHM %s | DAMAGE %s (opcode %d) | Composite %s | XFixes %s | XTest %s | XI2 %s\n",
                 si.shm ? "oui" : "non", si.damage ? "oui" : "non", si.damage_opcode,
                 si.composite ? "oui" : "non", si.xfixes ? "oui" : "non",
                 si.xtest ? "oui" : "non", si.xinput2 ? "oui" : "non");
}

} // namespace visio
