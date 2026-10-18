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
| M3 | nommer : segmentation par couleur, catalogue | en cours |
| M4 | agir : XTest, XI2, gestes | à faire |
| M5 | protocole socket Unix NDJSON | à faire |
| M6 | gestes utiles de bout en bout | à faire |

6 tests, tous verts : `unit_rect`, `unit_merge`, `unit_tile_diff`, `unit_regions`,
`integration_damage`, `integration_windows`.

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
- `libtesseract-dev` inutile : l'API C est stable, on déclare cinq fonctions et on lie
  `libtesseract.so.5`.

## Licence

Pas encore décidée : aucun fichier `LICENSE` pour l'instant.
