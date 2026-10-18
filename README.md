# visio-ai

Transformer l'écran en API de gestes. L'agent ne voit jamais de pixels : la couche
visuelle nomme les objets, et l'agent choisit un geste dans une liste.

Ce dépôt est le logiciel, en C++20 sous X11. La conception est ailleurs, dans quatre
documents (SLM tool-use only, API visuelle, dessin ASCII et Krita, plan d'architecture).

## État

| jalon | contenu | état |
|---|---|---|
| M0 | socle, sonde d'écran, banc de capture | fait |
| M1 | DAMAGE par fenêtre, fusion de rectangles | fait |
| M2 | diff par tuiles, liste des fenêtres clientes | fait |
| M3 | nommer : segmentation, catalogue d'éléments, lignes de texte, OCR | en cours |
| M4 | agir : XTest, XI2, gestes | à faire |
| M5 | protocole socket Unix NDJSON | à faire |
| M6 | gestes utiles de bout en bout | à faire |

9 tests, tous verts : `unit_rect`, `unit_merge`, `unit_tile_diff`, `unit_regions`,
`unit_catalog`, `unit_text_lines`, `integration_damage`, `integration_windows`,
`integration_ocr`.

## Compiler

Dépendances (Debian) : `cmake g++ libx11-dev libxext-dev libxdamage-dev libxfixes-dev
libxtst-dev libxi-dev`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build
```

## Utiliser

```sh
./build/visio                        # ce que l'écran est, et les fenêtres clientes
./build/visio --regions x,y,w,h      # segmente une zone : ce qu'elle contient
./build/visio --win Firefox          # segmente la fenêtre dont le titre contient ce mot
```

Exemple réel, sur une fenêtre Firefox de 1156x882 :

```
fenetre      02a0003e  ... — Mozilla Firefox
zone         fenetre 1156x882 en 1793,92
capture 2e   3.98 ms (tampon deja alloue)
capture      9.98 ms   segmentation 8.91 ms   95 regions retenues
   1  1104x830  @ 1819,115   couleur #fbfafa  remplissage 0.76  697497 px
```

Le mode `--win` va plus loin : il construit le **catalogue** — la liste nommée de ce qui
est cliquable, avec un identifiant stable.

```
catalogue    13 elements en 0.01 ms (3 glyphes ecartes)
  d666072b63e6d18a fenetre   800x882  @ 2149,92   #000000
  4bb02ddcb05cf512 panneau   350x421  @ 1793,92   #24485c
  aba129a85f3d7847 champ      38x46   @ 1793,213  #3178ca
```

Puis les **lignes de texte** : le catalogue écarte les glyphes, cette couche les regroupe
par bandeaux (projection horizontale du non-fond), et `--ocr` les lit.

```sh
./build/visio --win "hermes old" --ocr
```

```
texte        45 lignes en 2.03 ms (fond #000000)
   249x17  @   22,78    32.3 ms  « : À preparing execute code. »
   482x17  @   22,96    49.6 ms  « . À exec from hermes tools import terminal 6.4s »
```

Deux filtres, appris en exécution : un filet de 1 à 4 px est un séparateur, pas une
cible ; une région de 7x11 est une lettre — sur un terminal, sans filtre, 53 éléments
dont 40 lettres. Et le fond de fenêtre n'est pas répété en panneau.

## Bancs

```sh
./build/bench_capture shm 60                  # capture plein écran
./build/bench_capture shm 400 220 200 400 300 # capture d'une région
./build/bench_damage                          # DAMAGE racine vs par fenêtre
./build/bench_ocr 20                          # tesseract en bibliothèque
```

## Mesures qui ont décidé du design

| mesure | valeur | moyen |
|---|---|---|
| `XGetImage` plein écran | 24,02 ms | `bench_capture get` |
| `XShmGetImage` plein écran | 4,93 ms | `bench_capture shm` |
| `XShmGetImage` 400x300 | 0,16 ms | `bench_capture shm 400 …` |
| DAMAGE racine sous compositeur | rectangles = écrans entiers | `bench_damage` |
| DAMAGE par fenêtre | rectangles exacts (3 420 px) | `bench_damage` |
| DAMAGE racine sans compositeur (Xvfb) | 54 180 px par rectangle | `bench_damage` |
| segmentation par couleur, 1 Mpx | 8,9 à 12,0 ms | `visio --win` |
| catalogue (50 éléments) | 0,01 à 0,03 ms | `visio --win` |
| lignes de texte (45 lignes) | 2,0 ms | `visio --win` |
| OCR par bandeau | 27 à 50 ms (250 à 480 px de large) | `visio --win --ocr` |
| tesseract, bandeau 400x25 | 5,56 ms (contre 95-155 ms en CLI) | `bench_ocr` |

Deux conséquences inscrites dans le code :

- sous compositeur, DAMAGE sur la racine ne localise rien ; on prend un `Damage` par
  fenêtre de premier niveau, au niveau `XDamageReportRawRectangles`, et on lit `de->area`.
  `XDamageReportNonEmpty` ne rend qu'une enveloppe.
- on capture le rectangle abîmé, jamais la fenêtre : 0,16 ms contre 6 ms.

## Pièges rencontrés

- Un `Damage` ne survit ni à son drawable ni à sa `Display` : libérer avant `XCloseDisplay`.
- `XShmCreateImage` garde l'**adresse** de son `XShmSegmentInfo`. Copier la structure
  après coup laisse un pointeur mort → `BadShmSeg` avec un segment `0x0`.
- `_NET_WM_CM_S0` est une sélection, pas une propriété de racine. `xprop -root` ne prouve
  rien et `gsettings` ment : seul `XGetSelectionOwner` répond.
- `libtesseract-dev` inutile : l'API C est stable, on déclare six fonctions et on lie
  `libtesseract.so.5` (chargé par `dlopen`, le binaire tourne sans lui).
- On ne moyenne **jamais** des couleurs empaquetées : sommer des ARGB puis diviser fait
  déborder le canal rouge sur le vert et le vert sur le bleu. Un fond blanc rendait
  `#fffe17`. On somme `r`, `g`, `b` séparément.
- `TessBaseAPISetPageSegMode` doit être appelé **après** `TessBaseAPIInit3`, sinon
  l'initialisation remet PSM 3.

## Licence

MIT — voir `LICENSE`. Copyright (c) 2026 Moussa.
