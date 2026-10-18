# L'API VISUELLE

## Un démon local qui traduit l'écran en gestes nommés — et l'agent qui pilote n'importe quel logiciel sans voir un pixel

    Source           conversation « Création d'un agent GUI autonome »
                     share.gemini.google/FQ6pM3woQzpj — 31 échanges
    Règle de lecture [Qn] = ce que MOUSSA a dit au tour n : cela engage le projet.
                     (Rn) = ce que l'assistant a répondu : une proposition, jamais une mesure.
                     [M x] = mesure faite sur cette machine le 18/10/2026.
    Statut           document de conception. Rien n'est codé.
    Suite            le plan d'architecture et les mesures de capture sont dans
                     API-visuelle-plan-architecture.md (meme dossier).

---

## 0. La thèse

Un agent textuel ne doit **jamais** voir des pixels : ni une image, ni une grille géante, ni des
coordonnées. Il doit voir **une liste de gestes possibles**, et en choisir un.

Le logiciel décrit ici se place entre les deux. Il lit l'écran à haute fréquence, ne remarque que
ce qui change, nomme les objets (un bouton, un champ, un lien), puis expose tout cela comme une
**API de gestes** : `click_element(id)`, `fill_input(id, texte)`, `draw_line(x1,y1,x2,y2)`. L'agent
appelle, le logiciel clique — à la bonne coordonnée, sur le bon écran, dans la bonne fenêtre.

L'écran redevient ce qu'il n'aurait jamais dû cesser d'être : **une API** [Q15][Q16].

---

## 1. Le problème à résoudre, en chiffres

    [M 1]  écran total        3600 x 1080 = 3 888 000 pixels (deux dalles côte à côte)
    [M 2]  une frame RGBA     3 888 000 x 4 octets = 14,83 Mo
    [M 3]  à 10 im/s          0,16 Go/s   |   à 100 im/s : 1,56 Go/s

Une grille textuelle de l'écran ? La conversation en rêve à 400 x 200 [Q8], et l'assistant
annonce « 80 000 tokens » (R8). Mesuré pour de vrai, avec le tokenizer d'un modèle local
(LFM2.5-1.2B) :

| grille 400 x 200 | caractères | tokens réels | remarque |
|---|---|---|---|
| bruit (chaque cellule différente) | 80 199 | **59 326** | 1,35 car/token |
| écran réaliste (grandes plages uniformes) | 80 199 | **20 989** | 3,82 car/token : le tokenizer fusionne les plages |

**Conclusion [M 4]** : même dans le meilleur cas, une grille coûte 21 000 à 59 000 tokens *par
tour*. Ce n'est pas un plan de travail, c'est un budget brûlé. La grille n'est donc **pas** le
canal principal — elle survit comme *retour d'image* compressé (section 8) et comme *micro-patch*
(section 6.4), jamais comme état complet renvoyé à chaque tour.

---

