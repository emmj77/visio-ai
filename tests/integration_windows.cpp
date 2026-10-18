// tests/integration_windows.cpp — M2 : lister les fenêtres, les nommer, les placer.
#include "perceive/windows.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>

using namespace visio;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

int main() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::printf("integration_windows : pas de serveur X, test ignore\n"); return 77; }
    const int scr = DefaultScreen(d);
    const Window root = RootWindow(d, scr);

    XSetWindowAttributes swa{};
    swa.background_pixel = WhitePixel(d, scr);
    const Window a = XCreateWindow(d, root, 140, 160, 300, 200, 0, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel, &swa);
    const Window b = XCreateWindow(d, root, 640, 160, 240, 120, 0, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel, &swa);
    XStoreName(d, a, "visio-fenetre-a");
    XStoreName(d, b, "visio-fenetre-b");
    // Sans WM_CLASS, le gestionnaire de fenêtres n'intègre pas la fenêtre à sa liste
    // (_NET_CLIENT_LIST) : mesuré ici. Une vraie application en a toujours un.
    XClassHint ca{};
    ca.res_name = const_cast<char*>("visio-test");
    ca.res_class = const_cast<char*>("VisioTest");
    XSetClassHint(d, a, &ca);
    XSetClassHint(d, b, &ca);
    XMapWindow(d, a);
    XMapWindow(d, b);
    XSync(d, False);


    // 1. les fenetres que le gestionnaire connait : coherentes entre elles
    {
        auto tout = list_windows(d, true);
        CHECK(!tout.empty());
        for (const auto& w : tout) {
            CHECK(w.id != 0);
            // une fenetre affichee a une geometrie non vide
            if (w.viewable) CHECK(!w.rect.empty());
            // le titre relu par l'acces direct est le meme que dans la liste
            if (!w.title.empty()) CHECK(window_title(d, w.id) == w.title);
            // la geometrie relue est la meme
            if (w.viewable) CHECK(window_rect(d, w.id) == w.rect);
        }
        // une fenetre du dessus, avec un titre : window_at doit la retrouver en son centre
        const WindowInfo* dessus = nullptr;
        for (auto it = tout.rbegin(); it != tout.rend(); ++it)
            if (it->viewable && !it->rect.empty()) { dessus = &*it; break; }
        CHECK(dessus != nullptr);
        if (dessus) {
            const int cx = dessus->rect.x + dessus->rect.w / 2;
            const int cy = dessus->rect.y + dessus->rect.h / 2;
            CHECK(window_at(tout, cx, cy) == dessus->id);
        }
    }

    // 2. une fenetre que NOUS creons : titre et geometrie lisibles directement.
    //    Mesure : muffin n'integre pas a _NET_CLIENT_LIST une fenetre sans gestion
    //    complete, donc on ne l'exige pas ici — on teste la lecture, pas le WM.
    CHECK(window_title(d, a) == "visio-fenetre-a");
    CHECK(window_title(d, b) == "visio-fenetre-b");
    {
        const Rect r = window_rect(d, a);
        CHECK(r.w == 300 && r.h == 200);
        CHECK(r.x >= 0 && r.y >= 0);
    }

    XDestroyWindow(d, a);
    XDestroyWindow(d, b);
    XCloseDisplay(d);
    std::printf("integration_windows : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
