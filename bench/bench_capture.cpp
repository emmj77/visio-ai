// bench/bench_capture.cpp — le banc qui décide de l'architecture.
// Deux voies de capture, mesurées dans les mêmes conditions, avec le diff par tuiles.
//
//   bench_capture shm 60                  plein écran, mémoire partagée
//   bench_capture shm 400 220 200 400 300 région 400x300 en (220,200)
//   bench_capture get 30                  plein écran, XGetImage (copie serveur)
#include "common/rect.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using visio::Rect;

namespace {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

struct Compte {
    double capture = 0, diff = 0;
    int    tuiles_changees = -1;
    int    frames = 0;
};

// Diff par tuiles de 32 sur deux tampons de même largeur/décalage.
int diff_tuiles(const uint8_t* a, const uint8_t* b, int stride, const Rect& r, int tile,
                std::vector<uint32_t>* memoire) {
    (void)memoire;
    int changees = 0;
    for (int y = r.y; y < r.bottom(); y += tile) {
        for (int x = r.x; x < r.right(); x += tile) {
            const int w = std::min(tile, r.right() - x);
            const int h = std::min(tile, r.bottom() - y);
            bool d = false;
            for (int yy = y; yy < y + h && !d; ++yy)
                if (std::memcmp(a + (size_t)yy * stride + (size_t)x * 4,
                                b + (size_t)yy * stride + (size_t)x * 4, (size_t)w * 4) != 0)
                    d = true;
            if (d) ++changees;
        }
    }
    return changees;
}

Compte banc_shm(Display* d, Window root, const Rect& r, int frames) {
    XShmSegmentInfo info{};
    XImage* img = XShmCreateImage(d, DefaultVisual(d, DefaultScreen(d)),
                                  (unsigned)DefaultDepth(d, DefaultScreen(d)), ZPixmap,
                                  nullptr, &info, (unsigned)r.w, (unsigned)r.h);
    if (!img) { std::fprintf(stderr, "XShmCreateImage a echoue\n"); std::exit(3); }
    info.shmid = shmget(IPC_PRIVATE, (size_t)img->bytes_per_line * img->height, IPC_CREAT | 0600);
    info.shmaddr = (char*)shmat(info.shmid, nullptr, 0);
    img->data = info.shmaddr;
    info.readOnly = False;
    if (!XShmAttach(d, &info)) { std::fprintf(stderr, "XShmAttach a echoue\n"); std::exit(4); }
    XSync(d, False);

    std::vector<uint32_t> precedent((size_t)r.w * r.h, 0);
    Compte c;
    for (int f = 0; f < frames; ++f) {
        auto t0 = Clock::now();
        if (!XShmGetImage(d, root, img, r.x, r.y, AllPlanes)) {
            std::fprintf(stderr, "XShmGetImage a echoue\n"); std::exit(5);
        }
        auto t1 = Clock::now();
        int changees = 0;
        if (f > 0) {
            changees = diff_tuiles((const uint8_t*)img->data, (const uint8_t*)precedent.data(),
                                   img->bytes_per_line,
                                   Rect{0, 0, r.w, r.h}, 32, nullptr);
            // rafraîchit le tampon de référence, tuile par tuile
            for (int y = 0; y < r.h; ++y)
                std::memcpy(&precedent[(size_t)y * r.w],
                            img->data + (size_t)y * img->bytes_per_line, (size_t)r.w * 4);
        }
        auto t2 = Clock::now();
        if (f > 0) { c.capture += Ms(t1 - t0).count(); c.diff += Ms(t2 - t1).count(); }
        c.tuiles_changees = changees;
        ++c.frames;
    }
    XShmDetach(d, &info);
    XDestroyImage(img);
    shmdt(info.shmaddr);
    shmctl(info.shmid, IPC_RMID, nullptr);
    return c;
}

Compte banc_get(Display* d, Window root, const Rect& r, int frames) {
    std::vector<uint32_t> precedent((size_t)r.w * r.h, 0);
    std::vector<uint32_t> courant((size_t)r.w * r.h, 0);
    Compte c;
    for (int f = 0; f < frames; ++f) {
        auto t0 = Clock::now();
        XImage* img = XGetImage(d, root, r.x, r.y, (unsigned)r.w, (unsigned)r.h, AllPlanes, ZPixmap);
        auto t1 = Clock::now();
        if (!img) { std::fprintf(stderr, "XGetImage a echoue\n"); std::exit(2); }
        for (int y = 0; y < r.h; ++y)
            std::memcpy(&courant[(size_t)y * r.w],
                        img->data + (size_t)y * img->bytes_per_line, (size_t)r.w * 4);
        XDestroyImage(img);
        int changees = 0;
        if (f > 0) {
            changees = diff_tuiles((const uint8_t*)courant.data(), (const uint8_t*)precedent.data(),
                                   r.w * 4, Rect{0, 0, r.w, r.h}, 32, nullptr);
        }
        auto t2 = Clock::now();
        if (f > 0) { c.capture += Ms(t1 - t0).count(); c.diff += Ms(t2 - t1).count(); }
        c.tuiles_changees = changees;
        precedent.swap(courant);
        ++c.frames;
    }
    return c;
}

void rapport(const char* voie, const Rect& r, const Compte& c) {
    const double n = c.frames > 0 ? c.frames - 1 : 1;
    std::printf("%-4s  %4dx%-4d  %6.2f ms capture  %5.2f ms diff  %6.2f ms total  -> %6.1f im/s"
                "  (%d tuiles changees a la fin)\n",
                voie, r.w, r.h, c.capture / n, c.diff / n, (c.capture + c.diff) / n,
                1000.0 / ((c.capture + c.diff) / n), c.tuiles_changees);
}

} // namespace

int main(int argc, char** argv) {
    const char* voie = argc > 1 ? argv[1] : "shm";
    const int frames = argc > 2 ? std::atoi(argv[2]) : 60;

    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::fprintf(stderr, "pas de display\n"); return 1; }
    const Window root = DefaultRootWindow(d);
    Rect r{0, 0, DisplayWidth(d, DefaultScreen(d)), DisplayHeight(d, DefaultScreen(d))};
    if (argc > 6) r = Rect{std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6])};

    std::printf("region %dx%d en %d,%d — %d frames\n", r.w, r.h, r.x, r.y, frames);
    const Compte c = (std::strcmp(voie, "get") == 0) ? banc_get(d, root, r, frames)
                                                     : banc_shm(d, root, r, frames);
    rapport(voie, r, c);
    XCloseDisplay(d);
    return 0;
}