## 2. L'architecture en six couches

    ┌─ 1. CAPTURER ───────────────────────────────────────────────┐
    │  X11 DAMAGE / Composite / SHM, ou PipeWire, ou hook GL/VK    │
    └───────────────────────┬─────────────────────────────────────┘
                            ▼  (frames, 10 à 100 par seconde)
    ┌─ 2. DIFFUSER ───────────────────────────────────────────────┐
    │  ne garder que les tuiles qui ont changé (seuil, GPU)        │
    └───────────────────────┬─────────────────────────────────────┘
                            ▼  (rectangles de changement)
    ┌─ 3. NOMMER ─────────────────────────────────────────────────┐
    │  formes + OCR + arbre d'accessibilité + DOM de la page       │
    └───────────────────────┬─────────────────────────────────────┘
                            ▼  (objets : bouton, champ, lien, canvas)
    ┌─ 4. EXPOSER ────────────────────────────────────────────────┐
    │  catalogue d'éléments = outils (id, type, texte, action)     │
    └───────────────────────┬─────────────────────────────────────┘
                            ▼  (appel d'outil : id + arguments)
    ┌─ 5. AGIR ───────────────────────────────────────────────────┐
    │  uinput (deuxième souris) / API native / raccourci / clic    │
    └───────────────────────┬─────────────────────────────────────┘
                            ▼
    ┌─ 6. RENDRE COMPTE ──────────────────────────────────────────┐
    │  grille ASCII compressée, ou état d'objet, ou rien           │
    └─────────────────────────────────────────────────────────────┘

Les couches 1 et 2 sont du système. Les couches 3 et 6 sont de la perception. Les couches 4 et 5
sont **l'API** — c'est là que se joue le projet, et c'est la partie que le reste sert.

---

## 3. Couche 1 — capturer

### 3.1 Ce que la conversation suppose

Lire « directement la VRAM », le framebuffer brut, avec un pointeur [Q4][Q5]. C'est l'idée la plus
radicale, et il faut être exact sur ce point : **aucune API ne donne à un processus utilisateur un
pointeur sur le tampon d'affichage**. Le noyau et le pilote propriétaire ne l'exposent pas ; ce qui
est exposé, ce sont des **surfaces partagées** (DMA-BUF) ou des pixmaps de fenêtre. Ce n'est pas
une limite de volonté, c'est la conception de la pile graphique.

Donc la « lecture directe » existe bien, mais par des portes prévues :

| porte | ce qu'elle donne | latence | ici |
|---|---|---|---|
| **X11 : DAMAGE + Composite + MIT-SHM** | rectangles exacts qui ont changé, et le contenu par `XShmGetImage` | **0,16 ms** (région 400x300) ; 4,93 ms (plein écran) [M 25][M 26] | **mesurée en C++** [M 5][M 28] |
| **PipeWire / portail ScreenCast** | frames DMA-BUF, la voie « légale » sous Wayland | ~une frame | pipewire présent, portail ScreenCast absent des portails installés [M 6] |
| **hook OpenGL / Vulkan** (`glSwapBuffers`, `vkQueuePresentKHR`) | la surface **d'une application** au moment du swap | ~0 | utile par application (canvas d'un logiciel), pas pour tout le bureau |
| **NVFBC** (NVIDIA) | capture plein écran côté pilote | faible | à tester sur le pilote 550 [M 7] |
| **DRM/KMS** (`/dev/dri/card*`) | les tampons scannés, mais il faut être **maître DRM** | — | indisponible pendant que X tourne |

### 3.2 Ce que dit cette machine

    [M 5]  extensions présentes : DAMAGE (opcode 143), Composite (142), MIT-SHM (130),
           profondeur 24
    [M 18] session X11, bureau Cinnamon
    [M 8]  deux dalles : DP-4 1680x1050 +0+0, DP-6 1920x1080 +1680+0
    [M 7]  pilote NVIDIA 550.163.01 sur les deux cartes
    [M 6]  pipewire et pw-cli présents ; ffmpeg, xdotool, wmctrl présents
           mais AUCUN portail n'implémente ScreenCast (le portail xapp fournit
           Screenshot et Wallpaper) -> la voie PipeWire n'est pas branchée ici

**Décision qui découle de la mesure** : sous X11 avec DAMAGE, on obtient **gratuitement** ce que
la couche 2 doit calculer ailleurs — X sait déjà quels rectangles ont changé. La voie 1 du projet
est donc : *XDamage pour l'événement, XShmGetImage pour le contenu de la zone*. C'est la moins
coûteuse et la seule qui soit déjà ouverte sur cette machine.

**Piège mesuré** : « l'écran » n'est pas un rectangle. Les deux dalles n'ont ni la même taille ni
la même densité (1680x1050 et 1920x1080) ; la boîte englobante fait 3600x1080 avec un trou de
1680x30 en bas à gauche. Une grille unique « au format 16/9 » [Q8] n'a donc pas de sens ici : il
faut **une grille par dalle**, chacune avec son origine.

