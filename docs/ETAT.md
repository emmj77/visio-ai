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
    plan ASCII           cadres imbriques (genre + identifiant), 94x52 cases pour une fenetre
    cibles texte         un mot = une cible, 75 a 470 par fenetre, avec identifiant stable
    cibles non textuelles blocs compacts 34x40 (icones, cases), 5-8 ms
    clic                 XTest, prouve en headless (docs/GESTE-HEADLESS.md)
    clavier              XTest, « Bonjour Krita 42 » recu caractere par caractere
    delta                une fenetre inchangee : 0 nouveau, 0 modifie
    cache d'etiquettes   1 177 ms froid -> 13 ms chaud (identifiant porte l'empreinte pixels)

## Ce qui ne marche pas

    lecture des lettres  18/19 sur temoins a 16 px (rendus a positions ENTIERES, donc plus
                         faciles que la realite), 2/19 a 11 px, et du charabia sur l'ecran
    pages web            zero ligne detectee : une page n'a pas de lignes vides, tout
                         fusionne en une bande que `hauteur_max` rejette
    icones               banque du systeme : 74 % top-1 sur 856 icones connues, 0 % sur une
                         vignette (aucune banque ne contient une vignette)
    distinction          ni case a cocher, ni curseur, ni bouton : tout est « zone sans texte »

## Le diagnostic qui compte

**La comparaison n'est pas le probleme, la decoupe l'est.** Trois essais de comparaison de
formes ont ete mesures et rejetes : binarisation + intersection sur union (13 px : 16/19 ->
4/19), gradients HOG (63 % contre 74 %), et la mesure d'intensites reste la meilleure.

Mais le vrai defaut est ailleurs : on extrait les lettres par composantes connexes a un
seuil, et on les compare a des gabarits rendus avec leurs bords antialiases. **Deux
decoupes differentes, donc deux formes non comparables.** C'est ce qui produit « rm » -> « mm ».

## Par quoi continuer, dans cet ordre

1. **Detecteur de boites fermees.** Chaque bouton est un PANNEAU. Le plan ASCII doit montrer
   toutes les boites fermees, de la plus grande a la plus petite. Changer `regions_uniformes`
   ou ajouter un detecteur dedie (suivre une bordure continue et verifier qu'elle se referme).

2. **Lecture par avances + programmation dynamique.** Sonde deja ecrite :
   `~/.hermes/cache/scratch/probe_avances.cpp`. On avance avec le pas de la police et on
   compare dans la case `[x, x+avance(c))`. La version gloutonne derive (0/16, 1/16, 8/16)
   mais lit exactement « Krita 42 » en fin de ligne : le mecanisme est bon, la recherche ne
   l'est pas. Remplacer le choix glouton par une DP sur les positions, cout normalise par
   l'UNION (sinon un « i » gagne toujours), rejet de la ligne si le cout cumule depasse un
   seuil — ce rejet signifie « mauvaise police », pas « lettre illisible ».

3. **Petit classifieur de lettres** si la DP plafonne : 95 classes, entraine sur du rendu
   FreeType augmente (tailles 8-24, phases sous-pixel, graisses, flous). 20 000 parametres
   suffisent, moins d'une milliseconde CPU. Il classe ; il ne decoupe pas.

## Commandes

    ./build/visio --win Firefox --ascii        le plan de la page
    ./build/visio --win Firefox --cibles       les cibles texte (id, rect, etiquette)
    ./build/visio --win Firefox --formes       les cibles sans texte
    ./build/visio --win Firefox --delta        seulement ce qui a change
    ./build/visio --win Firefox --clic Krita   clic sur la cible nommee
    ./build/visio --taper "bonjour"            frappe au clavier, sans rien lire
    ./build/visio --win Firefox --dump "x,y,w,h /tmp/z.ppm"   les pixels d'une zone

    ctest --test-dir build                     9 cibles, 16 assertions sur les temoins
