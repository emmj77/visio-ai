#!/bin/sh
# tools/session.sh — ma session de travail, a moi, sans ecran et sans voler le focus.
#
# Pourquoi : un geste fiable sous X11 (XTest) prend le focus et deplace le VRAI pointeur. Un
# test fait sur la session de l'utilisateur lui prend son clavier. On travaille donc dans un
# serveur X separe (Xvfb : serveur X sans ecran), invisible depuis la session de bureau.
#
#   tools/session.sh start          demarre le serveur dedie (ecran 1880x1040x24)
#   tools/session.sh status         dit s'il tourne
#   tools/session.sh run <commande> execute une commande DANS la session dediee
#   tools/session.sh shot [fichier] copie l'ecran dedie (PNG, defaut /tmp/session.png)
#   tools/session.sh stop           arrete la session dediee
#
# Variables : VISIO_DISPLAY (defaut :99), VISIO_ECRAN (defaut 1880x1040x24).
set -eu

ECRAN="${VISIO_DISPLAY:-:99}"
TAILLE="${VISIO_ECRAN:-1880x1040x24}"
PIDF="/tmp/visio-session${ECRAN#:}.pid"
LOGF="/tmp/visio-session${ECRAN#:}.log"

demarrer() {
    if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
        echo "session $ECRAN deja active (pid $(cat "$PIDF"))"
        return 0
    fi
    Xvfb "$ECRAN" -screen 0 "$TAILLE" >"$LOGF" 2>&1 &
    echo $! >"$PIDF"
    i=0
    while [ $i -lt 200 ]; do
        DISPLAY=$ECRAN xdpyinfo >/dev/null 2>&1 && { echo "session $ECRAN prete ($TAILLE, pid $(cat "$PIDF"))"; return 0; }
        i=$((i + 1)); sleep 0.05
    done
    echo "ECHEC : $ECRAN n'a pas demarre (voir $LOGF)" >&2
    return 1
}

arreter() {
    if [ -f "$PIDF" ]; then
        kill "$(cat "$PIDF")" 2>/dev/null || true
        rm -f "$PIDF"
        echo "session $ECRAN arretee"
    else
        echo "aucune session $ECRAN"
    fi
}

case "${1:-}" in
    start) demarrer ;;
    stop) arreter ;;
    status)
        if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
            echo "session $ECRAN active (pid $(cat "$PIDF"), $TAILLE)"
        else
            echo "session $ECRAN inactive"; exit 1
        fi ;;
    run)
        shift
        [ $# -gt 0 ] || { echo "usage: session.sh run <commande...>" >&2; exit 2; }
        DISPLAY=$ECRAN exec "$@" ;;
    shot)
        fichier="${2:-/tmp/session.png}"
        DISPLAY=$ECRAN import -window root "$fichier"
        echo "$fichier" ;;
    *)
        sed -n '2,15p' "$0"; exit 2 ;;
esac
