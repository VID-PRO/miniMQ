#!/usr/bin/env bash
# Offline-Installation für den Dell Wyse 3040 (Ubuntu 24.04 LTS, amd64).
#
# Nutzt eine lokale APT-Quelle aus ./repo (532 Pakete, samt aller Abhängigkeiten
# inkl. der MagicQ-Abhängigkeiten wie liblzo2-2) und führt anschliessend
# setup_magicq_wyse.sh aus – OHNE Internetverbindung.
#
# Verwendung (mit sudo, wie setup_magicq_wyse.sh):
#   sudo ./install_offline.sh                     # ohne MagicQ-.deb
#   sudo ./install_offline.sh /pfad/magicq_ubuntu_*.deb
#
# Optionen:
#   --restore        Apt-Quellen des Systems nach der Installation wiederherstellen
#                    (Standard: bleiben deaktiviert – der Rechner läuft offline als Show-PC)
#   --check-only     Nur prüfen (Repo verfügbar? alle Pakete auflösbar?) – installiert nichts
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$SCRIPT_DIR/repo"
SETUP_SCRIPT="$SCRIPT_DIR/setup_magicq_wyse.sh"
RESTORE=0
CHECK_ONLY=0
MAGICQ_DEB=""

for arg in "$@"; do
    case "$arg" in
        --restore) RESTORE=1 ;;
        --check-only) CHECK_ONLY=1 ;;
        -*) echo "Unbekannte Option: $arg" >&2; exit 2 ;;
        *) MAGICQ_DEB="$arg" ;;
    esac
done

log() { echo ">>> $*"; }
die() { echo "!!! FEHLER: $*" >&2; exit 1; }

# ---------- Vorprüfungen ----------------------------------------------------
[ "$(id -u)" -eq 0 ] || die "Bitte mit sudo ausführen."
[ -d "$REPO_DIR/pool" ] || die "Repo nicht gefunden: $REPO_DIR"
[ -f "$REPO_DIR/Packages" ] && [ -f "$REPO_DIR/Packages.gz" ] || die "Paketindex fehlt in $REPO_DIR"

UBUNTU_VERSION=$(lsb_release -rs 2>/dev/null | cut -d- -f1 | cut -d. -f1 || true)
ARCH=$(dpkg --print-architecture 2>/dev/null || uname -m)
log "Ubuntu-Version: ${UBUNTU_VERSION:-?}   Architektur: $ARCH"
[ "$ARCH" = "amd64" ] || die "Nur amd64 unterstützt (gefunden: $ARCH)."
if [ -n "$UBUNTU_VERSION" ] && [ "$UBUNTU_VERSION" -lt 24 ] 2>/dev/null; then
    echo "!!! Hinweis: Die Pakete sind für Ubuntu 24.04 (noble) gebaut. "
    echo "    Auf älteren Versionen kann der Offline-Install fehlschlagen."
fi

if [ -n "$MAGICQ_DEB" ]; then
    [ -f "$MAGICQ_DEB" ] || die "MagicQ-.deb nicht gefunden: $MAGICQ_DEB"
else
    # automatisch suchen, falls ein magicq_ubuntu_*.deb neben dem Skript liegt
    for cand in "$SCRIPT_DIR"/magicq_ubuntu_*.deb "$REPO_DIR"/magicq_ubuntu_*.deb "$REPO_DIR"/pool/magicq_ubuntu_*.deb; do
        [ -f "$cand" ] && MAGICQ_DEB="$cand" && break
    done
    if [ -z "$MAGICQ_DEB" ]; then
        echo "=== Kein magicq_ubuntu_*.deb gefunden – MagicQ selbst wird NICHT installiert."
        echo "    Du kannst den .deb später nachliefern und installieren:"
        echo "    sudo dpkg -i <pfad>/magicq_ubuntu_*.deb && sudo apt-get -f install"
        echo "    (Bei ChamSys ist für den Download ein Konto nötig.)"
    else
        log "MagicQ-.deb gefunden: $MAGICQ_DEB"
    fi
fi

