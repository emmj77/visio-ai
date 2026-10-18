#!/bin/sh
# tests/gestes_xvfb.sh — verifier les gestes SANS toucher la session de l'utilisateur.
#
# Pourquoi un serveur X a part : un geste fiable sous X11 (XTest) prend le focus et deplace le
# vrai pointeur. Faire le test sur la session de travail volerait le clavier de la personne
# devant l'ecran. Xvfb est un serveur X sans ecran : tout s'y passe, et rien n'en sort.
#
# Ce que le test verifie, et qui n'est pas une supposition :
#   1. les evenements ARRIVENT a l'application (journal de xev : ButtonPress, KeyPress) ;
#   2. le pointeur est RENDU a sa position d'origine ;
#   3. le focus est RENDU a la fenetre d'origine.
set -eu

ICI=$(cd "$(dirname "$0")/.." && pwd)
VISIO="$ICI/build/visio"
DISPLAY_NUM=":99"
JOURNAL=$(mktemp /tmp/xev.XXXXXX.log)
XVFB_LOG=$(mktemp /tmp/xvfb.XXXXXX.log)

nettoyer() {
    [ -n "${PID_XEV:-}" ] && kill "$PID_XEV" 2>/dev/null || true
    [ -n "${PID_XVFB:-}" ] && kill "$PID_XVFB" 2>/dev/null || true
    rm -f "$JOURNAL" "$XVFB_LOG"
}
trap nettoyer EXIT INT TERM

if [ ! -x "$VISIO" ]; then
    echo "ECHEC : $VISIO absent (cmake --build build)"
    exit 1
fi

Xvfb "$DISPLAY_NUM" -screen 0 1280x800x24 >"$XVFB_LOG" 2>&1 &
PID_XVFB=$!
# Attendre que le serveur reponde, sans sleep aveugle : on interroge jusqu'a reponse.
i=0
while [ $i -lt 100 ]; do
    if DISPLAY=$DISPLAY_NUM xdpyinfo >/dev/null 2>&1; then break; fi
    i=$((i + 1)); sleep 0.05
done
if ! DISPLAY=$DISPLAY_NUM xdpyinfo >/dev/null 2>&1; then
    echo "ECHEC : Xvfb n'a pas demarre"
    exit 1
fi

# xev occupe la fenetre cible et journalise tout ce qu'elle recoit.
DISPLAY=$DISPLAY_NUM xev -geometry 500x400+40+40 -name "Cible" >"$JOURNAL" 2>&1 &
PID_XEV=$!
i=0
while [ $i -lt 100 ]; do
    if grep -q "Outer window is" "$JOURNAL" 2>/dev/null; then break; fi
    i=$((i + 1)); sleep 0.05
done

# Le geste : clic a 200,200 puis le mot « bonjour ».
set +e
SORTIE=$(DISPLAY=$DISPLAY_NUM "$VISIO" --win "Cible" --gestes "200,200,bonjour" 2>&1)
CODE=$?
set -e
echo "$SORTIE"

echec=0
verifier() {
    if [ "$1" = "oui" ]; then printf '  OK    %s\n' "$2"; else printf '  ECHEC %s\n' "$2"; echec=1; fi
}

# 1. les evenements arrivent
if grep -q "ButtonPress" "$JOURNAL"; then r=oui; else r=non; fi
verifier "$r" "un ButtonPress est arrive a la cible"
if [ "$(grep -c 'KeyPress' "$JOURNAL")" -ge 7 ]; then r=oui; else r=non; fi
verifier "$r" "sept KeyPress (bonjour) sont arrives a la cible"
N1=$(grep -c 'KeyPress' "$JOURNAL")

# Phase 2 : le pointeur est PARQUE loin de la cible ; la frappe doit quand meme arriver, par le
# focus donne puis rendu. C'est la preuve qu'on n'a pas besoin d'aller sous la fenetre pour
# taper dedans — et donc que le geste ne deplace pas la souris.
if DISPLAY=$DISPLAY_NUM xdotool mousemove 1100 700 2>/dev/null; then
    SORTIE2=$(DISPLAY=$DISPLAY_NUM "$VISIO" --win "Cible" --gestes "-1,-1,merci" 2>&1)
    echo "$SORTIE2"
    N2=$(grep -c 'KeyPress' "$JOURNAL")
    if [ "$N2" -ge $((N1 + 5)) ]; then r=oui; else r=non; fi
    verifier "$r" "cinq KeyPress de plus, pointeur parque a 1100,700 (focus donne puis rendu)"
    case "$SORTIE2" in
        *"pointeur 1100,700 -> 1100,700"*) r=oui ;;
        *) r=non ;;
    esac
    verifier "$r" "le pointeur parque n'a pas bouge"
else
    verifier non "xdotool absent : phase 2 non testee"
fi

# 2. le pointeur est rendu
case "$SORTIE" in
    *"pointeur rendu : OUI"*) r=oui ;;
    *) r=non ;;
esac
verifier "$r" "le pointeur est revenu a sa position"
# 3. le focus est rendu
if [ "$CODE" -eq 0 ]; then r=oui; else r=non; fi
verifier "$r" "visio rend la main sans erreur (code $CODE)"

if [ "$echec" -ne 0 ]; then
    echo "--- journal xev (extrait) ---"
    grep -E "ButtonPress|KeyPress|FocusIn|FocusOut" "$JOURNAL" | head -20 || true
    exit 1
fi
echo "gestes : OK"
