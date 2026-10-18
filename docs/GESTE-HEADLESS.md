# Geste headless : ce qui est prouve, et comment

Experience du 18 octobre 2026, sur cette machine, avec Xvfb seul (aucune session, aucune
fenetre reelle, aucun gestionnaire de fenetres, aucun droit root).

## Dispositif

    Xvfb :97 -screen 0 1024x768x24
    ./fenetre_test 100 100          # fenetre 400x300 a 100,100 ; rapporte clics et touches
    visio --taper "Bonjour Krita 42"

## Resultat : le geste arrive

Journal de la fenetre temoin, tel quel :

    fenetre 0x400001 a 100,100
    MOTION x=149 y=99
    CLIC   button=1  x=149 y=99
    TOUCHE (special) TOUCHE B TOUCHE o TOUCHE n TOUCHE j TOUCHE o TOUCHE u TOUCHE r
    TOUCHE   TOUCHE (special) TOUCHE K TOUCHE r TOUCHE i TOUCHE t TOUCHE a TOUCHE
    TOUCHE 4 TOUCHE 2

Coordonnees exactes : pointeur deplace a 250,200, fenetre a 100,100, bordure 1 px
-> evenement recu a 149,99. La chaine « Bonjour Krita 42 » arrive caractere par caractere.
Les deux « (special) » sont les appuis sur Shift, envoyes pour les majuscules : une vraie
touche, donc un vrai evenement.

## Consequences

- **Souris virtuelle : oui.** XTEST, deja dans Xvfb, suffit. Ni `/dev/uinput`, ni root, ni
  appartenance au groupe `input`, ni gestionnaire de fenetres.
- **Clavier : oui.** `visio --taper` envoie des touches ; sous Xvfb sans gestionnaire, le
  focus est `PointerRoot`, donc la fenetre visee est celle qui se trouve sous le pointeur.
- **Piege (faux negatif mesure) :** sans **aucune fenetre cliente**, `xdotool mousemove`
  ne deplace rien et `getmouselocation` reste au centre de l'ecran. Ce n'est pas une
  limitation de Xvfb : c'est l'absence de fenetre. Il faut toujours une cible.

## Ce que cela permet pour LA

Un agent peut, dans une VM Debian minimale, cliquer un bouton et ecrire dans un champ
sans voir un seul pixel : `--cibles` donne la liste des etiquettes et de leurs
identifiants, `--clic <etiquette>` agit, `--taper <texte>` ecrit, `--lire` constate.