# ---------- Prüfmodus (nichts ändern) --------------------------------------
if [ "$CHECK_ONLY" -eq 1 ]; then
    log "PRÜFMODUS: Es wird nichts installiert."
    echo "Pakete im Repo: $(ls "$REPO_DIR"/pool/*.deb | wc -l | tr -d ' ')"
    echo "Indexgrösse:    $(du -sh "$REPO_DIR" | cut -f1)"
    echo "MagicQ-.deb:    ${MAGICQ_DEB:-keines}"
    # Strukturelle Prüfung: Einträge im Index vs. vorhandene .deb Dateien
    IDX_CNT=$(grep -c "^Package:" "$REPO_DIR/Packages" || true)
    DEB_CNT=$(ls "$REPO_DIR"/pool/*.deb | wc -l | tr -d ' ')
    [ "$IDX_CNT" -eq "$DEB_CNT" ] && [ "$IDX_CNT" -gt 0 ] \
        && log "Index konsistent: $IDX_CNT Pakete / $DEB_CNT .deb Dateien" \
        || die "Repo inkonsistent (Index: $IDX_CNT, Debs: $DEB_CNT)."
    exit 0
fi

# ---------- Apt-Quellen umstellen (offline) --------------------------------
SOURCES_BACKUP="$SCRIPT_DIR/.apt-sources-backup"
mkdir -p "$SOURCES_BACKUP"
MOVED=0
# Ubuntu 24.04: /etc/apt/sources.list.d/ubuntu.sources (DEB822)
# ältere: /etc/apt/sources.list und /etc/apt/sources.list.d/*.list
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
    [ -e "$f" ] || continue
    case "$f" in *.backup.*) continue ;; esac
    b="$SOURCES_BACKUP/$(basename "$f").backup"
    if [ ! -e "$b" ]; then
        cp -a "$f" "$b"
        log "Sicherung: $f -> $b"
    fi
    rm -f "$f"
    MOVED=1
done
[ "$MOVED" -eq 1 ] && log "Netzwerk-Apt-Quellen deaktiviert (Backup: $SOURCES_BACKUP)."

log "Aktiviere lokale Paketquelle: $REPO_DIR"
mkdir -p /etc/apt/sources.list.d
cat > /etc/apt/sources.list.d/minimq-offline.list <<EOF
deb [trusted=yes] file:$REPO_DIR ./
EOF

log "apt-get update (nur lokale Quelle)..."
rm -rf /var/lib/apt/lists/*
apt-get update

log "Prüfe, ob alle Pakete des Setup-Skripts auflösbar sind ..."
# Gleiche Paketliste wie setup_magicq_wyse.sh (noble-Variante), plus python3.
# --simulate: installiert nichts, zeigt aber fehlende/unkonfigurierbare Pakete.
if ! apt-get -s -y --no-install-recommends install \
    openbox xinit xserver-xorg x11-xserver-utils feh dbus-x11 udisks2 \
    libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5opengl5 \
    libqt5printsupport5 libqt5xml5 libqt5sql5 libqt5multimedia5 libqt5multimediawidgets5 \
    libqt5svg5 libqt5qml5 libqt5quick5 libglu1-mesa libgl1 libglx-mesa0 \
    libxext6 libxrender1 libusb-1.0-0 libportaudio2 libasound2t64 libarchive13 \
    zlib1g libglib2.0-0 libstdc++6 ffmpeg \
    libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    libxcb-xinerama0 libxcb-cursor0 libxcb-keysyms1 libxcb-image0 \
    libxcb-render-util0 libxcb-icccm4 libxcb-shape0 libxcb-xfixes0 libxcb-xkb1 \
    libxcb-xinput0 libxcb-randr0 libxcb-sync1 libxcb-shm0 libxcb1 \
    libxkbcommon-x11-0 libxkbcommon0 libfontconfig1 libfreetype6 libx11-xcb1 \
    libegl1 plymouth plymouth-themes plymouth-label plymouth-theme-ubuntu-text \
    plymouth-theme-spinner x11-apps python3 \
    >/tmp/minimq-simulate.log 2>&1; then
    echo "!!! Einige Pakete sind im lokalen Repo nicht auflösbar:" >&2
    grep -E "Unable to locate|E:|has no installation candidate" /tmp/minimq-simulate.log || true
    die "Prüfung fehlgeschlagen – siehe /tmp/minimq-simulate.log"
fi
log "Alle Pakete auflösbar – Simulation OK."

# ---------- Setup-Skript ausführen -----------------------------------------
if [ -n "$MAGICQ_DEB" ]; then
    log "Starte setup_magicq_wyse.sh mit MagicQ-.deb ..."
    bash "$SETUP_SCRIPT" "$MAGICQ_DEB"
elif command -v bash >/dev/null; then
    log "Starte setup_magicq_wyse.sh (ohne MagicQ-.deb)..."
    bash "$SETUP_SCRIPT"
else
    die "bash nicht gefunden"
fi

# ---------- Optional: Quellen wiederherstellen -----------------------------
if [ "$RESTORE" -eq 1 ]; then
    log "--restore: Netzwerk-Apt-Quellen werden wiederhergestellt ..."
    rm -f /etc/apt/sources.list.d/minimq-offline.list
    for b in "$SOURCES_BACKUP"/*.backup; do
        [ -e "$b" ] || continue
        orig="/etc/apt/$(basename "$b" .backup)"
        cp -a "$b" "$orig"
        log "Wiederhergestellt: $orig"
    done
    apt-get update || true
else
    echo
    echo "=============================================================="
    echo "  Netzwerk-Apt-Quellen bleiben deaktiviert (Show-PC ohne Internet)."
    echo "  Wiederherstellen (falls doch online gearbeitet werden soll):"
    echo "    sudo ./install_offline.sh --restore"
    echo "  Manuell: sudo cp ${SOURCES_BACKUP}/*.backup /etc/apt/<name>"
    echo "=============================================================="
fi

echo
echo "Fertig. Alles Gute für die Show!"