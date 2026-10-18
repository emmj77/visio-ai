// bench/bench_damage.cpp — la mesure qui décide du mode de perception.
// Compare DAMAGE sur la racine et DAMAGE sur une fenêtre, avec un compositeur actif.
// Résultat attendu sous composition : racine = écrans entiers, fenêtre = rectangles localisés.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

struct Stat {
    int events = 0, rects = 0;
    long area = 0;
    int minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1;
};

static void accumule(Stat& s, XRectangle* rs, int n) {
    for (int i = 0; i < n; ++i) {
        ++s.rects;
        s.area += (long)rs[i].width * rs[i].height;
        s.minx = std::min(s.minx, (int)rs[i].x);
        s.miny = std::min(s.miny, (int)rs[i].y);
        s.maxx = std::max(s.maxx, (int)(rs[i].x + rs[i].width));
        s.maxy = std::max(s.maxy, (int)(rs[i].y + rs[i].height));
    }
}

int main() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::printf("pas de display\n"); return 1; }
    const int scr = DefaultScreen(d);
    const Window root = RootWindow(d, scr);

    int ev_base = 0, err_base = 0;
    if (!XDamageQueryExtension(d, &ev_base, &err_base)) { std::printf("DAMAGE absent\n"); return 2; }
    char selname[64];
    std::snprintf(selname, sizeof selname, "_NET_WM_CM_S%d", scr);
    const bool compositor = XGetSelectionOwner(d, XInternAtom(d, selname, False)) != None;

    XSetWindowAttributes swa{};
    swa.background_pixel = WhitePixel(d, scr);
    const Window w = XCreateWindow(d, root, 80, 80, 240, 180, 0, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel, &swa);
    XStoreName(d, w, "visio-probe2");
    XSelectInput(d, w, ExposureMask);
    XMapRaised(d, w);
    XSync(d, False);

    const Damage dmg_root = XDamageCreate(d, root, XDamageReportNonEmpty);
    const Damage dmg_win  = XDamageCreate(d, w, XDamageReportNonEmpty);

    GC gc = XCreateGC(d, w, 0, nullptr);
    Stat sr, sw2;
    auto t0 = std::chrono::steady_clock::now();

    for (int step = 0; step < 40; ++step) {
        XSetForeground(d, gc, (step % 2) ? WhitePixel(d, scr) : BlackPixel(d, scr));
        XFillRectangle(d, w, gc, 10 + (step * 3) % 180, 10 + (step * 5) % 120, 60, 40);
        XFlush(d);

        for (;;) {
            while (XPending(d)) {
                XEvent e;
                XNextEvent(d, &e);
                if (e.type == ev_base + XDamageNotify) {
                    auto* de = (XDamageNotifyEvent*)&e;
                    const bool est_racine = (de->drawable == root);
                    XserverRegion reg = XFixesCreateRegion(d, nullptr, 0);
                    XDamageSubtract(d, de->damage, None, reg);
                    int n = 0;
                    XRectangle* rs = XFixesFetchRegion(d, reg, &n);
                    if (est_racine) { ++sr.events; accumule(sr, rs, n); }
                    else            { ++sw2.events; accumule(sw2, rs, n); }
                    if (rs) XFree(rs);
                    XFixesDestroyRegion(d, reg);
                }
            }
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();
            if (el > (long)(step + 1) * 40) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    auto dump = [](const char* nom, const Stat& s, int x0, int y0) {
        std::printf("%-8s %3d evenements  %3d rects  aire %ld px", nom, s.events, s.rects, s.area);
        if (s.rects)
            std::printf("  enveloppe (%d,%d)-(%d,%d)", s.minx + x0, s.miny + y0, s.maxx + x0, s.maxy + y0);
        std::printf("\n");
    };

    std::printf("compositeur : %s\n", compositor ? "ACTIF" : "inactif");
    dump("racine", sr, 0, 0);
    dump("fenetre", sw2, 80, 80);
    std::printf("oracle     : la fenetre est en 80,80 240x180 ; le dessin bouge dans 60x40\n");

    XDamageDestroy(d, dmg_win);
    XDamageDestroy(d, dmg_root);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}