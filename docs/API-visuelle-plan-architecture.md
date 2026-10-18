    Objet            Plan d'architecture du logiciel qui transforme l'écran en API de gestes.
    Source           conversation « Création d'un agent GUI autonome » (31 tours) + mesures du 18/10/2026
    Règle de lecture [Qn] = ce que MOUSSA a dit au tour n ; [M n] = mesure faite sur cette machine ;
                     (Rn) = proposition de l'assistant, jamais une mesure ; sans marque = proposition.
    Statut           plan d'architecture. Le code n'existe pas encore. Les chiffres, eux, existent.

---

# L'API VISUELLE — PLAN D'ARCHITECTURE

## 0. Ce que ce document est

Le document `API-visuelle-de-l-ecran.md` décrit **ce que le logiciel doit être**. Celui-ci décrit
**comment on le construit**, avec les mesures qui tranchent les choix. Il contient : les chiffres
qui décident, les six décisions d'architecture, l'arborescence exacte, le protocole, le plan de
test avec ses seuils, et les jalons découpés en tâches de moins de cinq minutes.

Une règle gouverne tout le reste : **on ne code pas ce qu'on n'a pas mesuré**. Le premier module
écrit sera donc celui qui mesure, et il l'est déjà — les sondes sont dans
`~/.hermes/cache/scratch/probe_shm.cpp` et `probe_capture.cpp`.

---

## 1. Le problème, en une phrase

Un agent doit **manipuler n'importe quel logiciel** — un bouton dans Krita, un champ dans Firefox,
une case dans un site — sans jamais voir de pixels ni connaître de coordonnées, et il doit le faire
**plus vite qu'un humain ne clique**.

Quatre exigences en découlent, et elles sont en tension :

| exigence | conséquence directe |
|---|---|
| le modèle ne voit pas de pixels | la couche doit **nommer** ce qu'elle voit, et exposer des gestes |
| il faut réagir vite | la capture ne peut pas être plein écran à chaque tour |
| il faut que le clic tombe juste | la géométrie est vérifiée **avant** et **après** l'action |
| il faut que ça marche partout | pas de plugin par application ; un socle X11 + des profils |

---

## 2. Les mesures qui décident

Toutes faites sur cette machine le 18/10/2026, dans le langage cible (C++, `g++ -O2`).

### 2.1 La capture : le plein écran est un piège

| # | voie | plein écran 3600x1080 | région 400x300 |
|---|---|---|---|
| M 24 | `XGetImage` (copie par le serveur, allocation à chaque frame) | **24,02 ms** (41,6 im/s) | non mesuré |
| M 25 | `XShmGetImage` (mémoire partagée, tampon réutilisé) | **4,93 ms** (202,8 im/s) | **0,16 ms** (6 219 im/s) |
| M 25 | + diff par tuiles de 32 | 5,90 ms → total **10,83 ms** (92,3 im/s) | 0,09 ms → total **0,25 ms** (4 055 im/s) |

Trois conclusions, et elles fondent l'architecture :

1. **SHM est obligatoire** : 4,9× plus rapide que `XGetImage`. Sans `libxext-dev`, on ne l'a pas
   (mesure M 29).
2. **Le plein écran n'est pas le bon geste** : 4,93 ms à chaque frame, c'est déjà 30 % du budget
   à 60 im/s. Une région de 400x300 coûte **0,16 ms**, soit trente fois moins.
3. **Le diff plein écran est le vrai coût** (5,90 ms pour 3 842 tuiles) : il disparaît si l'on ne
   scanne que ce que X11 signale comme abîmé.

Ces chiffres ont été refaits dans le binaire du projet [M 36] : 6,93 ms plein écran, 0,39 ms pour
400x300, 18,31 ms en `XGetImage`. L'écart avec la première sonde est de 1,4 à 2,4× selon l'état de
la machine (animations, compositeur). **Aucun seuil de performance ne doit être calé sur le
meilleur chiffre** : les seuils de la section 12.3 prennent la mesure la plus lente.

### 2.2 Ce que l'écran impose de toute façon

| # | fait | valeur | conséquence |
|---|---|---|---|
| M 27 | fréquence des dalles | 59,95 Hz (DP-4) et 60,00 Hz (DP-6) | **plafond utile : 60 im/s**. Capter à 200 est du gaspillage pur |
| M 28 | composition | **ACTIVE** : la sélection `_NET_WM_CM_S0` est possédée (gsettings dit `false`, il ment) | les fenêtres sont redirigées ; `DAMAGE` sur la racine ne localise plus rien (M 34) |
| M 8 | dalles | DP-4 1680x1050 +0+0, DP-6 1920x1080 +1680+0 | un seul écran virtuel de 3600x1080 ; l'API doit connaître les deux rectangles |
| M 1 | surface | 3 888 000 px, 14,83 Mo en RGBA | une frame plein écran ne doit jamais être copiée plus d'une fois |

M 28 corrige une erreur que j'avais commise : `xprop -root _NET_WM_CM_S0` ne prouve **rien**, car
`_NET_WM_CM_S0` est une **sélection**, pas une propriété de la racine. Le seul test valide est
`XGetSelectionOwner(...) != None`, et il dit : **compositeur actif**. `gsettings` dit `false` et se
trompe. C'est la première leçon du chantier : une mesure fausse est pire qu'une mesure absente.

### 2.4 DAMAGE : la racine ne localise rien, la fenêtre si

| # | objet surveillé | événements | rectangles | aire | enveloppe |
|---|---|---|---|---|---|
| M 34 | **racine** | 49 pour 40 changements | 49 | **190 512 000 px** = 49 écrans entiers | (0,0)-(3600,1080) |
| M 35 | **fenêtre** animée | 40 | 40 | **136 800 px** (3 420 px par rect) | (80,80)-(320,260), exactement la fenêtre |

Sous composition, la racine est abîmée **en entier** à chaque changement : elle dit « l'écran a
bougé », pas « *ceci* a bougé ». La fenêtre, elle, donne des rectangles utiles — en moyenne 3 420 px,
soit 1 137 fois moins que l'écran.

