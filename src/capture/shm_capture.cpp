#include "capture/shm_capture.h"

#include <X11/Xutil.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <cstring>

namespace visio {

ShmCapture::ShmCapture(Display* d) : d_(d) {
    if (!d_) return;
    if (!XShmQueryExtension(d_)) return;
    root_ = DefaultRootWindow(d_);
    ecran_ = Rect{0, 0, DisplayWidth(d_, DefaultScreen(d_)), DisplayHeight(d_, DefaultScreen(d_))};
    ok_ = !ecran_.empty();
}

ShmCapture::Tampon* ShmCapture::tampon_pour(int w, int h) {
    const auto cle = std::make_pair(w, h);
    auto it = tampons_.find(cle);
    if (it != tampons_.end()) return &it->second;

    // Piège : XShmCreateImage mémorise l'adresse de la structure info. Si on construit
    // un Tampon local puis qu'on le copie dans la table, l'XImage garde un pointeur vers
    // l'objet mort — d'où BadShmSeg avec un segment 0x0. On construit donc SUR PLACE.
    auto [it2, cree] = tampons_.try_emplace(cle);
    (void)cree;
    Tampon& t = it2->second;
    t.img = XShmCreateImage(d_, DefaultVisual(d_, DefaultScreen(d_)),
                            (unsigned)DefaultDepth(d_, DefaultScreen(d_)), ZPixmap, nullptr,
                            &t.info, (unsigned)w, (unsigned)h);
    if (!t.img) { tampons_.erase(it2); return nullptr; }
    t.info.shmid = shmget(IPC_PRIVATE, (size_t)t.img->bytes_per_line * t.img->height,
                          IPC_CREAT | 0600);
    if (t.info.shmid < 0) {
        XDestroyImage(t.img);
        tampons_.erase(it2);
        return nullptr;
    }
    t.info.shmaddr = (char*)shmat(t.info.shmid, nullptr, 0);
    if (t.info.shmaddr == (char*)-1) {
        shmctl(t.info.shmid, IPC_RMID, nullptr);
        XDestroyImage(t.img);
        tampons_.erase(it2);
        return nullptr;
    }
    t.img->data = t.info.shmaddr;
    t.info.readOnly = False;
    if (!XShmAttach(d_, &t.info)) {
        shmdt(t.info.shmaddr);
        shmctl(t.info.shmid, IPC_RMID, nullptr);
        XDestroyImage(t.img);
        tampons_.erase(it2);
        return nullptr;
    }
    XSync(d_, False);
    return &t;
}

bool ShmCapture::get(const Rect& r, Frame& out) {
    if (!ok_) return false;
    const Rect zone = intersect(r, ecran_);
    if (zone.empty() || zone != r) return false;   // hors écran : on refuse, on ne rogne pas

    Tampon* t = tampon_pour(r.w, r.h);
    if (!t) return false;
    if (!XShmGetImage(d_, root_, t->img, r.x, r.y, AllPlanes)) return false;

    out = Frame::pour(r);
    for (int y = 0; y < r.h; ++y)
        std::memcpy(&out.px[(size_t)y * r.w], t->img->data + (size_t)y * t->img->bytes_per_line,
                    (size_t)r.w * 4);
    return true;
}

bool ShmCapture::get_ecran(Frame& out) { return get(ecran_, out); }

void ShmCapture::release() noexcept {
    for (auto& [cle, t] : tampons_) {
        (void)cle;
        if (!t.img) continue;
        if (d_) {
            XShmDetach(d_, &t.info);
            XDestroyImage(t.img);
        }
        if (t.info.shmaddr) shmdt(t.info.shmaddr);
        if (t.info.shmid >= 0) shmctl(t.info.shmid, IPC_RMID, nullptr);
        t.img = nullptr;
        t.info.shmaddr = nullptr;
    }
    tampons_.clear();
    ok_ = false;
}

ShmCapture::~ShmCapture() { release(); }

} // namespace visio
