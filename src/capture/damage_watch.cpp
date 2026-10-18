#include "capture/damage_watch.h"

#include <algorithm>

namespace visio {

DamageWatch::DamageWatch(Display* d, Window w) : d_(d), w_(w) {
    int err_base = 0;
    if (!XDamageQueryExtension(d_, &ev_base_, &err_base)) {
        ev_base_ = -1;
        return;
    }
    // RawRectangles, et pas NonEmpty : NonEmpty rend l'ENVELOPPE de tout ce qui a bougé
    // pendant la période, donc deux dessins éloignés arrivent en un seul rectangle qui
    // couvre la fenêtre entière. Mesuré : c'est exactement ce qu'on veut éviter.
    dmg_ = XDamageCreate(d_, w_, XDamageReportRawRectangles);
}

void DamageWatch::release() noexcept {
    if (d_ && dmg_ != None) {
        XDamageDestroy(d_, dmg_);
        dmg_ = None;
    }
    d_ = nullptr;
    w_ = None;
}

DamageWatch::~DamageWatch() { release(); }

int DamageWatch::fd() const noexcept { return d_ ? ConnectionNumber(d_) : -1; }

bool DamageWatch::pending() const {
    if (!d_ || dmg_ == None) return false;
    std::vector<Rect> vide;
    return const_cast<DamageWatch*>(this)->drain(vide, 0) > 0 ? true : false;
}

int DamageWatch::drain(std::vector<Rect>& out, int gap) {
    if (!d_ || dmg_ == None || ev_base_ < 0) return 0;

    std::vector<Rect> trouves;
    int n = 0;

    while (XPending(d_)) {
        XEvent e;
        XNextEvent(d_, &e);
        if (e.type != ev_base_ + XDamageNotify) continue;

        auto* de = reinterpret_cast<XDamageNotifyEvent*>(&e);
        if (de->drawable != w_ || de->damage != dmg_) continue;   // pas à nous

        // En mode RawRectangles, la zone exacte est dans `area` (mesuré : 40x30 pour un
        // remplissage 40x30, deux événements distincts pour deux dessins éloignés).
        // La région « parts » est vide dans ce mode : ne pas la demander.
        const Rect r{(int)de->area.x, (int)de->area.y, (int)de->area.width, (int)de->area.height};
        XDamageSubtract(d_, de->damage, None, None);   // efface, sinon l'événement revient
        if (!r.empty()) trouves.push_back(r);
        ++n;
    }

    out = merge_rects(std::move(trouves), gap);
    return n;
}

} // namespace visio