**Conséquence, et c'est un pivot** : on ne surveille pas la racine, on surveille **une `Damage` par
fenêtre cliente**. Le catalogue et la capture se font ensuite par fenêtre, en coordonnées écran
après translation.

### 2.3 Ce que la machine fournit déjà, et ce qui manque

| # | élément | état | conséquence |
|---|---|---|---|
| M 29 | `libX11.so` + `Xlib.h` | présents | on peut capturer sans rien installer |
| M 29 | `libXext`, `libXdamage`, `libXtst`, `libXi`, `libXfixes` | **runtime présents, `-dev` absents** | SHM, DAMAGE, XTest : **impossible de compiler** en l'état |
| M 29 | `libxcb1-dev` | présent | une variante XCB est possible sans installation |
| M 30 | `xinput create-master` | disponible | un **second pointeur** (MPX) est possible : le curseur de Moussa ne bouge pas |
| M 30 | périphériques | un seul maître (`id=2`) ; tablette XP-Pen en esclave (`id=8`) | attention : ne jamais réattacher la tablette par erreur |
| M 12 | `/dev/uinput` | `crw------- root:root` | un périphérique virtuel exige root (règle udev ou petit helper setuid) |
| M 31 | `at-spi2-core`, accessibilité | **présents et activés** (`screen-reader-enabled = true`, lanceur là) | l'arbre d'accessibilité est une **source d'identifiants sémantiques gratuite** |
| M 32 | `tesseract` | 5.5.0 + `libtesseract5` | l'OCR est disponible **sans rien installer**, en bibliothèque |
| M 6 | portails ScreenCast | aucun | la capture passe par X11, pas par PipeWire |

M 31 est une découverte qui change le plan : Firefox et Chromium **exposent leur arbre DOM via
AT-SPI** quand l'accessibilité est active — et elle l'est. « Le web en API » ne demande donc pas
forcément CDP : le navigateur publie déjà rôles, noms, états et rectangles. C'est la route
principale pour le web, et CDP devient le recours quand l'arbre est muet.

---

## 3. Les six décisions

### D1 — Langage : C++20, sans framework

| option | pour | contre |
|---|---|---|
| **C++20** (recommandé) | en-têtes X11 déjà là, zéro runtime, structures X natives, mesuré ici | gestion mémoire manuelle |
| Rust (`x11rb`) | sûreté, `serde` pour le protocole | seconde chaîne d'outils ; `x11rb` est correct mais tout le SHM/DAMAGE est à réécrire |
| C | minimal | pénible pour le YAML, le JSON, les sockets |
| Python | prototypage | 24 ms par appel `XGetImage` **avant** l'analyse ; le GIL condamne le multi-thread |

Le chemin critique est un `memcpy` de 14 Mo et une boucle de comparaison : le langage compte moins
que la voie de capture. Mais C++ est le seul choix où **rien n'est à installer** pour commencer
(sauf les `-dev` des extensions), et où les trois sondes qui viennent d'être écrites compilent
telles quelles. Les sondes sont le début du produit.

**Bibliothèques** (toutes présentes ou triviales) : `X11`, `Xext` (SHM), `Xdamage`, `Xtst`
(XTest), `Xi` (MPX), `tesseract`/`leptonica` (OCR), `libatspi-2` (accessibilité), `yaml-cpp`
(absente : à installer, ou un parseur maison de 200 lignes pour un sous-ensemble), JSON : écrit à
la main (le protocole est simple, et `nlohmann/json` est un en-tête unique à copier dans `third_party/`).

### D2 — Capture : SHM, à la demande, sur région abîmée

    DAMAGE (par fenêtre) → rectangles abîmés → fusion → capture SHM de la région → tuiles

Une seule `Damage` par fenêtre de premier niveau, créée au map et détruite au unmap (M 35). La
racine n'est surveillée que si `XGetSelectionOwner(_NET_WM_CM_S0) == None` — mode sans compositeur,
où elle localise correctement. Deux modes, détectés au démarrage, jamais supposés :

| mode | composition | source des rectangles | capture |
|---|---|---|---|
| **F** (fenêtré) — l'état actuel | active | `Damage` par fenêtre [M 35] | région de la fenêtre, + position |
| **R** (racine) | inactive | `Damage` sur la racine | région de l'écran |

Le plein écran ne sert plus qu'à l'initialisation et à la resynchronisation, et il coûte 6,93 ms
[M 36] : c'est un geste d'exception, pas un régime.

### D3 — Transport : socket Unix, NDJSON

Pas de HTTP, pas de gRPC, pas de bibliothèque. Une socket `AF_UNIX` en `SOCK_STREAM` dans
`$XDG_RUNTIME_DIR/visio.sock`, une requête JSON par ligne, une réponse par ligne. Un client tient
en quinze lignes de Python — c'est ce que le harnais utilisera. Latence visée : **< 1 ms** par
aller-retour, mesurable avec `perf` si besoin.

### D4 — Concurrence : trois fils, deux files, une règle

| fil | rôle | file d'entrée | politique |
|---|---|---|---|
| capture | attend `DAMAGE`, capture la région, publie une frame | événements X | jamais bloquant |
| perception | diff, rectangles, OCR, catalogue | frames (bornée, 8) | **drop-oldest** : mieux vaut sauter une frame que retarder |
| api | socket, requêtes | clients | **jamais de perte** : une requête attend |

Une frame est publiée avec un compteur de séquence (seqlock) : le fil de perception lit une frame
cohérente ou recommence. Aucun verrou tenu pendant un `XShmGetImage`.

### D5 — Identité des éléments : un identifiant stable, une fenêtre de validité

    id = base32( blake2b( window_id | kind | nom_normalisé | rect_quantifié ) )[:8]

- Le même bouton garde le même `id` tant que sa fenêtre, son type, son nom et sa case (arrondie à
  4 px) ne changent pas.
- Le catalogue porte un `seq` : un `id` n'est valide que pour le `seq` où il a été vu. Un `id`
  périmé est **refusé**, pas réinterprété.
- Pour le web, l'`id` est éphémère par chargement de page : l'AT-SPI donne un nom, pas une
  identité durable, et c'est très bien ainsi.

### D6 — Action : trois niveaux, du plus bête au plus propre