---

## 4. Couche 2 — ne remonter que le changement

Le principe est posé dès le début [Q3][Q5] : comparer deux frames et ne renvoyer que le delta.

| élément | choix | pourquoi |
|---|---|---|
| granularité | tuiles de 32 x 32 → **[M 9] 3842 tuiles** sur 3600x1080 | on raisonne en blocs, pas en pixels |
| calcul | `abs(frame N+1 - frame N) > seuil`, vectorisé sur GPU | une boucle Python par pixel est disqualifiée [Q5] |
| seuil | quelques unités par canal | ignore l'anti-aliasing et le bruit du curseur |
| sortie | liste de rectangles + leur contenu | ce que la couche 3 consomme |
| coût vérifié | 3 résolutions réduites + rendu ASCII : **56 ms** [M 10] | le pipeline de réduction n'est pas le goulot |

À 10 im/s, lire 15 Mo par frame n'est pas gratuit mais reste tenable (0,16 Go/s) ; à 100 im/s
(1,56 Go/s) il faut que la comparaison reste sur la carte et que le CPU ne voie **que** les
rectangles. C'est exactement la raison d'être de XDamage : le serveur X fait ce travail.

---

## 5. Couche 3 — nommer les objets

Ce que la conversation demande, très précisément [Q13] : reconnaître qu'un ensemble de pixels
**forme un carré**, que quatre coins délimitent une zone, qu'**il y a du texte dedans**, et que
« c'est un bouton ». Puis, à partir de là, fournir non pas des coordonnées mais **des objets**.

Trois sources à croiser, et c'est le croisement qui fait la fiabilité :

| source | ce qu'elle donne | faiblesse seule |
|---|---|---|
| **pixels** (formes, contours) | la géométrie réelle, ce qui est visible | ne sait pas ce que fait l'objet |
| **OCR** | le libellé exact (« Valider ») | ne sait pas si c'est cliquable |
| **arbre d'accessibilité / DOM** | le rôle, l'état, la cible de l'action | peut mentir (widgets dessinés à la main), ou être absent |

Le croisement répond aux deux questions de Moussa [Q14] : **qu'est-ce qui est écrit dessus**, et
**qu'est-ce que ça fait** (un lien mène à telle page ; un bouton déclenche telle action). Sans la
seconde, l'agent clique à l'aveugle ; avec elle, il choisit.

---

## 6. Couche 4 — exposer une API

### 6.1 Le catalogue

Ce que le logiciel remet à l'agent n'est pas un écran, c'est **une liste d'actions possibles** :

    [
      {"id": "btn_submit",   "type": "button", "text": "Envoyer le formulaire",
       "action_target": "POST /api/submit",   "coords": [450, 600]},
      {"id": "link_settings", "type": "link",  "text": "Paramètres du compte",
       "action_target": "/settings.html",     "coords": [820, 30]}
    ]

`coords` **n'est pas pour l'agent** : c'est la table de correspondance du logiciel. L'agent ne
reçoit que `id`, `type`, `text` et éventuellement `action_target`.

### 6.2 L'appel

    {"name": "tool", "arguments": {"id": 512, "args": {"element_id": "btn_submit"}}}

Le logiciel regarde sa table : `btn_submit` → `[450, 600]` → clic. **Pour l'agent, il n'y a jamais
eu de coordonnée** [Q16] : il a demandé « envoyer le formulaire », le logiciel a cliqué.

### 6.3 Pourquoi c'est la seule forme tenable

| sans catalogue | avec catalogue |
|---|---|
| l'agent devine une position sur une image | il nomme un objet |
| une erreur de 20 px = clic à côté | l'objet est résolu par le logiciel |
| le prompt porte une image ou 20 000 tokens | quelques dizaines de lignes |
| le modèle doit voir | le modèle peut être **aveugle** — donc petit, donc rapide |

