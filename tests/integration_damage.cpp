// tests/integration_damage.cpp — M1 : une fenêtre bouge, le veilleur doit le dire, et
// seulement pour SA fenêtre. Ce test a besoin d'un serveur X (le vrai, ou Xvfb).
#include "capture/damage_watch.h"
#include "common/rect.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <sys/select.h>

#include <chrono>
#include <cstdio>
#include <vector>

using visio::Rect;

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_pass; }                                                          \
        else { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); ++g_fail; } \
    } while (0)

// Attend un damage pendant au plus `ms`, rend les rectangles.
static std::vector<Rect> attendre(visio::DamageWatch& w, int ms, int gap = 8) {
    std::vector<Rect> out;
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < fin) {
        if (w.drain(out, gap) > 0 && !out.empty()) return out;
        const int fd = w.fd();
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        timeval tv{0, 5000};
        select(fd + 1, &rs, nullptr, nullptr, &tv);
    }
    return out;
}

int main() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::printf("integration_damage : pas de serveur X, test ignore\n"); return 77; }
    const int scr = DefaultScreen(d);
    const Window root = RootWindow(d, scr);

    XSetWindowAttributes swa{};
    swa.background_pixel = WhitePixel(d, scr);
    const Window a = XCreateWindow(d, root, 100, 100, 300, 200, 0, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel, &swa);
    const Window b = XCreateWindow(d, root, 500, 100, 300, 200, 0, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel, &swa);
    XStoreName(d, a, "visio-a");
    XStoreName(d, b, "visio-b");
    XMapWindow(d, a);
    XMapWindow(d, b);
    XSync(d, False);

    // Les veilleurs doivent être détruits avant XCloseDisplay : un Damage ne survit pas
    // à sa connexion (sinon segfault dans XDamageDestroy). D'où la portée explicite.
    visio::DamageWatch wa(d, a), wb(d, b);
    CHECK(wa.valide());
    CHECK(wb.valide());
    CHECK(wa.window() == a);

    GC gc = XCreateGC(d, a, 0, nullptr);

    // 1. un dessin dans la fenetre a : au moins un rectangle, dans ses bornes
    XSetForeground(d, gc, BlackPixel(d, scr));
    XFillRectangle(d, a, gc, 10, 10, 60, 40);
    XFlush(d);
    auto ra = attendre(wa, 500);
    CHECK(!ra.empty());
    for (auto& r : ra) {
        CHECK(r.x >= 0 && r.y >= 0 && r.right() <= 300 && r.bottom() <= 200);
        CHECK(!r.empty());
    }

    // 2. la fenetre b ne doit rien avoir vu pour un dessin dans a
    {
        std::vector<Rect> rb;
        wb.drain(rb, 8);
        // b a pu recevoir la damage de son propre map : on la vide, puis on verifie
        XSetForeground(d, gc, BlackPixel(d, scr));
        XFillRectangle(d, a, gc, 100, 100, 20, 20);
        XFlush(d);
        auto ra2 = attendre(wa, 500);
        CHECK(!ra2.empty());
        std::vector<Rect> rb2;
        wb.drain(rb2, 8);
        CHECK(rb2.empty());
    }

    // 3. deux dessins eloignes dans la meme fenetre : la fusion a gap 0 les separe
    {
        std::vector<Rect> rien;
        wa.drain(rien, 0);
        // deux mises à jour distinctes de l'interface : un Sync entre les deux, comme
        // deux rafraîchissements réels. Sans Sync, le serveur fond les deux en une enveloppe.
        XFillRectangle(d, a, gc, 10, 10, 10, 10);
        XSync(d, False);
        auto r1 = attendre(wa, 500, 0);
        XFillRectangle(d, a, gc, 250, 150, 10, 10);
        XSync(d, False);
        auto r2 = attendre(wa, 500, 0);
        std::vector<Rect> r = r1;
        r.insert(r.end(), r2.begin(), r2.end());
        CHECK(r.size() >= 2);
        CHECK(!r1.empty() && r1[0].x == 10 && r1[0].y == 10 && r1[0].w == 10 && r1[0].h == 10);
        CHECK(!r2.empty() && r2[0].x == 250 && r2[0].y == 150 && r2[0].w == 10 && r2[0].h == 10);
        int64_t aire = 0;
        for (auto& x : r) aire += x.area();
        CHECK(aire >= 200);
    }

    // 4. un dessin dans b est vu par wb, pas par wa
    {
        std::vector<Rect> vide;
        wa.drain(vide, 8);
        XFillRectangle(d, b, gc, 20, 20, 30, 30);
        XFlush(d);
        auto rb = attendre(wb, 500);
        CHECK(!rb.empty());
        std::vector<Rect> reste;
        wa.drain(reste, 8);
        CHECK(reste.empty());
    }

    XFreeGC(d, gc);
    // Avant de détruire les fenêtres ET avant de fermer la connexion : un Damage ne
    // survit ni à son drawable ni à sa Display.
    wa.release();
    wb.release();
    XDestroyWindow(d, a);
    XDestroyWindow(d, b);
    XCloseDisplay(d);
    std::printf("integration_damage : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