| niveau | moyen | latence | avantage | défaut |
|---|---|---|---|---|
| 0 | `XTest` (motion + bouton + touche) | ~0,3 ms | déterministe, marche partout | **bouge le curseur de Moussa** |
| 1 | MPX : `xinput create-master` + périphérique `uinput` | ~1 ms | second curseur indépendant | support par les toolkits incertain ; `uinput` exige root |
| 2 | API de l'application (Krita → Python ; navigateur → CDP) | 5–50 ms | exact, sans géométrie | un adaptateur par application |

Défaut : **niveau 0**, parce qu'il est vérifiable et qu'il ne dépend d'aucun plugin. Le niveau 1
s'active par configuration ; le niveau 2 est un profil enrichi, pas un cas particulier codé en dur.

**Vérification obligatoire** : après chaque geste, on attend un `DAMAGE` dans le rectangle visé
pendant 150 ms. Pas de damage → `verified: false`, et on **ne réessaie pas** en boucle : on rend
la main au modèle avec l'état observé.

---

## 4. L'architecture

    +-------------+     damage      +-----------+   frames    +-------------+
    |  serveur X  | --------------> |  capture  | ----------> |  perception |
    |   (X11)     | <-------------- |  (SHM)    |             |  diff, rect |
    +-------------+   XShmGetImage  +-----------+             |  OCR, AT-SPI|
          ^                                                    +------+------+
          | XTest / MPX                                               | catalogue
          |                                                           v
    +-----+-------+        socket unix  NDJSON            +-------------+
    |  actionneur | <------------------------------------ |     api     |
    +-------------+                                       +------+------+
                                                                 |  JSONL
                                                                 v
                                                        +------------------+
                                                        | harnais + SLM    |
                                                        +------------------+

### Arborescence

    visio/
      CMakeLists.txt
      README.md
      third_party/nlohmann/json.hpp        (en-tête unique, copié)
      src/main.cpp
      src/common/rect.h                    rect, union, intersection, aire
      src/common/log.h                     journal, niveaux, horodatage
      src/common/json.h                    sérialisation minimale (une fonction par type)
      src/capture/shm_capture.{h,cpp}      XShmCreateImage, attach, get(region)
      src/capture/damage_watch.{h,cpp}     XDamageCreate, XDamageSubtract, fusion de rects
      src/capture/screen_info.{h,cpp}      dalles, dpi, composition, extensions
      src/core/frame.{h,cpp}               région + tampon + seq
      src/core/tile_diff.{h,cpp}           diff par tuiles 32, liste des tuiles changées
      src/core/frame_cache.{h,cpp}         anneau de frames, seqlock
      src/perceive/windows.{h,cpp}         _NET_CLIENT_LIST, géométrie, classe, titre
      src/perceive/regions.{h,cpp}         segmentation par couleur → « quoi est où »
      src/perceive/edges.{h,cpp}           contours → rectangles candidats
      src/perceive/ocr.{h,cpp}             tesseract, cache par empreinte de case
      src/perceive/atspi.{h,cpp}           arbre d'accessibilité → éléments sémantiques
      src/perceive/catalog.{h,cpp}         fusion, dédoublonnage, ids, seq
      src/profiles/profile.{h,cpp}         YAML, ancres, vérification
      src/act/xtest_act.{h,cpp}            souris, clavier, frappe
      src/act/mpx_act.{h,cpp}              maître supplémentaire, uinput
      src/act/verify.{h,cpp}               attente de damage dans un rect
      src/feedback/ascii_grid.{h,cpp}      max-pool, rampe, crop
      src/api/server.{h,cpp}               socket, accept, boucle
      src/api/methods.{h,cpp}              hello, catalog, act, crop, grid, type, key, events
      tests/unit_*.cpp                     un binaire par module
      tests/integration_*.sh               Xvfb + fenêtre témoin
      tests/acceptance_10_gestes.sh        le test qui compte
      bench/bench_capture.cpp              les sondes actuelles, intégrées
      profiles/firefox.yaml, krita.yaml, cinnamon.yaml, libreoffice.yaml

**Pourquoi cette découpe** : chaque module a un test qui l'exécute seul, et le seul module qui
touche au protocole est `api/`. On peut remplacer l'actionneur ou l'OCR sans rien casser ailleurs.

---

## 5. Le protocole

Une requête par ligne, une réponse par ligne. Champs courts : le protocole est lu par un modèle de
1 à 2 B autant que par un humain.

    -> {"m":"hello"}
    <- {"ok":true,"v":"0.1","screen":[3600,1080],"displays":[
          {"n":"DP-4","r":[0,0,1680,1050],"hz":59.95},
          {"n":"DP-6","r":[1680,0,1920,1080],"hz":60.0}],
         "compositor":false}

    -> {"m":"catalog"}                      catalogue courant, complet
    <- {"seq":12,"wins":[{"id":4194308,"app":"krita","title":"sans titre — Krita","r":[0,40,1920,1040]}],
        "els":[{"id":"a3f1c9d2","k":"button","n":"Enregistrer","t":"Enregistrer",
                "r":[120,80,90,28],"w":4194308,"s":["enabled"]}]}

    -> {"m":"catalog","since":12}           seulement les changements
    <- {"seq":13,"add":[...],"del":["b71e0f3a"],"chg":[...]}

    -> {"m":"crop","id":"a3f1c9d2","cols":40,"rows":10}
    <- {"g":"..####....\n..####...."}       grille ASCII, max-pool, rampe par défaut

    -> {"m":"act","id":"a3f1c9d2","do":"click"}
    <- {"ok":true,"verified":true,"ms":11}

    -> {"m":"type","id":"5c2d0a11","text":"bonjour"}
    <- {"ok":true,"verified":true,"ms":23}

    -> {"m":"key","combo":"ctrl+s"}
    <- {"ok":true,"verified":false,"why":"aucun damage dans les 150 ms"}

    -> {"m":"events"}                       flux : damage, changement de fenêtre, focus
    <- {"e":"focus","w":4194308}