C'est ce qui rend le second document (le SLM 1–2 B) possible : un modèle sans vision peut piloter
n'importe quel logiciel, **à condition que quelqu'un d'autre ait des yeux**.

### 6.4 Si l'on tient à la grille

Elle reste possible comme **canal secondaire**, avec deux garde-fous mesurés :

1. **jamais l'état complet à chaque tour** — seulement le micro-patch : « ligne 120, colonnes
   50-60 : de `.` à `B` » [Q9] ;
2. **pooling par maximum, jamais par moyenne** [M 11] : sur un canvas 3508x2480 réduit à 80x40,
   une cellule vaut 44x62 px ; un trait de 3 px n'y pèse que 4,8 % → invisible en moyenne,
   **visible en maximum**. Mesuré aux trois résolutions testées (80x40, 120x60, 160x80).

---

## 7. Couche 5 — agir

### 7.1 La deuxième souris

Idée de départ, et elle est excellente [Q1] : un périphérique d'entrée **dédié à l'agent**, pour
qu'il ne se batte pas avec la main de Moussa. `uinput` crée un vrai périphérique dans
`/dev/input/`, reconnu par le serveur graphique comme une souris physique de plus.

    [M 12]  /dev/uinput existe mais est en crw------- root:root
            → une règle udev (ou un groupe) est nécessaire pour que l'agent y écrive

Deux propriétés à retenir :

- **on téléporte, on ne glisse pas** : événements absolus, la cible est atteinte sans trajet
  [Q1] — pas de « main humaine » simulée, donc pas de latence ni de trajectoire à rater ;
- **deux curseurs coexistent** : celui de Moussa et celui de l'agent, sans interférence.

### 7.2 La hiérarchie de préférence

Le clic est le dernier recours, pas le premier. Pour chaque geste, dans cet ordre :

| ordre | moyen | quand |
|---|---|---|
| 1 | **API native** du logiciel (script, plugin, CLI) | quand elle existe — c'est déterministe et sans écran |
| 2 | **raccourci clavier** | quand le logiciel l'expose |
| 3 | **profil de disposition** (section 9) | quand la position est stable |
| 4 | **clic par coordonnée** | le cas général, le plus fragile |

Cette hiérarchie n'est pas dans la conversation ; elle découle de sa propre logique — et le
document suivant (dessin et Krita) en fait la démonstration.

---

## 8. Couche 6 — rendre compte

L'agent doit pouvoir **vérifier** ce qu'il a fait, sans recevoir une image. Trois formats, du plus
riche au plus pauvre :

| format | taille | usage |
|---|---|---|
| grille ASCII 80x40, rampe de 6 niveaux | **3239 caractères, 422 tokens** [M 13] | vue d'ensemble : le dessin est-il fermé, centré, présent |
| grille 120x60 | 7259 caractères, **918 tokens** [M 13] | quand 80 colonnes ne suffisent pas |
| état d'objet (« bouton X présent, texte = … ») | une ligne | vérifier un clic, une navigation |

Le coût est donc **modeste** : 422 tokens pour voir un canvas entier. C'est ce qui rend la boucle
« agir → regarder → corriger » possible à chaque tour, sans noyer le contexte.

**Ce que la grille ne sait pas faire** : juger une couleur (la rampe est en luminance), ni une
épaisseur de trait. Une grille est une carte de présence, pas une image.

---

## 9. Les profils par logiciel : le CLI figé

