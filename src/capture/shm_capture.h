// src/capture/shm_capture.h — lire des pixels par mémoire partagée, sans copie inutile.
#pragma once
#include "common/rect.h"
#include "core/frame.h"

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>

#include <map>
#include <utility>

namespace visio {

// Un tampon XShm par taille demandée, réutilisé d'un appel à l'autre.
// Mesuré : 0,16 ms pour 400x300, contre 4,93 ms pour l'écran entier (M 25/M 26).
class ShmCapture {
public:
    explicit ShmCapture(Display* d);
    ~ShmCapture();

    ShmCapture(const ShmCapture&) = delete;
    ShmCapture& operator=(const ShmCapture&) = delete;

    bool valide() const noexcept { return ok_; }

    // Recopie la région de l'écran dans `out` (coordonnées écran, origine = r.x,r.y).
    bool get(const Rect& r, Frame& out);

    // Écran entier.
    bool get_ecran(Frame& out);

    // Libère les tampons (à appeler avant XCloseDisplay).
    void release() noexcept;

private:
    struct Tampon { XImage* img = nullptr; XShmSegmentInfo info{}; };
    Tampon* tampon_pour(int w, int h);

    Display* d_ = nullptr;
    Window   root_ = None;
    Rect     ecran_{};
    bool     ok_ = false;
    std::map<std::pair<int, int>, Tampon> tampons_;
};

} // namespace visio