Erreurs : toujours `{"ok":false,"err":"<code>","why":"<texte court>"}`. Codes : `stale_id`,
`bad_args`, `no_window`, `locked`, `not_verified`, `unknown_method`. Un `id` périmé est une erreur
**normale** : le modèle en redemande un.

---

## 6. Le catalogue et ce que le modèle en voit

Le catalogue est gros ; le modèle, non. Trois règles :

1. **Filtrer par fenêtre active** : on ne publie que la fenêtre au premier plan, sauf demande.
2. **Un élément = une ligne courte** : `kind`, `nom`, `texte`, `rect`, `état`. Jamais de couleur,
   jamais d'image, jamais de profondeur.
3. **Tronquer par le haut** : si plus de 120 éléments, ne garder que les interactifs (button,
   input, link, menu_item, tab, checkbox), triés par lecture (haut-gauche → bas-droite).

Types reconnus en v1 : `window`, `button`, `input`, `link`, `menu`, `menu_item`, `tab`, `checkbox`,
`radio`, `slider`, `list_item`, `text`, `icon`, `canvas`, `dialog`, `unknown`.

---

## 7. La perception v1, dans l'ordre, avec son budget

| étape | budget | moyen | quand |
|---|---|---|---|
| réveil + capture de la région abîmée | **0,2 ms** | SHM [M 25] | à chaque damage |
| attribution à une fenêtre | 0,1 ms | `XTranslateCoordinates` | à chaque damage |
| ancres de profil (géométrie déclarée) | 0,1 ms | YAML chargé | immédiat |
| rectangles (contours sur la région) | 2–4 ms | Sobel + fusion | sur région > 64x64 |
| OCR des cases nouvelles | 5–20 ms | tesseract, **asynchrone** | cache par empreinte |
| arbre AT-SPI (si la fenêtre expose) | 1–10 ms | `libatspi` | changement de fenêtre ou de page |
| fusion + ids + seq | 0,2 ms | hash | à chaque mise à jour |
| **total perçu par geste** | **≤ 15 ms** | | hors OCR en attente |

L'OCR ne bloque jamais le catalogue : une case sans texte reconnu est publiée avec `t:""`, puis
mise à jour quand l'OCR rend. C'est ce qui permet de tenir 60 im/s sans attendre tesseract.

Ordre de priorité des sources, quand elles se contredisent : **profil > API de l'app > AT-SPI >
géométrie > OCR**. Le profil est écrit par un humain qui a regardé l'application ; il gagne.

---

## 8. L'action, et sa preuve

    act(id):
      1. l'id existe et son seq est le courant           sinon stale_id
      2. la fenêtre de l'élément est toujours active      sinon no_window
      3. l'élément est visible (aucun recouvrement)       sinon on clique le point visible
      4. XTestFakeMotionEvent(x + w/2, y + h/2) ; XTestFakeButtonEvent(1, True/False)
      5. attendre DAMAGE dans rect pendant 150 ms
      6. verified = (damage vu) ; si non, camper l'état observé dans la réponse

Le clic se fait au **centre** de la case, jamais à un point que le modèle aurait donné. C'est la
raison d'être de tout ce document : le modèle choisit **quoi**, le logiciel décide **où**.

Frappe de texte : `XTestFakeKeyEvent` avec `XStringToKeysym`/`XKeysymToKeycode` pour l'ASCII ;
pour les caractères hors clavier (accents directs, caractères composés), on passe par une
modification temporaire de la table de touches — ou, plus simplement en v1, par
`XSendEvent`/presse-papier (`xclip` est absent : à vérifier). À trancher au jalon M4.

---

## 9. Les profils

    # profiles/krita.yaml
    app: krita
    match: {class: "krita"}                 # ou {title_regex: "— Krita$"}
    anchors:
      - {id: toolbar, kind: toolbar, rect: [0, 40, 44, 1040]}
      - {id: canvas,  kind: canvas,  rect: [44, 88, 1780, 960]}
      - {id: brush,   kind: button,  name: "Pinceau", at: {rel: [toolbar, 6, 6, 32, 32]}}
    verify: {after_click: damage_in: [toolbar]}
    forbid: [quit_without_save]             # gestes refusés par le profil

Trois garde-fous, déjà exigés par le document 2 : le profil **déclare** la géométrie, il ne la
devine pas ; chaque geste est **vérifié** ; ce que le profil interdit, le code refuse — ce n'est
pas une consigne au modèle.

---

## 10. Le retour d'image

