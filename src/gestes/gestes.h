// src/gestes/gestes.h — agir sur l'ecran sans deranger celui qui l'utilise.
#pragma once

#include <X11/Xlib.h>

#include <string>

namespace visio {

// CE QUE LA DOC DIT, ET QUI DECIDE DE LA CONCEPTION (detail : docs/GESTES.md)
//  - XSendEvent marque l'evenement « send_event = true » : xterm, GTK, SDL, Allegro le
//    JETTENT. Un clic envoye ainsi n'arrive pas et le programme croit avoir clique.
//  - XTest injecte « almost as if a cooperative user » : aucune application ne le filtre,
//    mais le serveur X le livre a la fenetre au FOCUS et deplace le VRAI pointeur.
//  - uinput (peripherique virtuel du noyau) a exactement la meme limite : il ecrit dans le
//    focus. Il ne supprime pas le vol de focus, il change le niveau d'injection.
// Il n'y a donc pas de clic fantome sous X11. La reponse honnete est de rendre le geste
// REVERSIBLE : sauver pointeur et focus, agir, remettre tout en place.
class Contexte {
public:
    explicit Contexte(Display* d);
    ~Contexte();                       // remet pointeur et focus en place s'ils ne le sont pas

    void sauver();
    void rendre();
    bool faire_le_geste(int x, int y, const std::string& texte, int bouton = 1);

    // Donne le focus a une fenetre SANS la lever ni la deplacer.
    void pointer(Window w);

    int  pointeur_x() const { return px_; }
    int  pointeur_y() const { return py_; }
    Window focus_avant() const { return focus_; }
    Window focus_apres() const { return focus_apres_; }
    int  pointeur_x_apres() const { return pxa_; }
    int  pointeur_y_apres() const { return pya_; }

private:
    Display* d_ = nullptr;
    Window   racine_ = 0;
    int      px_ = 0, py_ = 0;         // pointeur avant
    int      pxa_ = 0, pya_ = 0;       // pointeur apres remise en place
    int      dummy_x_ = 0, dummy_y_ = 0;
    unsigned int masque_ = 0;
    Window   focus_ = 0, focus_apres_ = 0;
    int      revert_ = 0;
    bool     sauve_ = false;
};

} // namespace visio
