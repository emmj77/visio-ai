# Etat du projet — fin de session du 18 octobre 2026

Ce fichier est le point de reprise. Il dit ce qui marche, ce qui ne marche pas, et par quoi
continuer. Les mesures sont celles du code au commit indique ci-dessous.

## Ce que le logiciel est

Un logiciel qui transforme l'ecran en **API de gestes**. L'agent ne recoit jamais d'image,
jamais de pixel : il recoit une liste de cibles (identifiant, rectangle, etiquette) ou un
**plan ASCII** de la page, puis il agit par identifiant (clic, frappe).

C++20, 3 600 lignes, binaire de 171 Ko, X11 + FreeType, **aucun modele, aucun GPU**.

## Ce qui marche, mesure

    capture              XShmGetImage : 5 ms plein ecran (3600x1080), 0,16 ms pour 400x300
    segmentation         zones par couleur : 0,01-0,03 ms par fenetre
    boites fermees       test topologique sur l'encre : 12-14 ms pour 1880x1040
    plan ASCII           deux couches : encre (forme reelle) + structure (cadres, ids)
    cibles texte         un mot = une cible, 75 a 470 par fenetre, avec identifiant stable
    cibles non textuelles blocs compacts 34x40 (icones, cases), 5-8 ms
    clic                 XTest, prouve en headless (docs/GESTE-HEADLESS.md)
    clavier              XTest, « Bonjour Krita 42 » recu caractere par caractere
    delta                une fenetre inchangee : 0 nouveau, 0 modifie
    cache d'etiquettes   1 177 ms froid -> 13 ms chaud (identifiant porte l'empreinte pixels)

## Les boites fermees (M6-1, M6-2)

Le plan ASCII ne montrait aucun bouton. La cause n'etait pas la lecture : une bordure de
1 px occupe 9 % de son aire, donc `regions_uniformes` (remplissage >= 0,55) la jetait. Le
cadre existait dans les pixels et n'entrait dans aucune lecture.

`boites_fermees()` (src/perceive/regions.cpp) : on lit l'ENCRE — tout pixel qui s'ecarte du
fond —, on noie l'exterieur depuis le bord de la boite de la composante, et ce que l'eau ne
touche pas est un INTERIEUR. Le test est topologique, pas geometrique : un rond et un
triangle passent comme un cadre ; un trait, une croix, une lettre echouent.

Trois choses apprises en le mesurant, chacune avec sa mesure :

- **l'encre, pas la couleur.** Un arc de coin arrondi est antialiase : chaque pixel d'arc a
  sa propre couleur et une lecture par couleur coupe le cadre en quatre morceaux. Page
  temoin, bordure 1 px + `border-radius: 8px` : 0 boite par couleur, 1 par encre.
- **la reunion des segments etait fausse.** L'ancienne boucle a deux pointeurs jetait un
  segment des que son x etait plus a gauche ; des qu'on tolere le contact diagonal (ce
  qu'exige un arc), elle perd des paires. Reecrite en fenetre a avance monotone. La
  connectivite a 8 est reservee a cette passe : `regions_uniformes` est inchangee.
- **le fond passe le test de fermeture.** Il entoure les boites, donc il a de l'encre
  partout. Il est ecarte par sa taille (sa boite couvre la frame), pas par sa forme.

Mesure, page temoin (geometrie connue, rendue par Firefox, 1880x1040) :

    1 px coins carres 240x120      trouve        1 px coins arrondis 240x120   trouve
    2 px 240x80                    trouve        champ 240x32                 trouve
    2 imbriquees 240x120 / 100x60  trouve        rond 122x122                 trouve
                                                  triangle 164x122             trouve
    diagonale, croix, trait, texte, lettre « o »  rejetes
    case a cocher 16x16            hors seuil d'aire (256 px < 600)
    terminal (texte seul)          0 cadre

## Ce qui ne marche pas

    lecture des lettres  18/19 sur temoins a 16 px (rendus a positions ENTIERES, donc plus
                         faciles que la realite), 2/19 a 11 px, et du charabia sur l'ecran
    pages web            zero ligne detectee sur une page sans lignes vides, tout
                         fusionne en une bande que `hauteur_max` rejette
    icones               banque du systeme : 74 % top-1 sur 856 icones connues, 0 % sur une
                         vignette (aucune banque ne contient une vignette)
    distinction          ni case a cocher, ni curseur, ni bouton : tout est « zone sans texte »
    boite trop plate     resolu : un bouton de 32 px de haut ne fait pas 2 cases de plan, il
                         recoit une ligne de « = » et son nom (marqueur, pas cadre)
    bouton a fond teinte resolu : `aplats_bordes()` retrouve la bordure d'un aplat (deux
                         bandes autour du rect : a 1 px le trait, a 4 px la page) et rend un
                         cadre avec la vraie couleur du trait. Mesure : 118x30 trait #a0a0a0
    bande d'outils       la barre d'un navigateur est bien une boite fermee : elle est
                         comptee comme cadre. C'est exact, ca encombre.