`crop(id|rect, cols, rows)` : réduction par **maximum** (le minimum de luminosité = l'encre), rampe
`" .:-=#"`, lignes terminées par `\n`. Mesures déjà faites : 80x40 = 422 tokens, 120x60 = 918,
160x80 = 1 631 [M 13] ; la réduction + rendu coûte 9,4 ms pour 80x40 [M 10].

Règle : **on ne renvoie jamais une grille sans demande**. Le modèle la demande quand il doute. Une
grille poussée à chaque tour coûte des tokens et n'apprend rien.

---

## 11. Sécurité et invariants

| invariant | mise en œuvre | test |
|---|---|---|
| un seul écrivain sur la machine | verrou `flock` sur `$XDG_RUNTIME_DIR/visio.lock` | deux instances → la seconde refuse |
| jamais de frappe dans une fenêtre qui n'est pas la cible | vérification du `window_id` juste avant la frappe | test avec changement de focus |
| jamais de mot de passe | un `input` dont l'état AT-SPI dit `password text` est publié sans nom et refusé à l'action | test unitaire |
| jamais de clic hors écran | rect ∩ écran, sinon refus | test unitaire |
| arrêt immédiat | `SIGUSR1` gèle l'actionneur ; méthode `stop` | test d'intégration |
| aucun état caché entre deux tours | le catalogue porte `seq`, tout id périmé est refusé | test unitaire |

---

## 12. Le plan de test, avec ses seuils

### 12.1 Unités (un binaire, aucune dépendance à l'écran)

| test | ce qu'il prouve | seuil |
|---|---|---|
| `tile_diff` sur images synthétiques | 0 tuile changée sur images identiques ; exactement les bonnes sur un rectangle déplacé | exact |
| `rect` union/intersection | 500 cas aléatoires contre une implémentation naïve | exact |
| `catalog` stabilité des ids | même élément → même id sur 100 mises à jour ; case déplacée de 1 px → même id (quantification 4 px) ; case déplacée de 8 px → id différent | exact |
| `profile` parseur | un YAML valide, trois YAML invalides avec message clair | exact |
| `ascii_grid` | une image connue → une grille connue, octet pour octet | exact |
| `json` | aller-retour sur 20 messages du protocole | exact |

### 12.2 Intégration (Xvfb, fenêtre témoin)

| test | moyen | seuil |
|---|---|---|
| capture d'une région connue | fenêtre `xterm` peinte en rouge, capture 100x100, pixels comparés | 100 % identiques |
| damage | déplacer la fenêtre, compter les rectangles | au moins 1, en < 50 ms |
| action | clic sur un bouton Tk, vérifier le damage + l'effet | vérifié en < 150 ms |
| socket | client Python, 1 000 requêtes en boucle | > 5 000 req/s, 0 perte |
| frappe | taper « été » dans une entrée Tk | texte exact |

### 12.3 Bancs (régression de performance, exécutés à chaque jalon)

| banc | seuil d'échec |
|---|---|
| SHM plein écran | > 6,0 ms |
| SHM région 400x300 | > 0,30 ms |
| diff plein écran | > 6,0 ms |
| catalogue de 2 000 éléments | > 2,0 ms |
| grille 80x40 | > 12 ms |

### 12.4 Acceptation — le test qui compte

Dix gestes réels, scriptés, sur deux applications, chronométrés :

| # | geste | critère |
|---|---|---|
| 1 | Firefox : cliquer dans la barre d'URL | le champ est reconnu par AT-SPI, le focus change |
| 2 | Firefox : taper une URL et valider | la page change (damage > 30 % de la fenêtre) |
| 3 | Firefox : cliquer un lien nommé | la navigation a lieu |
| 4 | Firefox : revenir en arrière | la page précédente revient |
| 5 | Firefox : lire le titre de l'onglet | texte exact, via AT-SPI |
| 6 | Krita : ouvrir un document récent | la fenêtre de dessin apparaît |
| 7 | Krita : sélectionner l'outil pinceau | le bouton passe à l'état actif |
| 8 | Krita : choisir une couleur dans le sélecteur | le nuancier change (damage localisé) |
| 9 | Krita : tracer un trait par glisser | le canvas est abîmé le long du trait |
| 10 | Cinnamon : ouvrir le menu, lancer un terminal | la fenêtre terminal apparaît |

Critère global : **10/10 vérifiés**, latence médiane par geste < 40 ms (hors temps de réaction de
l'application), et aucun clic non vérifié accepté comme un succès.

---

## 13. Les jalons

Chaque tâche fait moins de cinq minutes et laisse le dépôt compilable. Commits fréquents.

**État au 18/10/2026, après la coupure de courant** — rien n'a été perdu :

| jalon | état | preuve |
|---|---|---|
| M0 socle | **fait** | `6c059a3` — `./build/visio` lit 3600x1080, profondeur 24, 92 dpi, extensions |
| M1 capture + damage | **fait** | `4b4cfba` — `integration_damage` 14/14, `bench_damage` reproduit M 34/M 35 |
| M2 diff + fenêtres | **fait** | `8c2608a` — 5 tests verts, dont `integration_windows` 27/27 (15/15 sous Xvfb) |
| M3 nommer | **en cours** | segmentation, catalogue, lignes de texte et OCR faits ; reste : atlas de glyphes, profils d'application, banque de formes |
| M4 agir | à faire | |
| M5 protocole | à faire | |
| M6 utile | à faire | |

### M0 — Le socle (une demi-journée)

1. Installer les en-têtes manquants : `libxext-dev libxdamage-dev libxtst-dev libxi-dev
   libxfixes-dev` (et `libatspi2.0-dev` pour plus tard) — **modification système, accord requis**.
2. `git init`, `CMakeLists.txt` minimal, cible `visio` qui affiche la taille de l'écran.
3. Y verser `probe_shm.cpp` sous `bench/bench_capture.cpp`, cible `bench_capture`.
4. Écrire `tests/unit_rect.cpp` (10 assertions) et le faire passer.
5. `common/rect.h` + `common/log.h`.

### M1 — Capturer (un jour)

1. `capture/shm_capture` : `attach`, `get(rect)`, `detach`, avec un test sur 100x100 rouge.
2. `capture/screen_info` : dalles, dpi, composition (`_NET_WM_CM_S0`), extensions.
3. `capture/damage_watch` : `XDamageCreate`, `XDamageSubtract`, fusion de rectangles.
4. Test d'intégration : fenêtre `xterm` déplacée → au moins un rectangle, en moins de 50 ms.
5. Banc `bench_capture` intégré : plein écran et 400x300, seuils de la section 12.3.

### M2 — Voir le changement (une demi-journée)

1. `core/tile_diff` + test exact sur images synthétiques.
2. `core/frame_cache` + test de cohérence (seqlock).
3. `perceive/windows` : `_NET_CLIENT_LIST`, titre, classe, géométrie.

### M3 — Nommer (deux jours)

1. `perceive/edges` : contours → rectangles candidats, test sur une image connue.
2. `profiles/profile` : parseur YAML, ancres, test.
3. `perceive/ocr` : tesseract en bibliothèque, cache par empreinte, test sur une capture réelle.
4. `perceive/catalog` : fusion, ids, seq, test de stabilité.
5. `feedback/ascii_grid` + test octet pour octet.

### M4 — Agir (un jour)

1. `act/xtest_act` : motion, bouton, touche, frappe ASCII.
2. `act/verify` : attente de damage dans un rect, délai 150 ms.
3. Refus : id périmé, fenêtre inactive, rect hors écran — trois tests.
4. Décider la question des accents (table de touches ou presse-papier).

### M5 — Parler (un jour)

1. `api/server` : `AF_UNIX`, une ligne par message, jamais deux réponses mêlées.
2. `api/methods` : `hello`, `catalog`, `since`, `crop`, `act`, `type`, `key`.
3. Client Python de référence (`client/visio.py`, 80 lignes) + test 1 000 requêtes.
4. Branchement du harnais : un geste réel piloté depuis le client.

### M6 — Être utile (trois jours)

1. `perceive/atspi` : arbre d'accessibilité, rôles, noms, états ; Firefox et Krita.
2. Fusion AT-SPI + géométrie + OCR avec priorité de la section 7.
3. Trois profils : firefox, krita, cinnamon.
4. Acceptation 10 gestes ; rapport de latence ; corrections.
5. `README.md` : installation, lancement, protocole, limites connues.

---

## 14. Ce qui peut faire échouer le projet

| risque | probabilité | parade |
|---|---|---|
| la composition change d'état (elle est **active** aujourd'hui) → la source des rectangles change | certaine | deux modes F/R détectés par `XGetSelectionOwner` ; tests d'intégration dans les deux |
| l'OCR est trop lent sur du texte fin (interface à 96 dpi) | moyenne | OCR seulement sur les cases nouvelles, en asynchrone ; profil en priorité |
| l'arbre AT-SPI est muet dans certaines applis (Qt sans greffon) | moyenne | repli géométrie + OCR ; c'est déjà la colonne vertébrale |
| MPX mal supporté par les toolkits | forte | XTest par défaut ; MPX en option |
| `uinput` exige root | certaine | règle udev `TAG+="uaccess"` sur `/dev/uinput`, ou helper setuid minimal |
| deux dalles à dpi différents | faible | `screen_info` publie les dpi ; les profils sont par rectécran |
| le modèle choisit un `id` périmé en boucle | moyenne | `stale_id` est une réponse normale ; on renvoie le catalogue à jour avec l'erreur |

---

## 15. Ce qu'on fait dans les 48 heures

1. **M0** en entier — il débloque tout le reste (les `-dev` sont le seul obstacle technique dur).
2. **M1** : capture et damage. Si le damage ne marche pas comme prévu sous Cinnamon, on le sait en
   une heure, avant d'avoir écrit la perception.
3. **M5.1–M5.3** en parallèle dès que la capture existe : le protocole est indépendant de la
   perception, et il permet de brancher le harnais sur une capture brute immédiatement.

Rien d'autre. La perception (M3) et l'accessibilité (M6) ne commencent qu'une fois la latence de
capture **constatée** dans le binaire final, pas dans une sonde.

---

## 16. Les trois réponses qu'il me faut pour commencer

1. **Langage** : C++20 (recommandé, tout est déjà là) ou Rust ?
2. **Paquets** : j'installe `libxext-dev libxdamage-dev libxtst-dev libxi-dev libxfixes-dev` ?
3. **Dossier** : où créer le projet — `~/Applications/visio` (à côté de Krita) ou ailleurs ?

---

## 17. Ce que Moussa a demandé le 18/10 au soir

Quatre capacités de plus, et une question. Chacune change le plan, aucune ne le contredit.

### 17.1 Deux usages, pas un

| usage | où | ce que ça donne | état mesuré |
|---|---|---|---|
| **bureau réel** | DP-4 + DP-6, compositeur actif | l'agent pilote les applications de Moussa | mode F, damage par fenêtre [M 35] |
| **headless** | `Xvfb` 1920x1080x24 | une application par écran virtuel, format connu, reproductible | **sans compositeur** : 1920x1080, 100 dpi, et **la racine localise** (54 180 px par rectangle au lieu de l'écran entier) [M 37][M 39] |

Le mode headless n'est pas un mode dégradé : c'est le **meilleur** terrain d'exécution, parce que
la résolution est fixe, qu'il n'y a ni notification ni changement de focus imprévu, et que le
damage sur la racine y fonctionne (M 37). C'est là que les outils dédiés (LA, Hermes) tourneront
d'abord : `visio open --headless --screen 1920x1080 firefox --url ...` puis le protocole habituel.

Une application lancée ainsi est **une fenêtre connue** : sa géométrie ne bouge pas, donc les
profils peuvent être écrits une fois et rester vrais.

### 17.2 Lire tout texte : deux voies, une seule tenable

| voie | coût mesuré | ce qu'elle sait faire | ce qu'elle ignore |
|---|---|---|---|
| tesseract en **processus** | **95 à 155 ms** par bandeau [M 38] | n'importe quelle police | le lancement du processus domine : 90 ms pour 12 ms de travail |
| tesseract en **bibliothèque** | **5,6 à 5,7 ms** [M 41], init 87,4 ms une fois | idem, sans le lancement | rien — on déclare les 5 fonctions de l'API C et on lie `libtesseract.so.5` |
| **matching de glyphes** | quelques µs par caractère | le texte des polices dont on a l'atlas | les polices inconnues, le rendu exotic |

Le CLI est hors-jeu pour un bouton ; la bibliothèque est 17 à 28 fois plus rapide [M 41]. Et
`libtesseract-dev` n'est **pas** nécessaire : il tirerait 44 paquets, dont des montées de version de
`libssl3t64`, `libcurl4t64` et `liblzma5`. On déclare les cinq fonctions de l'API C et on lie la
`.so.5` — même méthode que pour `XShm`, qui n'avait pas non plus ses en-têtes au départ.

Deux chantiers, dans cet ordre :

1. **Atlas de glyphes** (ce que Moussa appelle matching de pixel) : on découpe les bandes de texte
   en caractères, on compare chaque caractère aux vignettes connues, par police et par taille. Une
   interface n'utilise que deux ou trois polices : l'atlas se construit une fois, et se complète
   quand un caractère inconnu apparaît. Précision attendue : exacte sur du texte net, et surtout
   **instantanée**, ce qui permet de lire 200 étiquettes sans budget.
2. **tesseract en bibliothèque** comme secours : quand l'atlas ne reconnaît pas (police inconnue,
   texte antialiasé bizarre, rendu d'un site web).

Le rendu du CLI [M 38] est d'ailleurs instructif : à 11, 13 et 16 px, tesseract lit
« Enregistrer sous. » au lieu de « Enregistrer sous… » — 17 caractères sur 19. Les points de
suspension sont un seul glyphe pour un humain, trois pour la machine. Ce genre d'écart doit être
absorbé par la couche, pas par le modèle.

### 17.3 Les menus : une commande, deux clics

    -> {"m":"menu","path":["Fichier","Exporter","PNG"]}
    <- {"ok":true,"verified":true,"ms":184,"steps":3}

Le démon exécute la séquence : clic sur « Fichier » → attente de damage → localisation de
« Exporter » → survol → attente du sous-menu → localisation de « PNG » → clic → vérification.
**Le LLM voit une commande**, pas quatre gestes. C'est exactement le niveau 2 de D6 : une macro
déclarée, vérifiée à chaque étape, qui échoue proprement (`step:2, why:"sous-menu absent"`).

Règle : la macro ne devine jamais. Si l'étape 2 n'apparaît pas dans les 200 ms, elle rend l'échec
avec ce qu'elle a vu, et le modèle décide autrement.

### 17.4 La banque d'objets

Trois descripteurs suffisent pour commencer, et aucun n'a besoin d'un réseau de neurones :

| descripteur | ce qu'il capture | coût |
|---|---|---|
| moments de Hu normalisés (7 nombres) | la forme globale, invariante à l'échelle et à la rotation | ~µs par vignette |
| histogramme de bords (16 directions) | l'orientation des traits : icône, flèche, croix | ~µs |
| empreinte de couleur quantifiée (16 cases) | « ce bouton est bleu », sans dépendre du thème | ~µs |

Une banque de vignettes 32x32 étiquetées (fermer, réduire, menu, dossier, fichier, retour,
rechercher…), interrogée par distance. Elle s'enrichit toute seule : chaque élément qu'un clic a
**confirmé** entre dans la banque avec son étiquette. C'est ce qui rend le système meilleur sur la
machine de Moussa au bout d'une semaine, sans réentraînement.

### 17.5 La spatialisation sans capture d'écran : oui, et c'est déjà le plan

La question de Moussa — « gestion fine de l'écran sans screenshot » — a une réponse mesurée :
une grille 80x40 coûte **422 tokens** [M 13], une image coûte mille fois plus et ne se compare pas.
Ce qu'on envoie au modèle, par ordre de richesse croissante, et **jamais** d'image :

| niveau | contenu | taille mesurée |
|---|---|---|
| 0 | la liste des éléments : id, type, nom, rect | ~25 tokens par élément |
| 1 | les relations : « au-dessous de », « à droite de », distances en px | ~5 tokens par relation |
| 2 | une carte grossière : « barre d'outils à gauche (44 px), canvas au centre » | ~30 tokens |
| 3 | une grille ASCII de la zone douteuse, sur demande | 422 tokens en 80x40 [M 13] |

Le niveau 0 suffit pour cliquer, le 1 pour désambiguïser deux boutons identiques, le 2 pour
s'orienter dans une application inconnue, le 3 pour vérifier un dessin. **Aucun n'est une image.**
C'est le « computer use ultra léger » : pas de modèle visuel, pas de capture transmise, juste de la
géométrie nommée.

### 17.6 Ce que ça ajoute au protocole

| méthode | rôle |
|---|---|
| `menu {path:[...]}` | une commande, N clics vérifiés |
| `text {zone ou id}` | le texte lu par atlas, avec le taux de confiance |
| `find {pattern}` | cherche un élément par nom ou par forme dans la banque |
| `map {app}` | la carte de niveau 2, en texte |
| `open {app, headless?, screen?}` | lance une application dans un écran virtuel connu |

### 17.7 Ce que ça change dans les jalons

| jalon | ajout |
|---|---|
| M3 | atlas de glyphes avant l'OCR ; banque d'objets (Hu + bords + couleur) |
| M4 | la macro `menu`, avec échec par étape |
| M5 | `text`, `find`, `map`, `open` |
| M6 | `libtesseract` en secours, et l'acceptation sur les deux modes (bureau + headless) |

---

## Registre des mesures [M n]

Numérotation commune avec `SLM-tool-use-only.md` et `API-visuelle-de-l-ecran.md`. Faites sur cette
machine le 18/10/2026.

| # | mesure | valeur | moyen |
|---|---|---|---|
| M 24 | `XGetImage` plein écran 3600x1080 | 24,02 ms/frame (41,6 im/s) ; diff 3 842 tuiles 8,44 ms | C++ `-O2`, `-lX11` |
| M 25 | `XShmGetImage` plein écran | 4,93 ms (202,8 im/s) ; + diff 5,90 ms = 10,83 ms (92,3 im/s) | C++, `libXext.so.6` |
| M 26 | `XShmGetImage` région 400x300 | **0,16 ms** ; + diff 0,09 ms = **0,25 ms** (4 055 im/s) | même binaire |
| M 27 | fréquences | DP-4 59,95 Hz, DP-6 60,00 Hz | `xrandr` |
| M 28 | composition | **ACTIVE** : `XGetSelectionOwner(_NET_WM_CM_S0) != None` ; `gsettings compositing-manager` dit `false` et ment ; `xprop -root` ne prouve rien (c'est une sélection) | sonde C++ |
| M 34 | `DAMAGE` sur la racine, compositeur actif | 49 événements = 49 écrans entiers (190 512 000 px), enveloppe (0,0)-(3600,1080) | `XDamageCreate(root)` |
| M 35 | `DAMAGE` sur une fenêtre, compositeur actif | 40 événements, 3 420 px par rectangle, enveloppe = la fenêtre exacte | `XDamageCreate(window)` |
| M 36 | second passage du banc (build du projet) | plein écran 6,93 ms ; région 400×300 **0,39 ms** ; `XGetImage` 18,31 ms | `bench_capture` |
| M 29 | en-têtes | `libX11` + `Xlib.h` là ; `libxext`, `libxdamage`, `libxtst`, `libxi`, `libxfixes` : runtime là, `-dev` absents ; `libxcb1-dev` là | `dpkg`, `ls` |
| M 30 | pointeurs | `xinput create-master` disponible ; un seul maître (id 2) ; tablette XP-Pen esclave (id 8) | `xinput --help`, `xinput list` |
| M 31 | accessibilité | `at-spi2-core` présent, `screen-reader-enabled=true`, lanceur présent ; `python3-pyatspi` absent | `dpkg`, `gsettings` |
| M 32 | OCR | `tesseract` 5.5.0 + `libtesseract5` présents | `tesseract --version` |
| M 33 | x11grab | 150 frames à 100 im/s demandées → 101 im/s tenus | `ffmpeg` |
| M 37 | mode headless | `Xvfb` : 1920x1080, profondeur 24, 100 dpi, **aucune composition**, SHM/DAMAGE/XTest/XI2 présents | `xvfb-run ./build/visio` |
| M 38 | tesseract en processus | **95 à 155 ms** par bandeau de 400x24 (11, 13, 16 px), pour ~12 ms de travail utile ; 17/19 caractères justes | CLI `tesseract -l fra --psm 7`, 5 passes |
| M 39 | `DAMAGE` racine **sans** compositeur (Xvfb) | 40 événements, **2 167 200 px** au total, soit 54 180 px par rectangle — contre 2 073 600 px par rectangle sur le bureau composité | `xvfb-run ./build/bench_damage` |
| M 40 | banc sous Xvfb | 1920x1080 : racine localisée, fenêtre identique au bureau (136 800 px), tests d'intégration 14/14 | `xvfb-run` + `bench_damage` + `integration_damage` |
| M 41 | tesseract en **bibliothèque** | 5,56 / 5,70 / 5,73 ms par bandeau de 400x24 (11, 13, 16 px) ; init 87,4 ms une seule fois ; **17 à 28× plus rapide que le CLI** | `bench_ocr`, lien direct `libtesseract.so.5` |
| M 42 | capture de régime | 4,0 à 7,8 ms pour une région de 1 Mpx (fenêtre entière) ; le premier appel paie l'allocation SHM (10 à 13 ms) | `visio --win Firefox`, `--win "hermes old"` |
| M 43 | segmentation par couleur | **8,9 à 12,0 ms** pour 1 Mpx, 49 à 95 régions ; première version « un nœud par pixel » : 38 ms | `visio --win Firefox` |
| M 44 | capture seule, région 400x300 | 0,16 ms [M 26] — soit 25× moins qu'une fenêtre entière : c'est bien le **rectangle abîmé** qu'il faut capturer, pas la fenêtre | `bench_capture` |
| M 47 | lignes de texte (projection) | **2,0 ms** pour 45 lignes sur une fenêtre de 840x973 ; le fond est trouvé par histogramme, jamais par moyenne | `visio --win … ` |
| M 48 | OCR sur bandeau réel | 250x17 : 27 à 32 ms · 480x17 : 38 à 50 ms · 830x14 : 158 à 210 ms. Le coût suit la **largeur**, pas l'aire : c'est l'analyse de disposition qui paie | `visio --win … --ocr` |
| M 49 | inversion du fond sombre | sur un terminal (fond noir), inverser la bande fait passer 250x17 de 51 à 32 ms ; la précision ne change pas (les glyphes de boîte ne sont pas dans l'alphabet) | `visio --win … --ocr` |
| M 50 | lecture globale d'une fenêtre | une seule capture XShm de 840x973, **44 lignes trouvées en 2,13 ms** ; la lecture des lettres coûte ensuite 1 à 10 ms par ligne. Rien n'est découpé à l'écran : on lit l'image entière | `visio --win … --lire` |
| M 51 | identification de la typo | le lecteur déduit seul (police, taille) des dimensions des lettres : DejaVu Sans 11 et 13 px, Noto Sans 17 px sur les témoins. Coût **36 à 50 ms** pour la première ligne (rendu des tables FreeType), 1 à 10 ms ensuite | `unit_glyph` |
| M 52 | justesse sur témoins | texte de 19 caractères : **18/19 à 16 px**, **16/19 à 13 px**, 2/19 à 11 px (les lettres se soudent, une paire « re » se lit « m »). Le trou de 11 px est mesuré, pas masqué | `unit_glyph` |
| M 53 | texte réel d'un terminal | phrase en proportionnel : « tu devrai? peut etre bien in?pect? le code je voi? que tu a? REMIS du francai? » — le monospace (police d'interface de kitty) reste faux. Les polices grasses sont désormais candidates | `visio --win … --lire` |

| M 57 | seuil de lecture | accepter la lettre la plus proche jusqu'à 0,30 d'écart au lieu de rendre « ? » : même justesse sur les témoins (2/16/18/17/16/17 sur 19) et beaucoup moins de trous sur du texte réel | `unit_glyph` |
| M 56 | cache par densité | un titre en gras de 17 px et du texte courant de 17 px ont la même hauteur : la clé du cache inclut la densité d'encre, sinon le second hérite de la typo du premier | `unit_glyph` |
| M 55 | gras | le gras d'une famille est essayé juste après son régulier (rang par famille puis par graisse) : témoin gras 14 px → **DejaVu Sans Bold 15 px, 16/19** ; monospace gras → **DejaVu Sans Mono Bold 14 px, 17/19** | `unit_glyph` |
| M 54 | chasse fixe | le pas du texte donne la taille sans balayage : témoin monospace 14 px, pas 8 px, **DejaVu Sans Mono identifiée du premier coup**, 17/19. À 12 px le pas n'est plus régulier (lettres soudées) : le détecteur ne voit rien — limite constatée | `unit_glyph` |
| M 45 | catalogue d'éléments | **0,01 à 0,03 ms** pour 13 à 53 éléments ; identifiants stables d'une image à l'autre | `visio --win` |
| M 46 | bruit de glyphes | un terminal donne **53 régions, dont 40 lettres** de 7x11 px ; après filtre, 8 éléments utiles | `visio --win "hermes old"` |
| M 10 | réduction + rendu ASCII | 56 ms (3 tailles) ; 9,4 ms (80x40 seule) | numpy/PIL |
| M 13 | grilles ASCII en tokens | 80x40 = 422 ; 120x60 = 918 ; 160x80 = 1 631 | tokenizer LFM2.5-1.2B |

Ce qui **n'est pas** mesuré, et qui sera mesuré en M1 : le débit réel de `DAMAGE` sous Cinnamon, la
latence d'un `XTestFakeButtonEvent` vu par l'application, et le coût de `libatspi` sur une page
web chargée.
