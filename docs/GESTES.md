# Les gestes : agir sans deranger celui qui travaille

Question posee : un agent doit pouvoir cliquer et taper dans une fenetre SANS prendre le
focus de l'utilisateur. Voici ce que la documentation dit, puis ce qu'on en fait.

## Les trois voies d'injection d'entree sous X11

**1. XSendEvent (evenement synthetique).**
L'evenement part avec le drapeau `send_event = true`. Les toolkits le savent et le JETTENT :

- xterm : `allowSendEvents` vaut `false` par defaut, « meaning they are discarded », et
  l'activer « would create a very large security hole » (manuel xterm) ;
- GTK, SDL, Allegro : verifie le drapeau et ignore (cua-driver, issue #2022, meme diagnostique
  cote WebXR : « synthetic events ») ;

Consequence : un clic envoye par XSendEvent n'arrive souvent PAS, et le programme croit avoir
clique. Faux succes silencieux. Cette voie est donc inutilisable pour agir.

**2. XTest (extension XTEST).**
Injecte « almost as if a cooperative user had moved the pointing device or pressed a key »
(manuel XTest). Pas de drapeau `send_event`, donc aucune application ne le filtre. C'est ce que
fait xdotool, et c'est la seule voie fiable sous X11.

Sa limite, ecrite noir sur blanc dans cua-driver : « XTest delivers to the *focused* window ».
Le serveur X livre la frappe a la fenetre qui a le focus, et `XTestFakeMotionEvent` deplace le
VRAI pointeur. Un geste fiable derange donc toujours un peu.

**3. uinput (peripherique virtuel du noyau).**
`/dev/uinput` permet de creer un clavier/souris virtuels (documentation du noyau, section
uinput) ; les evenements entrent au niveau du noyau, sans drapeau synthetique. Utile pour les
applications qui grabent le pointeur, et pour Wayland. MAIS la limite du focus est la MEME : un
clavier virtuel ecrit dans la fenetre qui a le focus du serveur graphique. uinput ne supprime
pas le vol de focus, il change seulement le niveau ou l'evenement nait.

## Ce qu'on en deduit

Il n'existe pas de « clic fantome » sous X11. La seule voie fiable prend le focus. La reponse
honnete n'est donc pas de le nier, c'est de rendre le geste **REVERSIBLE** :

1. on sauve la position du pointeur et la fenetre au focus (`XQueryPointer`, `XGetInputFocus`) ;
2. on donne le focus a la cible sans la lever (`XSetInputFocus`, pas de `XRaiseWindow`) ;
3. on agit (`XTestFakeMotionEvent`, `XTestFakeButtonEvent`, `XTestFakeKeyEvent`) ;
4. on `XSync` pour que le serveur ait traite ;
5. on remet le pointeur et le focus exactement ou ils etaient.

Duree de la gene : quelques millisecondes, et rien ne reste change. C'est ce que fait
`visio::Contexte` (`src/gestes/gestes.cpp`).

## Regle de conduite

Un geste ne s'execute pas « parce que c'est possible ». Il s'execute quand la demande est
explicite (`--clic`, `--taper`), et jamais pour naviguer a la place de l'utilisateur : ouvrir
une URL, taper dans la barre d'adresse, changer de fenetre — c'est SON clavier.

## Les tests ne touchent pas la session

`Xvfb` (serveur X sans ecran) ou `Xephyr` (serveur X imbrique dans une fenetre) : on lance
l'application et les gestes DANS ce serveur, `DISPLAY=:99`. La session de l'utilisateur ne voit
rien passer. Script : `tests/gestes_xvfb.sh`.

    Xvfb :99 -screen 0 1280x800x24 &
    DISPLAY=:99 xev -geometry 500x400+40+40 &
    DISPLAY=:99 ./build/visio --win "Event Tester" --gestes "100,100,bonjour"

Le test verifie trois choses : les evenements ARRIVENT (journal de xev), le pointeur revient a
sa position, et le focus est rendu a la fenetre d'origine.