## L'echelle du plan

Un caractere de terminal est DEUX fois plus haut que large. Avec des cases carrees en pixels
(10x10), la page sortait etiree en hauteur : le rond de 122x122 px de la page temoin
ressortait en oeuf. Une case doit valoir UN caractere a l'ecran, donc 10 px de large et 20 px
de haut. Le plan garde alors exactement les proportions de l'ecran (188/52 = 3,6 en
caracteres, soit 1,8 une fois la hauteur de caractere corrigee = 1880/1040).

    --ascii       188 x 52    cases de 10x20 px   1 034 jetons (page dense)
    --ascii 5     376 x 104   cases de  5x10 px   4 000 jetons
    --ascii 1    1880 x 104   cases de  1x2  px   ~40 000 jetons (un caractere par pixel)

Jetons MESURES au tokenizer (o200k_base), pas estimes : une grille ASCII est surtout faite
d'ESPACES et un jeton encode des suites d'espaces (30 a 77 caracteres par jeton ici).
Compter « octets / 4 » surestime le cout d'un facteur 12 — erreur payee, corrigee ici.

## Le trait : orientation, et un seul caractere par run

Densite seule, une diagonale de 1,5 px ressortait en chapelet de points et un cote de triangle
en trait double. Deux mecanismes, mesures sur la page temoin (trait reel : 2 a 4 px) :