Idée centrale de la conversation [Q15] : un logiciel lourd (**Krita** est l'exemple donné) est
lancé **avec une disposition enregistrée** — panneaux, outils, calques à des endroits connus. À
cette disposition correspond un **fichier de profil** : chaque entrée est un geste nommé et sa
cible.

    profil krita.yaml
      - id: outil_pinceau        cible: bouton   pos: [120, 45]
      - id: outil_forme_rect     cible: bouton   pos: [120, 78]
      - id: panneau_calques      cible: panneau  pos: [1500, 200]
      - id: curseur_taille       cible: slider   pos: [300, 60]

L'agent ne voit que `outil_forme_rect` ; le logiciel sait que c'est à `[120, 78]`. Il peut même
commander **en aveugle**, sans regarder l'écran [Q15].

**Le contrat d'un profil, et c'est là qu'il vit ou meurt** : un profil n'est valable que si la
disposition est **réellement** celle qu'il décrit. Trois garde-fous obligatoires :

1. le profil nomme la **version** du logiciel et la **résolution** de l'écran ;
2. au lancement, une **capture de référence** est comparée à l'écran réel (quelques tuiles
   suffisent) ; si ça diverge, le profil est **refusé**, pas « ajusté » ;
3. toute action qui échoue (cible absente à l'endroit dit) **invalide le profil** et rend la main
   au mode « voir », au lieu de cliquer au hasard.

Sans ces trois règles, un profil est une bombe à retardement : il continue de cliquer
précisément au mauvais endroit.

---

## 10. Le web : le CLI fabriqué à la volée

Pas de profil, parce que le design change. À l'ouverture d'une page [Q15] :

1. instantané (DOM + arbre d'accessibilité, et une capture pour ce qui n'est pas dans le DOM) ;
2. extraction des éléments interactifs : boutons, liens, champs, `<select>` ;
3. génération d'une **API éphémère** pour cette page : `cmd_1: click("Rechercher")`,
   `cmd_2: fill_input("email", …)` ;
4. l'agent choisit ; le logiciel exécute (clic réel, ou navigation directe si l'URL est connue).

Les identifiants sont **éphémères** : ils meurent avec la page. Un identifiant de profil, lui,
est **stable**. Cette distinction doit être portée par le format — sinon l'agent réutilise un id
d'une page à l'autre.

---

## 11. Le couplage avec le harnais

C'est le point qui rend les deux documents solidaires : **chaque élément d'écran est un outil** au
sens du harnais (identifiant + arguments) [Q21][Q22].

| couche | ce qui circule | qui le fabrique |
|---|---|---|
| catalogue | fiches d'outils (id, description, arguments valides) | le logiciel d'écran |
| schéma | un seul, immuable | le harnais |
| appel | `id` + arguments | le modèle |
| exécution | résolution id → fiche → geste | le moteur |
| retour | résultat compact (objet, grille, vide) | le logiciel d'écran |

Conséquence de numérotation, à trancher : les outils d'écran sont **dynamiques** (ils changent
d'une page à l'autre), les outils de la machine sont **stables**. Ils ne peuvent donc pas partager
un espace d'identifiants sans règle : soit deux plages, soit un catalogue par contexte.

---

## 12. Ce qui existe déjà

La conversation pose la question [Q17], et la réponse est : **le principe existe**, l'assemblage
non.

| existant | ce que c'est | ce qui manque pour ce projet |
|---|---|---|
| OSWorld, AndroidWorld | environnements de recherche où un agent pilote un OS via arbres textuels | ce sont des bancs d'essai, pas un démon local |
| agents « computer use » | capture + clics par un grand modèle | un modèle voyant, gros, lent, et des coordonnées devinées |
| OS-Atlas, SeeClick | modèles qui rendent des coordonnées à partir d'une image | c'est l'inverse de la thèse : on ne veut pas de coordonnées |
| OCR (Tesseract, PaddleOCR) | le texte exact | ne dit pas ce qui est cliquable |
| AT-SPI / DOM | rôle et action des éléments | absent ou menteur hors des toolkits standard |

**Ce qui serait propre à ce projet** : un démon local, la deuxième souris dédiée, des profils
semi-statiques par logiciel, et un seul schéma d'outil pour tout. C'est un assemblage, et il n'est
pas public.

---

## 13. Sécurité et cohérence

