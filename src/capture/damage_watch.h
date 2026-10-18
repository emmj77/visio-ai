// src/capture/damage_watch.h — la seule source de vérité sur « qu'est-ce qui a bougé ».
#pragma once
#include "common/rect.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>

#include <vector>

namespace visio {

// Surveille UNE fenêtre. Sous composition, c'est la seule voie qui localise (mesure M 35) :
// la racine rapporte des écrans entiers (M 34).
class DamageWatch {
public:
    DamageWatch(Display* d, Window w);
    ~DamageWatch();

    DamageWatch(const DamageWatch&) = delete;
    DamageWatch& operator=(const DamageWatch&) = delete;
    DamageWatch(DamageWatch&&) = delete;
    DamageWatch& operator=(DamageWatch&&) = delete;

    Window window() const noexcept { return w_; }
    int    fd() const noexcept;              // pour poll/select
    bool   valide() const noexcept { return dmg_ != None; }
    int    event_base() const noexcept { return ev_base_; }

    // Vide la file des événements déjà arrivés, rend les rectangles fusionnés (gap px).
    // Ne bloque pas. Renvoie le nombre d'événements traités.
    int drain(std::vector<Rect>& out, int gap = 8);

    // Vrai si un événement de damage est en attente (sans le consommer).
    bool pending() const;

    // Détruit la Damage tout de suite. À appeler avant XCloseDisplay : un Damage ne
    // survit pas à sa connexion (sinon XDamageDestroy écrit dans une Display morte).
    void release() noexcept;

private:
    Display* d_ = nullptr;
    Window   w_ = None;
    Damage   dmg_ = None;
    int      ev_base_ = -1;
};

} // namespace visio