- **Orientation.** Pour chaque case, les moments de l'encre donnent l'ecart-type perpendiculaire
  a l'axe principal. Sous 3,5 px la case est traversee par un TRAIT : on la dessine alors avec
  son orientation — `-`, `|`, `/`, `\` — calculee dans le repere de la case (10x20 px), donc
  dans l'angle qu'on voit a l'ecran. Un rond devient une suite de `/ | \ -`, une diagonale une
  suite de `/`.
- **Un caractere par run de trait, par ligne.** Un cote a 51 degres traverse 1,3 case par ligne
  (13 px pour une case de 20 px de haut) : il allumait deux caracteres par ligne et ressortait
  double. On regroupe les cases contigues portant un trait non horizontal et on garde celle qui
  porte le plus d'encre. Les traits horizontaux ne sont pas touches (une ligne de `-` continue).
- **Le texte reste en densite** (`:`, `*`) : les futs des glyphes sont aussi des traits, et une
  ligne de texte devenait un hachis de tirets.
- **Bordure d'un cadre** : l'encre du meme trait tombe souvent dans la case voisine, d'ou des
  `||` qui n'existent pas dans la page. Les cases voisines d'un cadre dessine sont blanchies.

Cout : plan 188x52 = 616 jetons, capture + analyse 12 a 31 ms.

## Dessiner une forme depuis son MODELE (M6-9)

Un rond de 122 px dans une case de 10x20 px tient sur 12 caracteres et 6 rangees. Dessine a
partir de la largeur MESUREE de chaque rangee, il sort en rond-point : la corde du haut vaut
80 % du diametre — c'est vrai geometriquement — donc la barre prend toute la place et les cotes
n'ont plus ou s'incliner. Et la pointe d'un triangle mesure deja 5 cases sur sa premiere
rangee : elle sortait en trapeze tronque.

La reponse est de MODELISER puis de dessiner le modele (`src/perceive/forme.{h,cpp}`) :

- `analyser_forme` rend une **ellipse** (centre, demi-axes, erreur moyenne en px) ou un
  **polygone** (coins detectes par un angle > 32 deg, erreur moyenne a la polyligne). Mesures
  sur la page temoin : rond -> ellipse err 0,07 px ; triangle -> polygone 3 sommets err 4,17 px
  (la meme forme en ellipse donne 15,25 px).
- `dessiner_forme` dessine depuis le modele :
  - ellipse : corde par rangee (aucune vibration possible), chapeau canonique
    `.-"""-.` en haut et `` `-...-' `` en bas, cotes par pente exacte ;
  - polygone : bord gauche et bord droit donnes par les aretes a chaque rangee — donc
    SYMETRIE par construction — base en barre `,____.`, pointe forcee en `/\`.

Regles de l'ASCII art appliquees, chacune corrige un defaut vu a l'ecran :

| regle | defaut corrige |
|---|---|
| un cote ne bouge que d'une case par rangee | la ligne se coupe sur une pente de 1,3 case/ligne |
| caractere d'apres la pente en FRACTION de case (0,35) | cote gauche en `/`, cote droit en `|` sur le meme rond |
| position par `floor` (la case qui contient le bord) | arrondi : les deux cotes decales d'une demi-case |
| chapeau plat si la rangee fait >= 50 % de la largeur max | pointe de triangle prise pour un chapeau |
| la pointe d'un polygone est un POINT (`/\`) | sommet ouvert, trapeze tronque |

Pieges trouves en lisant les coordonnees exactes (`VISIO_DEBUG_PLAN=1` imprime chaque caractere
avec sa colonne, `VISIO_DEBUG_FORME=1` le modele et les rangees) :

- `t = (b.y - a.y) > 1e-9 ? ... : 0` laissait `t` a zero sur une arete qui MONTE : un cote du
  triangle restait colle en bas ;
- la rangee du chapeau etait sautee quand son centre tombait hors de l'ellipse : rond sans
  couvercle ;
- un trait d'encre `-` bloquait le caractere du contour (la case n'etait pas « libre ») : la
  pointe perdait un cote ;
- le nettoyage « un caractere par run » tournait APRES les formes et effacait le second
  caractere de `/\` : il appartient a la couche d'encre, il passe avant.

`tests/unit_forme.cpp` juge le trace sur une GRILLE NUE (15 assertions) : sur une capture, on
confond un bug de dessin avec un decalage de lecture de la sortie. `ctest` : 10/10.

## Le diagnostic qui compte

**La comparaison n'est pas le probleme, la decoupe l'est.** Trois essais de comparaison de
formes ont ete mesures et rejetes : binarisation + intersection sur union (13 px : 16/19 ->
4/19), gradients HOG (63 % contre 74 %), et la mesure d'intensites reste la meilleure.

Mais le vrai defaut est ailleurs : on extrait les lettres par composantes connexes a un
seuil, et on les compare a des gabarits rendus avec leurs bords antialiases. **Deux
decoupes differentes, donc deux formes non comparables.** C'est ce qui produit « rm » -> « mm ».

## Par quoi continuer, dans cet ordre

1. ~~**Detecteur de boites fermees**~~ — fait (M6-1, M6-2). Voir plus haut. Restes : la case
   a cocher 16x16 (sous le seuil d'aire ; a traiter avec les lignes de texte pour ne pas
   confondre une lettre fermee et une case), et le bouton de 32 px de haut qui ne fait pas
   deux cases de plan (il faudrait un marqueur d'une ligne, pas un cadre).

2. **Lecture par avances + programmation dynamique.** Sonde deja ecrite :
   `tools/probe_avances.cpp` (entre dans le depot). On avance avec le pas de la police et on
   compare dans la case `[x, x+avance(c))`. La version gloutonne derive (0/16, 1/16, 8/16)
   mais lit exactement « Krita 42 » en fin de ligne : le mecanisme est bon, la recherche ne
   l'est pas. Remplacer le choix glouton par une DP sur les positions, cout normalise par
   l'UNION (sinon un « i » gagne toujours), rejet de la ligne si le cout cumule depasse un
   seuil — ce rejet signifie « mauvaise police », pas « lettre illisible ».

3. **Petit classifieur de lettres** si la DP plafonne : 95 classes, entraine sur du rendu
   FreeType augmente (tailles 8-24, phases sous-pixel, graisses, flous). 20 000 parametres
   suffisent, moins d'une milliseconde CPU. Il classe ; il ne decoupe pas.

## M6-10 — LE MINIMUM : BOITES + ECRITURE (18/10)

Le plan ne dessine plus rien : il rend les boites, leur identifiant et **ce qui est ecrit dedans**.
Le texte vient de tesseract (binaire, `fra+eng`, deja installe sur la machine).

    plan         94x26 cases de 20x40 px  12 cibles  3 avec texte
    cible        id     genre      position        texte
                 15f    bouton      46,411 118x30   Valider
                 2b5    texte      190,91   60x23   Envoyer
                 f1b    texte        6,91  178x23   Moussa Diallo

**Ce que ca remplace, et pourquoi.** Le lecteur maison (FreeType + comparateur de glyphes) rendait
« Va][der » pour « Valider », « Yr <V \L » pour « booeba ». Tesseract rend les deux exactement. Le
comparateur, la palette de glyphes, le squelette, les contours, la couche d'encre : supprimes
(563 + 231 + 323 lignes de code, plus les blocs de `main.cpp` qui les appelaient).

**Mesures qui ont decide de la forme :**

- **Un appel par zone coutait 2 337 ms** pour 10 zones : le demarrage de tesseract domine. Les
  zones sont donc EMPILEES dans une image (bande blanche de 12 px entre elles) et lues en UN appel :
  **564 ms**. Chaque ligne retrouve sa zone par son ordonnee.
- **Filtre de confiance (55)** : sans lui, un rond se lit « PO / NE » et un triangle « aN ». Une
  ligne n'est gardee qu'avec deux lettres ET la confiance du moteur.
- **Le clic et la frappe sont deux evenements separes** : sans attente, la frappe part avant que le
  champ ait le focus et se perd (champ vide). 150 ms apres le clic.
- **3 ms par touche perd des lettres** (8 tapees, 5 arrivees) ; **12 ms** les rend toutes. Mesure
  sur le champ de la page temoin.
- **Ctrl+A avant d'ecrire** : la semantique d'un `input_text` est de REMPLACER le contenu.
- **`--verbose`** : les diagnostics de mesure sont muets par defaut. Ils pesaient 8 ko sur un plan
  de 3 ko, et faisaient depasser le plafond de retour d'un outil (4 ko) — le DESSIN, c'est-a-dire
  les boutons, etait tronque.

**Les deux gestes**, par l'identifiant que le plan affiche (un prefixe suffit) :

    --clic <id>              clic au centre de la cible ; pointeur et focus RENDUS
    --ecrire <id> <texte>    clic dans le champ, Ctrl+A, frappe ; pointeur et focus RENDUS

**Ce que le plan ne fait pas encore bien** : une ligne d'URL de 1870 px se lit « &é > G D
file:///nome/moussa/... » (icones prises pour des lettres) ; le champ d'une seule rangee doit
montrer « [f1b texte] » parce qu'un cadre mangerait sa ligne ; le texte d'une boite plus etroite
que son texte deborde (mesure : « Envoyer » dans 6 cases).

## Commandes

    ./build/visio --win Firefox --plan             LE PLAN : boites + ecriture (defaut 10 px)
    ./build/visio --win Firefox --plan 20          moins fin, 3,4 ko (tient dans un retour d'outil)
    ./build/visio --win Firefox --texte            tout le texte de la fenetre, deux passes
    ./build/visio --win Firefox --clic 15f         cliquer une cible du plan
    ./build/visio --win Firefox --ecrire f1b "x"   ecrire dans un champ du plan
    ./build/visio --win Firefox --boites           les cadres fermes (cote_min, aire_min)
    ./build/visio --win Firefox --dump "x,y,w,h /tmp/z.ppm"   les pixels d'une zone
    ./build/visio --win Firefox --verbose          les diagnostics de mesure
    ./build/visio --taper "bonjour"                frappe au clavier, sans rien lire

    ctest --test-dir build                         9 cibles (dont unit_ocr), 296 assertions

## Les outils de l'agent

Deux plugins Hermes, actives, effectifs a la session suivante :

    ~/.hermes/plugins/visio/    visio_plan(window, pas) | visio_clic(cible) | visio_input_text(cible, texte)
    ~/.hermes/plugins/ocr/      ocr_image(chemin, langue, psm) | ocr_ecran(fenetre, langue)

Et, cote LA (VM_LA), quatre tools au registre, corps dans `config/tools/core/` :

    3263 read_text_on_screen   le texte affiche dans une fenetre (OCR, ~1,2 s)
    3264 see_page              le plan (boites, identifiants, ecriture)
    3265 click_target          cliquer une cible du plan
    3266 type_into             ecrire dans un champ du plan (texte par stdin)

## Commandes

    ./build/visio --win Firefox --ascii            le plan : encre + structure, 1 car = 10 px
    ./build/visio --win Firefox --ascii 20         idem, plus grossier (4x moins de jetons)
    ./build/visio --win Firefox --boites           les formes fermees (cadres, ronds, cases)
    ./build/visio --win Firefox --boites 14,600    idem, avec cote_min,aire_min
    ./build/visio --win Firefox --cibles       les cibles texte (id, rect, etiquette)
    ./build/visio --win Firefox --formes       les cibles sans texte
    ./build/visio --win Firefox --delta        seulement ce qui a change
    ./build/visio --win Firefox --clic Krita   clic sur la cible nommee
    ./build/visio --taper "bonjour"            frappe au clavier, sans rien lire
    ./build/visio --win Firefox --dump "x,y,w,h /tmp/z.ppm"   les pixels d'une zone

    ctest --test-dir build                     9 cibles, 296 assertions unitaires