| risque | règle |
|---|---|
| clic hors de la cible | le geste porte une **fenêtre cible** ; hors de son rectangle, refus |
| l'agent et Moussa se battent pour la souris | **un seul écrivain** à la fois : l'humain reprend la main, l'agent se met en attente |
| action irréversible | les gestes destructifs (supprimer, valider un paiement) sont refusés **par le code de l'outil**, pas par une consigne |
| données sensibles | un champ de mot de passe n'entre **jamais** dans le catalogue (type détecté → absent) |
| panne silencieuse | chaque geste est **journalisé** (id, cible, résultat) et le journal est relu par l'agent, pas par nous |

---

## 14. L'horizon : le super CLI vectoriel

Dernière idée de la conversation [Q20] : extraire des **milliers de fonctions unitaires** de tous
les logiciels, les décrire, les indexer par embedding, et laisser l'agent retrouver les trois ou
quatre fonctions utiles par similarité — sans même lancer le logiciel.

C'est cohérent avec tout le reste, et cela se greffe **au-dessus** de la couche 4 : le catalogue
cesse d'être l'écran courant et devient une bibliothèque. Deux réserves à garder en tête :

- **l'indexation ne remplace pas l'exécution** : une fonction retrouvée doit avoir un corps
  (une fiche YAML) et un état (le logiciel est-il lancé, dans quelle disposition) ;
- **la recherche ramène du plausible, pas du vrai** : un seuil et une vérification d'existence
  sont obligatoires, sinon l'agent invoque une fonction qui n'existe pas.

C'est le même mouvement que MCP, poussé à l'échelle de tout le poste (R20).

---

## 15. Points ouverts

1. **La voie de capture** : XDamage (ouverte ici) comme voie principale — et faut-il une seconde
   voie PipeWire pour l'avenir Wayland, ou attendre ?
2. **Le périmètre de la v1** : un seul logiciel (Krita, cf. document 3) ou le bureau entier
   d'abord ?
3. **Le format du profil** : un YAML par logiciel et par disposition ; qui le produit — à la main,
   ou par enregistrement d'une session réelle ?
4. **La détection d'objets** : formes + OCR suffisent-ils pour un logiciel au thème connu, ou
   faut-il un modèle (YOLO/segmentation) dès la v1 ?
5. **La durée de vie des identifiants** : catalogue par contexte (page/fenêtre) — à figer avant
   que le harnais ne les apprenne.
6. **Le retour** : grille systématique, ou seulement sur demande de l'agent ?
7. **La deuxième souris** : règle udev à poser (`/dev/uinput` est root-only ici) ; à valider par
   Moussa, c'est une modification système.

---

## 16. Fiche de synthèse

| | |
|---|---|
| **nature** | démon local : écran → objets nommés → gestes exécutables |
| **ce que voit l'agent** | un catalogue d'éléments (id, type, texte, action) — jamais un pixel ni une coordonnée |
| **ce que fait l'agent** | un appel d'outil : `click_element(id)`, `fill_input(id, …)`, `draw_line(…)` |
| **capture ici** | X11 : DAMAGE + Composite + MIT-SHM, vérifiés [M 5] |
| **changement** | tuiles 32x32, seuil, GPU ; 3842 tuiles sur 3600x1080 [M 9] |
| **action ici** | `uinput` (deuxième souris, téléport) — `/dev/uinput` à ouvrir par udev [M 12] |
| **profils** | un YAML par logiciel et par disposition, vérifié au lancement, invalidé à l'échec |
| **web** | API éphémère générée à la volée depuis DOM + arbre d'accessibilité |
| **retour** | grille ASCII : 80x40 = 422 tokens, 120x60 = 918 tokens [M 13] |
| **garde-fou** | pooling par maximum (sinon un trait de 3 px disparaît) [M 11] |
| **horizon** | bibliothèque de milliers de gestes indexés par embedding |

---

## Registre des mesures [M n]

