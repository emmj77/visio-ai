# Travailler dans ma session, pas dans la tienne

Regle : mes tests ne passent JAMAIS par ta session graphique. Deux raisons, la seconde est la
bonne : un geste fiable sous X11 prend le focus et deplace le vrai pointeur (docs/GESTES.md),
donc un test fait chez toi te vole ton clavier — et tes fenetres, ton focus et ta position de
souris ne doivent pas dependre de ce que je mesure.

## Les options, relevees sur cette machine

| option | presente | ce qu'elle donne | limite |
|---|---|---|---|
| **Xvfb** (`/usr/bin/Xvfb`) | oui | serveur X sans ecran ; session invisible ; demarre en moins de 0,3 s ; Firefox, GTK, xterm y tournent ; GL logiciel (llvmpipe) | pas de gestionnaire de fenetres : les fenetres se placent seules ; pas d'acceleration |
| `Xorg` (`:1` sur un VT libre) | oui | vrai serveur X, GL logiciel, proche du reel | demande une configuration d'ecran virtuelle et un basculement de VT ; peut clignoter l'ecran |
| **Xephyr** | non (`xserver-xephyr`) | serveur X imbrique DANS une fenetre : tu vois mes tests tourner | un paquet a installer |
| **Xpra** | non (`xpra`) | session virtuelle persistante, consultable en SSH ou en HTML, on s'y attache et on s'en detache | paquet a installer ; le plus lourd |
| weston / cage / sway en mode headless | non | Wayland sans ecran | ne sert que pour tester des applications Wayland |
| `x11vnc` | oui | expose MON ecran virtuel en VNC : tu peux regarder si tu veux | rien a installer |
| VM (QEMU) | oui (deja utilise) | isolation totale, noyau separe | lourd, lent a demarrer |

## Ce qui est en place

`tools/session.sh` — la session dediee, sur l'affichage `:99`, taille `1880x1040x24` :

    tools/session.sh start                     # demarre (invisible)
    tools/session.sh status
    tools/session.sh run ./build/visio --win "Boites temoin" --ascii
    tools/session.sh shot /tmp/ecran.png       # copie de MON ecran (import)
    tools/session.sh stop

Firefox y tourne avec son PROPRE profil (`--no-remote --profile /tmp/ff-hermes`), donc il ne
touche ni ton instance (pid 65659) ni ton profil. Mesure du 18/10 : capture 11,1 ms, 23 regions,
plan 188x52 sur la page temoin, ta session inchangee.

## Ce que je ne fais pas

- `xdotool type`, `xdotool key`, `wmctrl -a` sur ton affichage `:0` : ca te prend le clavier.
- Changer de fenetre, ouvrir une URL, taper dans ta barre d'adresse : c'est ton poste.
- Installer Xephyr, Xpra ou un compositeur Wayland sans ton accord.

## Reste ouvert

Le pilotage « sans focus » pour l'usage reel (un agent qui agit pendant que tu travailles) :
la seule voie fiable sous X11 est XTest + remise en place (pointeur et focus rendus), ce que
fait `visio::Contexte` ; `uinput` ne change pas la limite du focus, il la deplace au niveau du
noyau. Mesure a faire : la duree exacte de la gene pendant un clic.
