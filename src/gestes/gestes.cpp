// src/gestes/gestes.cpp — le geste, et la remise en place.
#include "gestes/gestes.h"

#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cstdio>

namespace visio {

Contexte::Contexte(Display* d) : d_(d) {
    if (d_) racine_ = DefaultRootWindow(d_);
}

Contexte::~Contexte() {
    if (sauve_) rendre();
}

void Contexte::sauver() {
    if (sauve_ || !d_) return;
    Window enfant = 0, racine_retour = 0;
    XQueryPointer(d_, racine_, &racine_retour, &enfant, &px_, &py_, &dummy_x_, &dummy_y_,
                  &masque_);
    XGetInputFocus(d_, &focus_, &revert_);
    sauve_ = true;
}

void Contexte::pointer(Window w) {
    if (!d_ || w == 0) return;
    sauver();
    // XSetInputFocus directement : on ne LEVE pas la fenetre (pas de XRaiseWindow) et on ne
    // demande rien au gestionnaire de fenetres. Si la fenetre n'est pas visible, l'appel echoue
    // proprement (BadMatch) et le focus ne change pas.
    XSetInputFocus(d_, w, RevertToPointerRoot, CurrentTime);
    XFlush(d_);
}

void Contexte::rendre() {
    if (!sauve_ || !d_) return;
    // Le focus d'abord : remettre le pointeur avant deplacerait la souris d'une fenetre a
    // l'autre sous un gestionnaire en « focus follows mouse ».
    if (focus_ != 0 && focus_ != PointerRoot && focus_ != None) {
        const int rev = (revert_ == RevertToNone || revert_ == RevertToPointerRoot ||
                         revert_ == RevertToParent)
                            ? revert_
                            : RevertToPointerRoot;
        XSetInputFocus(d_, focus_, rev, CurrentTime);
    }
    XWarpPointer(d_, None, racine_, 0, 0, 0, 0, px_, py_);
    XFlush(d_);
    XSync(d_, False);
    // Constat, pas supposition : on relit l'etat apres la remise en place.
    Window enfant = 0, racine_retour = 0;
    XQueryPointer(d_, racine_, &racine_retour, &enfant, &pxa_, &pya_, &dummy_x_, &dummy_y_,
                  &masque_);
    int rev2 = 0;
    XGetInputFocus(d_, &focus_apres_, &rev2);
    sauve_ = false;
}

bool Contexte::faire_le_geste(int x, int y, const std::string& texte, int bouton) {
    if (!d_) return false;
    sauver();
    if (x >= 0 && y >= 0) {
        XTestFakeMotionEvent(d_, -1, x, y, CurrentTime);
        XTestFakeButtonEvent(d_, (unsigned)bouton, True, CurrentTime);
        XTestFakeButtonEvent(d_, (unsigned)bouton, False, CurrentTime);
        XFlush(d_);
        XSync(d_, False);        // laisser le serveur traiter AVANT de remettre le pointeur
    }
    if (!texte.empty()) {
        for (unsigned char ch : texte) {
            KeySym ks = ch < 0x80 ? (KeySym)ch : 0;
            bool maj = false;
            if (ch >= 'A' && ch <= 'Z') { maj = true; }
            else if (ch >= 0x80) { ks = 0; }
            const KeyCode kc = ks ? XKeysymToKeycode(d_, ks) : 0;
            if (!kc) continue;
            const KeyCode sh = maj ? XKeysymToKeycode(d_, XK_Shift_L) : 0;
            if (maj && sh) XTestFakeKeyEvent(d_, sh, True, CurrentTime);
            XTestFakeKeyEvent(d_, kc, True, CurrentTime);
            XTestFakeKeyEvent(d_, kc, False, CurrentTime);
            if (maj && sh) XTestFakeKeyEvent(d_, sh, False, CurrentTime);
            XFlush(d_);
        }
        XSync(d_, False);
    }
    rendre();
    return true;
}

} // namespace visio