Toutes faites sur cette machine, le 18/10/2026. Aucune n'est une estimation reprise d'une réponse
d'assistant.

| # | mesure | valeur | moyen |
|---|---|---|---|
| M 1 | écran virtuel total | 3600x1080 = 3 888 000 px | `xdpyinfo` |
| M 2 | une frame RGBA | 14,83 Mo | calcul |
| M 3 | débit de lecture | 0,16 Go/s à 10 im/s ; 1,56 Go/s à 100 im/s | calcul |
| M 4 | grille 400x200 en tokens | 59 326 (bruit) ; 20 989 (écran réaliste) pour 80 199 car | tokenizer LFM2.5-1.2B |
| M 5 | extensions X11 | DAMAGE 143, Composite 142, MIT-SHM 130, profondeur 24 | `xdpyinfo -queryExtensions` |
| M 6 | portails | pipewire/pw-cli là ; aucun portail n'implémente ScreenCast | `*.portal` |
| M 7 | pilote graphique | NVIDIA 550.163.01 (RTX 3060 + Quadro P2200) | `nvidia-smi` |
| M 8 | dalles | DP-4 1680x1050 +0+0 ; DP-6 1920x1080 +1680+0 | `xrandr` |
| M 9 | tuiles 32x32 | 3842 sur 3600x1080 | calcul |
| M 10 | réduction + rendu | 56 ms (3 tailles) ; 9,4 ms (80x40 seule) | numpy/PIL |
| M 11 | pooling | trait de 3 px : invisible en moyenne, visible en maximum, aux 3 tailles | numpy/PIL |
| M 12 | périphérique virtuel | `/dev/uinput` en crw------- root:root | `ls -l` |
| M 13 | grilles ASCII | 80x40 = 422 tokens ; 120x60 = 918 ; 160x80 = 1 631 | tokenizer LFM2.5-1.2B |
| M 14 | Krita installé ? | non : seuls `krita-data` et `krita-l10n` | `dpkg -l` |
| M 15 | paquet krita 5.2.9 | 21 Mo à télécharger, 88 Mo installés | `apt-cache show` |
| M 16 | écran virtuel X | `xvfb-run` et `Xvfb` présents | `command -v` |
| M 17 | format `.kra` | zip : `maindoc.xml`, `preview.png`, calques par tuiles, `mergedimage.png` si enregistré | `zipfile` |
| M 18 | session | X11, bureau Cinnamon | `$XDG_SESSION_TYPE` |
| M 19 | lecture d'un `.kra` réel | aperçu 256x139 lu en 19,1 ms (canvas 1408x768) | PIL |
| M 20 | compilateur de démonstration | 29x8 → 18 rectangles → raster → grille : 100,0 % des cellules, 0 écart, 0,07 ms, SVG de 424 octets valide | cairo/pycairo |
| M 24 | `XGetImage` plein écran | 24,02 ms/frame (41,6 im/s) | C++ -O2, `-lX11` |
| M 25 | `XShmGetImage` plein écran | 4,93 ms (202,8 im/s) ; + diff = 10,83 ms (92,3 im/s) | C++, `libXext` |
| M 26 | `XShmGetImage` région 400x300 | **0,16 ms** ; + diff 0,09 ms = 0,25 ms | même binaire |
| M 28 | composition | **ACTIVE** : sélection `_NET_WM_CM_S0` possédée ; `gsettings` dit `false` et ment | sonde C++ |
| M 34 | `DAMAGE` racine, compositeur actif | 49 écrans entiers, aucune localisation | C++ |
| M 35 | `DAMAGE` par fenêtre | rectangles de 3 420 px, enveloppe = la fenêtre | C++ |

La numérotation est commune aux trois documents : **M 21 à M 23** sont définies dans
`SLM-tool-use-only.md` (format d'appel réel), **M 24 à M 33** dans `API-visuelle-plan-architecture.md`
(capture SHM, composition, en-têtes, pointeurs, accessibilité, OCR).
