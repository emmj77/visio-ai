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
    bouton a fond teinte un bouton dont le fond differe du fond de page est une REGION pleine,
                         pas un cadre (il est dans le plan, mais comme aplat)
    bande d'outils       la barre d'un navigateur est bien une boite fermee : elle est
                         comptee comme cadre. C'est exact, ca encombre.

## Le pas du plan

`--ascii [pas]` : la taille d'une case en pixels. Mesure sur une fenetre 1880x1040.

    pas 20    94x52 cases     8 146 octets   ~2 000 jetons   un bouton 240x120 fait 12x6
    pas 10   188x104 cases   22 969 octets   ~6 000 jetons   defaut : 1 caractere = 10 px
    pas  5   376x208 cases   81 935 octets  ~20 000 jetons
    pas  2   940x520 cases  493 464 octets ~125 000 jetons
    pas  1  1880x1040       1 961 426 octets ~500 000 jetons  impossible (fenetre de contexte)

Un caractere par pixel n'est donc PAS gratuit : c'est 500 000 jetons pour une seule capture.
10 px par case est le plus fin qui reste negligeable, et il suffit a voir un rond, un
triangle et une diagonale a main levee.

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
