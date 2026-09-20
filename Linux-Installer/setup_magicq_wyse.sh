#!/usr/bin/env bash
#
# setup_magicq_wyse.sh
# ----------------------------------------------------------------------------
# Einrichtungs-Skript für einen ChamSys MagicQ Show-PC auf einem Dell Wyse 3040
# mit Ubuntu (Server) + Openbox.
#
# Was das Skript tut:
#   1. Installiert Openbox, xinit und Hilfspakete (leichtgewichtig).
#   2. Installiert Qt5-/xcb-/Laufzeit-Abhängigkeiten.
#   3. Installiert MagicQ (falls .deb im selben Ordner).
#   4. Legt Openbox-Autostart + Konfiguration für User 'chamsys' an.
#   5. Richtet Autologin, DHCP, USB-Automount, Splashscreen ein.
#
# Ausführung:
#   sudo ./setup_magicq_wyse.sh [magicq_ubuntu_*.deb]
#
# Muss als root (sudo) ausgeführt werden.
# ---------------------------------------------------------------------------

set -euo pipefail

# ---------- Konfiguration (bei Bedarf anpassen) -------------------------------
MAGICQ_USER="chamsys"
MAGICQ_HOME=$(getent passwd "$MAGICQ_USER" | cut -d: -f6)

if [ -z "$MAGICQ_HOME" ]; then
    echo "!!! FEHLER: Benutzer '$MAGICQ_USER' existiert nicht."
    echo "    Erstelle ihn zuerst:  sudo adduser $MAGICQ_USER"
    exit 1
fi

echo "=== MagicQ / Wyse 3040 Einrichtung ==="
echo "MagicQ-Benutzer : $MAGICQ_USER"
echo "HOME-Verzeichnis: $MAGICQ_HOME"
echo

# ---------- 0. .config/openbox-Verzeichnis VORHER anlegen --------------------
# Muss existieren, bevor im Skript darauf zugegriffen wird.
echo ">>> 0/6 Erstelle Konfigurationsverzeichnisse..."
mkdir -p "$MAGICQ_HOME/.config/openbox"
chown -R "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.config"

# Ubuntu-Version früh ermitteln (wird für usbmount vs. udev/udisks benötigt)
UBUNTU_VERSION=$(lsb_release -rs 2>/dev/null | cut -d. -f1 || echo "22")
echo "Detected Ubuntu major version: $UBUNTU_VERSION"

# ---------- 1. Basispakete installieren (Openbox, X, USB) --------------------
echo ">>> 1/6 Installiere Basispakete (Openbox, X, USB-Werkzeuge)..."
sudo apt-get update

# usbmount ist nur bis Ubuntu 22.04 verfügbar. Ab 24.04 wurde das Paket aus den
# Repos entfernt ("Unable to locate package usbmount"). Wir installieren es daher
# nur auf alten Versionen; auf 24+ nutzen wir udev/udisks für Auto-Mount.
if [ "$UBUNTU_VERSION" -lt 24 ] 2>/dev/null; then
    sudo apt-get install -y --no-install-recommends \
        openbox \
        xinit \
        xserver-xorg \
        x11-xserver-utils \
        feh \
        dbus-x11 \
        udisks2 \
        usbmount 2>/dev/null || true
else
    sudo apt-get install -y --no-install-recommends \
        openbox \
        xinit \
        xserver-xorg \
        x11-xserver-utils \
        feh \
        dbus-x11 \
        udisks2 \
        || true
    echo ">>> Ubuntu >= 24: usbmount nicht verfügbar -> nutze udev/udisks für Auto-Mount."
fi

# ---------- 1b. Qt5 / xcb / Laufzeit-Abhängigkeiten --------------------------
echo ">>> 1b/6 Installiere Qt5, xcb-Plugins und Laufzeit-Abhängigkeiten..."

# ALSA (Audio): Paketname ist versionsabhängig.
#   - Ubuntu 22.04: libasound2
#   - Ubuntu 24.04+: libasound2t64  (Suffix "t64" = 64-bit time_t-Version)
# "libasound2 has no installation candidate" tritt auf, wenn man libasound2
# auf 24.04+ installieren will (es existiert dort nur noch als libasound2t64).
if [ "$UBUNTU_VERSION" -ge 24 ] 2>/dev/null; then
    ALSA_PKG="libasound2t64"
    echo ">>> Ubuntu >= 24: verwende ALSA-Paket '$ALSA_PKG' (nicht libasound2)."
else
    ALSA_PKG="libasound2"
fi

sudo apt-get install -y --no-install-recommends \
    libqt5core5a \
    libqt5gui5 \
    libqt5widgets5 \
    libqt5network5 \
    libqt5opengl5 \
    libqt5printsupport5 \
    libqt5xml5 \
    libqt5sql5 \
    libqt5multimedia5 \
    libqt5multimediawidgets5 \
    libqt5svg5 \
    libqt5qml5 \
    libqt5quick5 \
    libglu1-mesa \
    libgl1 \
    libglx-mesa0 \
    libxext6 \
    libxrender1 \
    libusb-1.0-0 \
    libportaudio2 \
    "$ALSA_PKG" \
    libarchive13 \
    zlib1g \
    libglib2.0-0 \
    libstdc++6 \
    ffmpeg \
    libgstreamer1.0-0 \
    libgstreamer-plugins-base1.0-0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    || true

# ---- 1c. xcb-Plugin-Abhängigkeiten (FIX: "could not load QT plugin xcb") ----
# Das Qt5 xcb-Plattform-Plugin benötigt diese Bibliotheken zwingend.
# OHNE diese Fehlermeldung: "could not load QT plugin xcb"
echo ">>> 1c/6 Installiere xcb-Plugin-Abhängigkeiten (Qt5 xcb-Fehlerbehebung)..."

sudo apt-get install -y --no-install-recommends \
    libxcb-xinerama0 \
    libxcb-cursor0 \
    libxcb-keysyms1 \
    libxcb-image0 \
    libxcb-render-util0 \
    libxcb-icccm4 \
    libxcb-shape0 \
    libxcb-xfixes0 \
    libxcb-xkb1 \
    libxcb-xinput0 \
    libxcb-randr0 \
    libxcb-sync1 \
    libxcb-shm0 \
    libxcb1 \
    libxkbcommon-x11-0 \
    libxkbcommon0 \
    libfontconfig1 \
    libfreetype6 \
    libx11-xcb1 \
    libegl1 \
    libgl1 \
    libglx-mesa0 \
    || true

echo ">>> xcb-Plugin-Abhängigkeiten installiert."

# ---------- 2. MagicQ installieren (falls .deb vorhanden) ----------------------
MAGICQ_DEB="${1:-}"
if [ -n "$MAGICQ_DEB" ] && [ -f "$MAGICQ_DEB" ]; then
    echo ">>> 2/6 Installiere MagicQ aus: $MAGICQ_DEB"
    sudo dpkg -i "$MAGICQ_DEB" || sudo apt-get -f install -y

    MQ_BIN="/opt/magicq/bin/mqqt"
    if [ -x "$MQ_BIN" ]; then
        echo ">>> Prüfe MagicQ-Bibliotheken (ldd)..."
        MISSING=$(cd /opt/magicq && LD_LIBRARY_PATH=/opt/magicq/lib \
                  ldd ./bin/mqqt 2>/dev/null | grep -i "not found" || true)
        if [ -n "$MISSING" ]; then
            echo "!!! FEHLENDE BIBLIOTHEKEN ERKANNT:"
            echo "$MISSING"
            echo ">>> Versuche fixende Installation..."
            sudo apt-get -f install -y || true
            sudo apt-get install -y --no-install-recommends \
                libglu1-mesa libgl1 libusb-1.0-0 libportaudio2 \
                "$ALSA_PKG" libarchive13 ffmpeg \
                libqt5core5a libqt5gui5 libqt5widgets5 libqt5opengl5 \
                || true
            MISSING2=$(cd /opt/magicq && LD_LIBRARY_PATH=/opt/magicq/lib \
                       ldd ./bin/mqqt 2>/dev/null | grep -i "not found" || true)
            if [ -n "$MISSING2" ]; then
                echo "!!! Immer noch fehlend:"
                echo "$MISSING2"
            else
                echo ">>> Alle fehlenden Bibliotheken behoben."
            fi
        else
            echo ">>> Alle Bibliotheken gefunden - MagicQ-Abhängigkeiten vollständig."
        fi
    else
        echo ">>> Hinweis: Binary $MQ_BIN nicht gefunden - prüfe nach der Installation."
    fi
else
    echo ">>> 2/6 KEINE MagicQ-.deb angegeben/gefunden -> überspringe Install."
    echo "    Lade diese herunter von: https://www.chamsys.co.uk/mqdownload/"
    echo "    und führe danach aus: sudo dpkg -i magicq_ubuntu_*.deb"
fi

# ---------- 3. MagicQ-Start-Skript anlegen ----------------------------------
echo ">>> 3/6 Erstelle MagicQ-Start-Skript..."

cat > /usr/local/bin/start_magicq.sh <<'STARTEOF'
#!/usr/bin/env bash
# Startet MagicQ im Fullscreen / Full-Panel-Modus (Compact-Wing-Layout).
set -e

# Warten, bis der X-Server bereit ist
while ! xset q >/dev/null 2>&1; do sleep 1; done

# Bildschirm-Sparmodus deaktivieren: Monitor soll IMMER an bleiben.
xset s off 2>/dev/null || true
xset -dpms 2>/dev/null || true
xset dpms 0 0 0 2>/dev/null || true

# Maus-Cursor ausblenden: transparentes XCursor-Theme erzwingen (gilt direkt für
# den MagicQ/Qt-Prozess) + Root-Cursor auf "none" setzen.
# (WICHTIG: funktioniert nur, wenn es ein GÜLTIGES XCursor-Theme mit enthaltenen
#  .cursor-Dateien ist - rohe PNGs reichen nicht. Schritt 5f erzeugt das per
#  xcursorgen. -cursor_name none blendet zusätzlich den Root-Zeiger aus.)
export XCURSOR_THEME="${XCURSOR_THEME:-Transparent}"
export XCURSOR_SIZE="${XCURSOR_SIZE:-32}"
xsetroot -cursor_name none 2>/dev/null || true

# $HOME auf den MagicQ-Benutzer erzwingen (MagicQ legt sein Datenverzeichnis
# in ~/MagicQ an). Läuft dieses Skript z. B. mit falschem/leerem $HOME, kann
# MagicQ sein Datenverzeichnis nicht anlegen ("cannot create the data folder").
if [ -n "$MAGICQ_USER" ] && [ -d "/home/$MAGICQ_USER" ]; then
    export HOME="/home/$MAGICQ_USER"
fi
if [ -z "$HOME" ] || [ ! -d "$HOME" ]; then
    [ -d /home/chamsys ] && export HOME=/home/chamsys
fi
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache"
mkdir -p "$HOME/MagicQ" "$HOME/.config" "$HOME/.local/share" "$HOME/.cache"

# Qt5 xcb-Plattform erzwingen + DPI-Skalierung deaktivieren
export QT_QPA_PLATFORM=xcb
export QT_AUTO_SCREEN_SCALE_FACTOR=0
export QT_PLUGIN_PATH=/opt/magicq/plugins

# X11: volle Auflösung erzwingen (kein underscan auf DP-Output)
export QT_SCREEN_SCALE_FACTORS=1

# MagicQ starten.
# Nachdem MagicQ beendet wurde (QUIT-Softbutton) den Show-PC automatisch
# herunterfahren (Fahrplan: "wenn ich MagicQ beende, fahre den PC herunter").
# Shutdown läuft auch bei einem Absturz/Fehlercode - nicht nur bei sauberem QUIT.
MQ_BIN="/opt/magicq/bin/mqqt"
MQ_RUN="/opt/magicq/runmagicq.sh"

MQ_EXIT=0
if [ -x "$MQ_BIN" ]; then
    "$MQ_BIN" 2>&1 || MQ_EXIT=$?
elif [ -x "$MQ_RUN" ]; then
    "$MQ_RUN" 2>&1 || MQ_EXIT=$?
else
    echo "MagicQ nicht gefunden (weder $MQ_BIN noch $MQ_RUN)."
    exit 1
fi

echo "MagicQ beendet (Exit $MQ_EXIT) - fahre Show-PC herunter."
# Sicheres Herunterfahren erzwingen (sudoers.d/magicq-shutdown erlaubt es ohne
# Passwort; macht auch swap auf Platte, bevor es ausschaltet).
sudo -n shutdown -h now 2>/dev/null \
    || systemctl poweroff 2>/dev/null \
    || sudo -n systemctl poweroff 2>/dev/null \
    || shutdown -h now 2>/dev/null \
    || true
# Fallback, falls nichts verfügbar (sollte nie passieren):
exit 0
STARTEOF
chmod +x /usr/local/bin/start_magicq.sh

# Erlaube der MagicQ-Benutzersitzung, den Rechner ohne Passwort herunterzufahren
# (wird von start_magicq.sh nach dem Beenden von MagicQ aufgerufen). Robuster als
# das reine polkit-Verhalten, weil die Sitzung per Autologin evtl. nicht immer als
# "aktive lokale Sitzung" registriert ist.
if [ -n "$MAGICQ_USER" ]; then
    mkdir -p /etc/sudoers.d
    # 'shutdown' liegt Debianmäßig unter /usr/sbin, auf manchen Systemen /sbin;
    # beide Pfade + systemctl abdecken, damit der Befehl garantiert ohne
    # Passwort läuft (Literalpfade sind in sudoers erforderlich).
    {
        printf '%s ALL=(root) NOPASSWD: /usr/sbin/shutdown -h now, /sbin/shutdown -h now\n' "$MAGICQ_USER"
        printf '%s ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff, /bin/systemctl poweroff\n' "$MAGICQ_USER"
    } > /etc/sudoers.d/magicq-shutdown
    chmod 440 /etc/sudoers.d/magicq-shutdown
    chown root:root /etc/sudoers.d/magicq-shutdown
fi

# ---------- 3b. X11-Auflösung auf 1280x800 festlegen --------------------------
# Erzwingt für die X-Sitzung (und damit MagicQ fullscreen) 1280x800 über den
# modesetting-Treiber (Standard auf Ubuntu 24.04). 'PreferredMode' + 'Modes'
# sorgen dafür, dass Xorg 1280x800 als einzige/bzw. bevorzugte Auflösung nutzt.
# Der Splash/Plymouth läuft über GRUB_GFXMODE=1280x800 (Abschnitt 6b) ebenfalls
# in 1280x800 - alles bleibt konsistent.
mkdir -p /etc/X11/xorg.conf.d
cat > /etc/X11/xorg.conf.d/11-resolution.conf <<'EOF'
Section "Device"
    Identifier "WyseGPU"
    Driver "modesetting"
    Option "PreferredMode" "1280x800"
EndSection

Section "Screen"
    Identifier "DefaultScreen"
    Device "WyseGPU"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "1280x800"
    EndSubSection
EndSection
EOF
echo ">>> X11-Auflösung auf 1280x800 festgelegt (xorg.conf.d/11-resolution.conf)."

# ---------- 4. Openbox-Autostart für $MAGICQ_USER ----------------------------
echo ">>> 4/6 Erstelle Openbox-Autostart für '$MAGICQ_USER'..."

AUTOSTART="$MAGICQ_HOME/.config/openbox/autostart"
mkdir -p "$MAGICQ_HOME/.config/openbox"

cat >"$AUTOSTART" <<'EOF'
# Openbox-Autostart für den MagicQ Show-PC (Wyse 3040)

# Bildschirm-Sparmodus deaktivieren: Monitor soll IMMER an bleiben.
# (xset verhindert X11-Screensaver + DPMS-Standby; setterm verhindert das
#  Leeren der text-basierten Konsole.)
xset s off 2>/dev/null || true
xset -dpms 2>/dev/null || true
xset dpms 0 0 0 2>/dev/null || true
setterm -blank 0 -powersave off -powerdown 0 >/dev/null 2>&1 || true

# USB-Sticks automatisch mounten (udev/udisks)
for d in /dev/sd[b-z]*; do
    [ -b "$d" ] && udisksctl mount -b "$d" 2>/dev/null || true
done &

# Kurz warten, dann MagicQ starten (Fullscreen / Full-Panel / Compact Wing).
sleep 4
/usr/local/bin/start_magicq.sh &
EOF
chown "$MAGICQ_USER":"$MAGICQ_USER" "$AUTOSTART"
chmod +x "$AUTOSTART"

# ---------- 4b. Openbox rc.xml: keine Fensterdekorationen für MagicQ ----------
echo ">>> 4b/6 Schreibe Openbox-Konfiguration (keine Dekorationen, Fullscreen)..."

RC_XML="$MAGICQ_HOME/.config/openbox/rc.xml"

# Vollständige, valide Openbox-Konfiguration schreiben (statt per sed zu patchen —
# sed-Ersatz hat die rc.xml beim letzten Mal zerstört -> "document empty").
# Der <application>-Block sorgt dafür, dass MagicQ ohne Fensterrahmen und
# im Fullscreen startet.
cat > "$RC_XML" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<openbox_config xmlns="http://openbox.org/3.4/rc"
    xmlns:xi="http://www.w3.org/2001/XInclude">
  <resistance>
    <strength>10</strength>
    <corner_strength>10</corner_strength>
  </resistance>
  <focus>
    <focusNew>yes</focusNew>
    <followMouse>no</followMouse>
    <focusLast>yes</focusLast>
    <underMouse>no</underMouse>
    <focusDelay>200</focusDelay>
    <raiseOnFocus>no</raiseOnFocus>
  </focus>
  <placement>
    <policy>Smart</policy>
    <center>yes</center>
    <monitor>Primary</monitor>
    <primaryMonitor>1</primaryMonitor>
  </placement>
  <theme>
    <name>Clearlooks</name>
    <cornerRadius>0</cornerRadius>
  </theme>
  <desktops>
    <number>1</number>
    <firstdesk>1</firstdesk>
    <names>
      <name>Default</name>
    </names>
    <popupTime>875</popupTime>
  </desktops>
  <resize>
    <drawContents>yes</drawContents>
    <popupShow>Never</popupShow>
    <popupPosition>Center</popupPosition>
  </resize>
  <margins>
    <top>0</top>
    <bottom>0</bottom>
    <left>0</left>
    <right>0</right>
  </margins>
  <dock>
    <position>TopLeft</position>
    <floatingX>0</floatingX>
    <floatingY>0</floatingY>
    <noStrut>no</noStrut>
    <stacking>above</stacking>
    <direction>Vertical</direction>
    <autoHide>no</autoHide>
    <hideDelay>300</hideDelay>
    <showDelay>300</showDelay>
    <moveButton>Middle</moveButton>
  </dock>
  <keyboard>
    <keybind key="A-F4"><action name="Close"/></keybind>
    <keybind key="C-A-Delete"><action name="Execute"><command>shutdown -r now</command></action></keybind>
  </keyboard>
  <mouse>
    <dragThreshold>8</dragThreshold>
    <doubleClickTime>200</doubleClickTime>
    <screenEdgeWarpTime>400</screenEdgeWarpTime>
    <screenEdgeWarpMouse>false</screenEdgeWarpMouse>
  </mouse>
  <menu>
    <file>/etc/xdg/openbox/menu.xml</file>
    <hideDelay>200</hideDelay>
    <showDelay>100</showDelay>
  </menu>
  <applications>
    <!-- MagicQ: Vollbild + ohne Fensterrahmen (Show-PC / Compact Wing) -->
    <application class="*MagicQ*">
      <decor>no</decor>
      <fullscreen>yes</fullscreen>
    </application>
    <application class="*">
      <decor>no</decor>
    </application>
  </applications>
</openbox_config>
EOF
chown "$MAGICQ_USER":"$MAGICQ_USER" "$RC_XML"
echo ">>> rc.xml neu geschrieben: MagicQ ohne Dekoration + Fullscreen."

# ---------- 5. Alle Ethernet-Interfaces auf DHCP (netplan) -------------------
echo ">>> 5/6 Setze alle Ethernet-Interfaces auf DHCP (netplan)..."

cat >/etc/netplan/99-magicq-dhcp.yaml <<'EOF'
network:
  version: 2
  renderer: networkd
  ethernets:
    all-eth:
      match:
        name: "en*"
      dhcp4: yes
      dhcp6: yes
EOF
chmod 600 /etc/netplan/99-magicq-dhcp.yaml

# Boot NICHT am Netzwerk hängen lassen: Ohne angeschlossenes Kabel (kein Carrier)
# würde 'systemd-networkd-wait-online.service' (bzw. network-online.target)
# unbegrenzt warten -> Boot hängt. Wir maskieren den Waiter => Boot geht sofort
# weiter, DHCP/Netzwerk läuft weiterhin im Hintergrund, sobald ein Kabel da ist.
# (MagicQ läuft auch ohne Netzwerk; Art-Net über-nimmt, sobald verbunden.)
systemctl mask systemd-networkd-wait-online.service 2>/dev/null || true
systemctl disable systemd-networkd-wait-online.service 2>/dev/null || true
echo ">>> Boot-langes Warten auf Netzwerk deaktiviert (wait-online maskiert)."

# netplan abschließend anwenden
netplan apply 2>/dev/null || true

# ---------- 5b. udev-Regel: USB-Sticks automatisch mounten -------------------
echo ">>> 5b/6 Lege udev-Regel zum automatischen Mounten von USB-Sticks an..."

tee /etc/udev/rules.d/99-magicq-usb.rules >/dev/null <<'EOF'
ACTION=="add", KERNEL=="sd[b-z][0-9]*", SUBSYSTEM=="block", \
    RUN+="/bin/bash -c 'sleep 2; runuser -l chamsys -c \"udisksctl mount -b %k\" 2>/dev/null'"
EOF
udevadm control --reload-rules

# ---------- 5c. Autologin für $MAGICQ_USER auf tty1 --------------------------
echo ">>> 5c/6 Richte Autologin für '$MAGICQ_USER' auf tty1 ein..."

AUTOLOGIN_DIR="/etc/systemd/system/getty@tty1.service.d"
AUTOLOGIN_CONF="$AUTOLOGIN_DIR/autologin.conf"

mkdir -p "$AUTOLOGIN_DIR"
tee "$AUTOLOGIN_CONF" >/dev/null <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $MAGICQ_USER --noclear tty1 linux
EOF

systemctl enable getty@tty1.service 2>/dev/null || true
systemctl daemon-reload

# ---------- 5d. .xinitrc + .bash_profile für $MAGICQ_USER --------------------
echo ">>> 5d/6 Richte X-Login-Start für '$MAGICQ_USER' ein..."

XINITRC="$MAGICQ_HOME/.xinitrc"
if [ ! -f "$XINITRC" ] || ! grep -q "openbox-session" "$XINITRC"; then
    echo "exec openbox-session" >> "$XINITRC"
    chown "$MAGICQ_USER":"$MAGICQ_USER" "$XINITRC"
fi

BASH_PROFILE="$MAGICQ_HOME/.bash_profile"
if [ ! -f "$BASH_PROFILE" ] || ! grep -q "startx" "$BASH_PROFILE"; then
    cat >> "$BASH_PROFILE" <<'EOF'

# X + Openbox automatisch starten, falls auf lokaler tty eingeloggt
if [ -z "$DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec startx
fi
EOF
    chown "$MAGICQ_USER":"$MAGICQ_USER" "$BASH_PROFILE"
fi

# ---------- 5e. MagicQ-Datenverzeichnis anlegen ------------------------------
# MagicQ speichert Show-Dateien u. Konfiguration in ~/MagicQ (und ~/.config/MagicQ,
# ~/.local/share/Chamsys...). Fehlt es oder ist es nicht beschreibbar, meldet MagicQ
# "cannot create the data folder". Wir legen es hier mit korrekten Rechten an und
# setzen chamsys als Besitzer.
echo ">>> 5e/6 Lege schreibbares MagicQ-Datenverzeichnis für '$MAGICQ_USER' an..."
mkdir -p "$MAGICQ_HOME/MagicQ" \
         "$MAGICQ_HOME/.config/MagicQ" \
         "$MAGICQ_HOME/.local/share/Chamsys" \
         "$MAGICQ_HOME/.local/share/ChamSys" 2>/dev/null || true
chown -R "$MAGICQ_USER":"$MAGICQ_USER" \
    "$MAGICQ_HOME/MagicQ" \
    "$MAGICQ_HOME/.config" \
    "$MAGICQ_HOME/.local" 2>/dev/null || true
echo ">>> MagicQ-Datenverzeichnis bereit: $MAGICQ_HOME/MagicQ"

# ---------- 5f. Maus-Cursor ausblenden ----------------------------------------
# Für einen Show-PC soll kein Mauszeiger sichtbar sein (weder in Openbox noch in
# der Qt-Anwendung MagicQ).
# Vorgehen:
#   - Erzeugt ein GÜLTIGES transparentes XCursor-Theme 'Transparent' per
#     eigenem Python-Writer (XCursor-Binärformat, wie XcursorXcFileSave aus
#     libXcursor; rohe PNGs reichen nicht - Xcursor ignoriert sie stumm).
#   - Legt es SYSTEMWEIT unter /usr/share/icons/Transparent an (immer gefunden,
#     unabhängig von $HOME beim Start) + spiegelt es ins Home.
#   - Setzt /usr/share/icons/default auf 'Transparent' (globaler Fallback für
#     ALLE Apps, auch Qt - damit greift selbst dann ein transparenter Zeiger,
#     wenn MagicQ einen Cursor-Namen auswählt).
#   - 'xsetroot -cursor_name none' blendet zusätzlich den Root-Zeiger aus.
echo ">>> 5f/6 Blende Maus-Cursor aus (transparentes XCursor-Theme)..."
# 'xcursorgen' steckt auf noble nicht in einem eigenen Paket - der Befehl liegt im
# Paket 'x11-apps' (Datei /usr/bin/xcursorgen). Er wird NUR als Fallback gebraucht:
# Primär schreibt das Skript das XCursor-Binärformat direkt per Python (unabhängig
# von xcursorgen/x11-apps). x11-apps trotzdem versuchen zu installieren.
apt-get install -y --no-install-recommends \
    x11-apps xcursor-invisible 2>/dev/null || true
if ! command -v xcursorgen >/dev/null 2>&1; then
    apt-get update 2>/dev/null || true
    apt-get install -y --no-install-recommends \
        x11-apps 2>/dev/null || true
fi
if ! command -v xcursorgen >/dev/null 2>&1; then
    echo "    (Hinweis: xcursorgen fehlt - aber der Python-Writer erzeugt die Dateien trotzdem.)"
fi

# Erzeuge eine gültige transparente XCursor-Datei (XCursor-Binärformat) an Pfad
# $1 (muss auf .cursor enden).
# Primär: eigener Writer per Python - schreibt das XCursor-Dateiformat direkt
# (exakt wie XcursorXcFileSave aus libXcursor), OHNE Abhängigkeit von xcursorgen.
# Fallback: 'xcursorgen' mit einer transparenten PNG, falls x11-apps bereitsteht.
create_transparent_cursor() {
    local OUT="$1"
    local S="${2:-32}"
    # Primär: Python direkt - kein xcursorgen nötig.
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "
import struct, sys
S=${S}
# Konstanten aus Xcursor.h / xcursorint.h (LSBFirst)
MAGIC=0x72756358; FILE_VER=(1<<16)|0
FILE_HDR=16; FILE_TOC=12; CHUNK_HDR=16
IMG_HDR=CHUNK_HDR+(5*4)   # 36
IMG_TYPE=0xfffd0002; IMG_VER=1
w=h=S; pixels=bytes(w*h*4)          # komplett transparent (ARGB=0, premultiplied)
position=FILE_HDR+FILE_TOC          # 1 Bild, Beginn des Image-Chunks
out=struct.pack('<4I',MAGIC,FILE_HDR,FILE_VER,1)
out+=struct.pack('<3I',IMG_TYPE,S,position)          # TOC: type, subtype(size), position
out+=struct.pack('<4I',IMG_HDR,IMG_TYPE,S,IMG_VER)   # ChunkHeader
out+=struct.pack('<5I',w,h,0,0,0)                    # width,height,xhot,yhot,delay
out+=pixels
open('$OUT','wb').write(out)
" 2>/dev/null && [ -s "$OUT" ] && return 0
    fi
    # Fallback: xcursorgen mit transparenter PNG.
    local PNG="${OUT}.png"
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "
import struct, zlib
def chunk(t,d):
    c=t+d
    return struct.pack('>I',len(d))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
w=${S}
ihdr=struct.pack('>IIBBBBB',w,w,8,6,0,0,0)
raw=b'\x00'+b'\x00\x00\x00\x00'*(w*w)
idat=zlib.compress(raw)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)+chunk(b'IDAT',idat)+chunk(b'IEND',b'')
open('$PNG','wb').write(png)
" 2>/dev/null || true
    fi
    if [ -s "$PNG" ] && command -v xcursorgen >/dev/null 2>&1; then
        printf '%s 1 1 %s\n' "${S}" "$PNG" > "${OUT}.config"
        xcursorgen "${OUT}.config" "$OUT" 2>/dev/null || true
        rm -f "${OUT}.config"
    fi
    rm -f "$PNG"
}

# Baut das komplette Theme (cursors/ + index.theme) in Verzeichnis $1.
set_cursor_theme() {
    local THEME_D="$1"
    mkdir -p "$THEME_D/cursors" "$THEME_D"

    create_transparent_cursor "$THEME_D/cursors/transparent.cursor"

    local CURSOR_PNG=""
    [ -s "$THEME_D/cursors/transparent.cursor" ] && CURSOR_PNG="$THEME_D/cursors/transparent.cursor"

    # Alle üblichen Cursor-Namen auf die transparente Cursor-Datei leiten.
    for c in default left_ptr hand pointer text move wait arrow crosshair watch no copy alias \
             all-scroll cell col-resize row-resize n-resize e-resize s-resize w-resize \
             ne-resize nw-resize se-resize sw-resize ew-resize ns-resize nesw-resize nwse-resize \
             context-menu help progress grab grabbing v_double_cursor sb_h_double_arrow sb_v_double_arrow; do
        if [ -n "$CURSOR_PNG" ]; then
            cp -f "$CURSOR_PNG" "$THEME_D/cursors/$c" 2>/dev/null || true
        else
            cp /dev/null "$THEME_D/cursors/$c" 2>/dev/null || true
        fi
    done
    rm -f "$THEME_D/cursors/transparent.cursor"

    tee "$THEME_D/index.theme" >/dev/null <<'EOF'
[Icon Theme]
Name=Transparent
Comment=Transparent cursor
Inherits=
EOF
}

XCURSOR_THEME_NAME="Transparent"

# Paket-Weg: 'Invisible' (xcursor-invisible) direkt nutzen, falls vorhanden.
if [ -d /usr/share/icons/Invisible ]; then
    XCURSOR_THEME_NAME="Invisible"
    echo ">>> Cursor-Theme 'Invisible' (Paket) als Ausgangspunkt."
fi

# Eigene Theme bauen - SYSTEMWEIT (/usr/share/icons) UND im Home spiegeln.
set_cursor_theme "/usr/share/icons/Transparent"
if [ "$XCURSOR_THEME_NAME" = "Transparent" ]; then
    set_cursor_theme "$MAGICQ_HOME/.icons/Transparent"
    chown -R "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.icons" 2>/dev/null || true
    # Verifikation: Die Cursor-Datei MUSS >0 Bytes sein (sonst leer = Fehler).
    if [ -s /usr/share/icons/Transparent/cursors/left_ptr ]; then
        echo ">>> Gültiges transparentes XCursor-Theme 'Transparent' erzeugt (Python, systemweit)."
    else
        echo "!!! FEHLER: Cursor-Datei ist LEER (0 Byte) - die Erzeugung hat nichts geschrieben."
        echo "    Bitte prüfen:  which python3  und  ls -l /usr/share/icons/Transparent/cursors/";
        echo "    (python3 ist normal auf Ubuntu vorinstalliert; sonst: sudo apt-get install -y python3)"
    fi
else
    # Paket 'Invisible' liegt systemweit; zusätzlich ins Home spiegeln
    mkdir -p "$MAGICQ_HOME/.icons/Invisible"
    cp -rn /usr/share/icons/Invisible/* "$MAGICQ_HOME/.icons/Invisible/" 2>/dev/null || true
    chown -R "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.icons" 2>/dev/null || true
    echo ">>> Cursor-Theme 'Invisible' (Paket) aktiviert + ins Home gespiegelt."
fi

# Globaler Fallback: /usr/share/icons/default -> unser Theme (System-Default).
mkdir -p /usr/share/icons/default "$MAGICQ_HOME/.icons/default"
printf '[Icon Theme]\nInherits=%s\n' "$XCURSOR_THEME_NAME" > /usr/share/icons/default/index.theme
printf '[Icon Theme]\nInherits=%s\n' "$XCURSOR_THEME_NAME" > "$MAGICQ_HOME/.icons/default/index.theme"
chown -R "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.icons" 2>/dev/null || true

# X (Qt) + GTK kennen das Theme: global in /etc/environment + GTK-Settings.
if [ -f /etc/environment ] && ! grep -q "XCURSOR_THEME" /etc/environment; then
    printf 'XCURSOR_THEME=%s\nXCURSOR_SIZE=32\n' "$XCURSOR_THEME_NAME" >> /etc/environment
fi
mkdir -p "$MAGICQ_HOME/.config/gtk-3.0"
cat > "$MAGICQ_HOME/.config/gtk-3.0/settings.ini" <<EOF
[Settings]
gtk-cursor-theme-name=$XCURSOR_THEME_NAME
EOF
chown -R "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.config/gtk-3.0" 2>/dev/null || true

echo ">>> Maus-Cursor: transparent (Theme '$XCURSOR_THEME_NAME' systemweit + default-Fallback)."

# XStart-Anpassung: Root-Cursor unsichtbar + Theme setzen (für Openbox + Qt)
# WICHTIG: Die Zeilen müssen VOR 'exec openbox-session' stehen, sonst laufen sie
# nie (exec ersetzt die Shell). Einfügen per awk (portabel zwischen GNU/BSD awk).
mkdir -p "$MAGICQ_HOME/.config"
CURSOR_BLOCK_FILE="$MAGICQ_HOME/.cursor_block.tmp"
printf '# Unsichtbarer Mauszeiger (Show-PC)\nxsetroot -cursor_name none 2>/dev/null || true\nexport XCURSOR_THEME=%s\nexport XCURSOR_SIZE=32\n' "$XCURSOR_THEME_NAME" > "$CURSOR_BLOCK_FILE"
if [ -f "$MAGICQ_HOME/.xinitrc" ] && ! grep -q "XCURSOR_THEME" "$MAGICQ_HOME/.xinitrc"; then
    awk 'NR==FNR{c[NR]=$0; n=NR; next}
         /exec openbox-session/{for(i=1;i<=n;i++)print c[i]; print ""}
         {print}' "$CURSOR_BLOCK_FILE" "$MAGICQ_HOME/.xinitrc" > "$MAGICQ_HOME/.xinitrc.new"
    mv -f "$MAGICQ_HOME/.xinitrc.new" "$MAGICQ_HOME/.xinitrc"
fi
rm -f "$CURSOR_BLOCK_FILE"
chown "$MAGICQ_USER":"$MAGICQ_USER" "$MAGICQ_HOME/.xinitrc" 2>/dev/null || true

# ---------- 6. Plymouth Splashscreen (verdeckt alle Boot-Logs) ----------------
# Ab hier set +e: Der Plymouth-Teil darf NIEMALS durch set -e abgebrochen werden
# (z. B. wenn ein früherer apt-Befehl fehlschlug). Das Theme wird so selbst dann
# angelegt, wenn andere Schritte Probleme hatten.
set +e
echo ">>> 6/6 Installiere Plymouth-Splashscreen, um Boot-Logs zu verdecken..."

# WICHTIG (Plugin-Module / .so Dateien in /usr/lib/.../plymouth/):
#   - plymouth (Basis)      -> script.so, text.so, tribar.so, details.so
#   - plymouth-themes       -> fade-throbber.so, space-flares.so
#   - plymouth-label        -> stellt label-pango.so bereit
# WICHTIG: 'backgrounds.so' gibt es auf noble in KEINEM Paket -> kein
# backgrounds-Theme möglich (das verursachte "backgrounds.so missing").
# Wir nutzen script.so (im plymouth-Basis-Paket), das garantiert da ist.
apt-get install -y --no-install-recommends \
    plymouth \
    plymouth-themes \
    plymouth-label \
    plymouth-theme-ubuntu-text \
    plymouth-theme-ubuntu-logo \
    plymouth-theme-spinner \
    plymouth-theme-script \
    2>/dev/null

# Harte Prüfung auf das plymouth-Binary selbst (nicht plymouth-set-default-theme:
# dieses fehlt auf Ubuntu 24.04 "Noble" bekanntermaßen - LP #1596220). Ohne die
# Prüfung liefe das Skript stillschweigend weiter, obwohl kein Splash da wäre.
if ! command -v plymouth >/dev/null 2>&1; then
    echo "!!! FEHLER: plymouth ist nicht installiert."
    echo "    Prüfe:  sudo apt-get update"
    echo "            sudo apt-get install -y plymouth plymouth-themes"
    echo "    (Splashscreen wird übersprungen.)"
else
    echo ">>> Plymouth OK: plymouth-Binary gefunden."
    echo "    (Hinweis: 'plymouth-set-default-theme' fehlt auf Noble - wir aktivieren"
    echo "     das Theme stattdessen über update-alternatives.)"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPLASH_SRC="$SCRIPT_DIR/splash.png"

# --- 6a. Eigenes 'script'-Theme bauen (Modul GARANTIERT vorhanden) ------------
# RECHERCHEFUND: Auf Ubuntu 24.04 (noble) existiert das backgrounds-Plugin NICHT
# als backgrounds.so in irgendeinem Paket. Dateilisten:
#   plymouth      (Basis, main): details.so, script.so, text.so, tribar.so
#   plymouth-themes (universe):  fade-throbber.so, space-flares.so
# -> backgrounds.so ist auf noble schlicht nicht lieferbar ("backgrounds.so
#    missing" beim Boot ist die Folge). Darum bauen wir ein Theme auf Basis des
#    script-Moduls (script.so), das im plymouth-Basis-Paket steckt und damit
#    zuverlaessig vorhanden ist. script zeigt unser Bild via .script-Nutzcode
#    (reines Ganzbild ohne Ladebalken).
PLYMOUTH_THEME="magicq-splash"
THEME_DIR="/usr/share/plymouth/themes/$PLYMOUTH_THEME"
mkdir -p "$THEME_DIR"

# Bild bevorzugen: splash.png aus dem Skriptverzeichnis; sonst ubuntu-logo/logo.png.
if [ -f "$SPLASH_SRC" ]; then
    cp "$SPLASH_SRC" "$THEME_DIR/background.png"
    echo ">>> Verwende splash.png als Hintergrundbild."
elif [ -f "/usr/share/plymouth/themes/ubuntu-logo/logo.png" ]; then
    cp "/usr/share/plymouth/themes/ubuntu-logo/logo.png" "$THEME_DIR/background.png"
    echo ">>> Verwende ubuntu-logo/logo.png als Hintergrundbild (Fallback)."
else
    echo "!!! Kein Bild gefunden -> schwarzer Hintergrund (script-Modul trotzdem OK)."
    touch "$THEME_DIR/background.png"
fi

# Nutzcode (script.script): Hintergrundbild über den ganzen Bildschirm zeichnen.
# WICHTIG (bekannter Plymouth-script-Bug): Ohne eine aktive Neudarstellung im
# refresh-Callback wird das Framebuffer bei jedem Refresh-Zyklus schwarz und das
# Bild "flackert" (Symptom: Splash wird immer wieder von schwarz abgelöst).
# Lösung: Im refresh-Callback das Sprite jedes Mal neu setzen (SetPosition/
# SetScale/SetOpacity) - das markiert es als "dirty" und recokomposiert es.
tee "$THEME_DIR/$PLYMOUTH_THEME.script" >/dev/null <<'EOF'
bg_image = Image("background.png");
bg_sprite = Sprite(bg_image);

fun draw_bg ()
{
  bg_sprite.SetPosition(Window.GetX(), Window.GetY());
  bg_sprite.SetScale(Window.GetWidth() / bg_image.GetWidth(),
                     Window.GetHeight() / bg_image.GetHeight());
  bg_sprite.SetOpacity(1);
  bg_sprite.SetZ(15);
}

draw_bg();

fun refresh_callback ()
{
  draw_bg();
}

Plymouth.SetRefreshFunction(refresh_callback);
Plymouth.SetRefreshRate(10);
EOF

tee "$THEME_DIR/$PLYMOUTH_THEME.plymouth" >/dev/null <<EOF
[Plymouth Theme]
Name=MagicQ Splash
Description=Splash screen for the MagicQ show PC
ModuleName=script

[script]
ImageDir=$THEME_DIR
ScriptFile=$THEME_DIR/$PLYMOUTH_THEME.script
EOF

# Theme als Standard aktivieren. Auf Ubuntu 24.04 (Noble) fehlt das Binary
# 'plymouth-set-default-theme' (bekannter Ubuntu-Bug LP #1596220) und auch
# 'update-alternatives --set' liefert oft fälschlich Erfolg, ohne das Theme zu
# ändern (Verifikation per readlink ist nötig). Verlässlichste Methode: den
# Alternatives-Link DIREKT auf unser Theme setzen und das Ergebnis verifizieren.
THEME_PLY="$THEME_DIR/$PLYMOUTH_THEME.plymouth"
ACTIVATED=0

try_activate() {
    ACTIVATED=0
    if readlink /etc/alternatives/default.plymouth 2>/dev/null | grep -q "$PLYMOUTH_THEME"; then
        ACTIVATED=1
        return
    fi
    # alternatives registrieren + auf unser Theme zeigen lassen
    update-alternatives --install \
        /usr/share/plymouth/themes/default.plymouth \
        default.plymouth "$THEME_PLY" 100 2>/dev/null || true
    update-alternatives --set default.plymouth "$THEME_PLY" 2>/dev/null || true
    # direkt auf unseren Steckbrief verlinken (gnadenlos auf unser Theme)
    ln -sfn "$THEME_PLY" /etc/alternatives/default.plymouth 2>/dev/null || true
    if [ -e /usr/share/plymouth/themes/default.plymouth ] || [ -L /usr/share/plymouth/themes/default.plymouth ]; then
        ln -sfn /etc/alternatives/default.plymouth /usr/share/plymouth/themes/default.plymouth 2>/dev/null || true
    else
        cp -f "$THEME_PLY" /usr/share/plymouth/themes/default.plymouth 2>/dev/null || true
    fi
    if readlink /etc/alternatives/default.plymouth 2>/dev/null | grep -q "$PLYMOUTH_THEME"; then
        ACTIVATED=1
    fi
}

if command -v plymouth-set-default-theme >/dev/null 2>&1; then
    plymouth-set-default-theme "$PLYMOUTH_THEME" 2>/dev/null || true
fi
try_activate

if [ "$ACTIVATED" -eq 1 ]; then
    echo ">>> Default-Theme verifiziert: '$PLYMOUTH_THEME' ist nun aktiv."
    echo "    Kontrolle:  readlink /etc/alternatives/default.plymouth"
else
    echo "!!! WARNUNG: Konnte Default-Theme nicht verifizieren (Automatik fehlgeschlagen)."
    echo "    Manuell setzen:"
    echo "      echo '$THEME_PLY' | sudo tee /etc/alternatives/default.plymouth"
    echo "      sudo update-initramfs -u"
fi
echo ">>> Eigenes script-Theme '$PLYMOUTH_THEME' angelegt (script.so aus plymouth-Basis)."

# --- 6b. GRUB anpassen (quiet + splash, keine Log-Meldungen) -----------------
# WICHTIG: 'splash' muss in GRUB_CMDLINE_LINUX_DEFAULT stehen, sonst startet
# Plymouth nicht. Wir setzen zusätzlich GRUB_GFXMODE=1280x800, damit die
# Grafikkonsole + der Splash in 1280x800 laufen (verhindert Umschalten auf Text).
GRUB_CFG="/etc/default/grub"
if [ -f "$GRUB_CFG" ]; then
    sed -i \
        -e 's/^GRUB_CMDLINE_LINUX_DEFAULT=.*/GRUB_CMDLINE_LINUX_DEFAULT="quiet splash loglevel=3 vt.global_cursor_default=0"/' \
        -e 's/^GRUB_CMDLINE_LINUX=.*/GRUB_CMDLINE_LINUX=""/' \
        "$GRUB_CFG"

    if ! grep -q "^GRUB_CMDLINE_LINUX_DEFAULT" "$GRUB_CFG"; then
        echo 'GRUB_CMDLINE_LINUX_DEFAULT="quiet splash loglevel=3 vt.global_cursor_default=0"' >> "$GRUB_CFG"
    fi

    for line in 'GRUB_GFXMODE=1280x800' 'GRUB_GFXPAYLOAD_LINUX=keep' 'GRUB_TIMEOUT=0' 'GRUB_HIDDEN_TIMEOUT=0' 'GRUB_HIDDEN_TIMEOUT_QUIET=true' 'GRUB_TERMINAL_OUTPUT=console'; do
        key="${line%%=*}"
        if grep -q "^${key}=" "$GRUB_CFG" 2>/dev/null; then
            sed -i "s|^${key}=.*|${line}|" "$GRUB_CFG"
        else
            echo "$line" >> "$GRUB_CFG"
        fi
    done
fi

# --- 6c. initramfs + GRUB neu bauen ------------------------------------------
# WICHTIG: Es reicht NICHT, die Theme-/Symlink-Datei zu setzen - danach MUSS die
# initramfs neu gebaut werden, sonst bleibt das alte (Ubuntu-/bgrt-)Theme
# eingebacken. Die Ausgabe wird NICHT unterdrückt, damit Fehler sichtbar sind.
echo ">>> Baue initramfs + GRUB neu (Thema wird eingebacken)..."
update-initramfs -u || true
update-grub 2>/dev/null || true

echo ">>> Splashscreen konfiguriert: Theme='$PLYMOUTH_THEME', Bild=splash.png, GRUB quiet+splash."

# --- 6d. Aktive Verifikation der Plymouth-Module im initramfs -----------------
echo
echo ">>> Prüfe, ob Theme UND script-Modul in der initramfs gelandet sind..."
INITRD="/boot/initrd.img-$(uname -r)"

# Unser Theme nutzt script.so (im plymouth-Basis-Paket, garantiert vorhanden).
# Es braucht KEIN label(-pango).so und KEIN backgrounds.so (gibt es auf noble
# gar nicht), daher ist "plugin ... missing" hier ausgeschlossen, solange
# script.so in der initramfs ist.
MODULES_OK=1
for mod in script.so; do
    if ! lsinitramfs "$INITRD" 2>/dev/null | grep -q "$mod"; then
        echo "!!! FEHLT in initramfs: $mod"
        MODULES_OK=0
    fi
done

# 2) Theme selbst
THEME_OK=0
if lsinitramfs "$INITRD" 2>/dev/null | grep -qi "$PLYMOUTH_THEME"; then
    THEME_OK=1
fi

# 3) Warnen, falls das alte bgrt-/Ubuntu-Logo-Theme noch drin ist (der Grund
#    für "Ubuntu-Logo statt splash.png" beim Boot).
if lsinitramfs "$INITRD" 2>/dev/null | grep -qi "bgrt"; then
    echo "!!! HINWEIS: 'bgrt'-Theme ist noch in der initramfs enthalten."
    echo "    Es zeigt beim Boot das Firmware-/Ubuntu-Logo. Unser Theme MUSS davon"
    echo "    verdrängt werden - siehe Reparatur unten bzw. manuell:"
    echo "      sudo ln -sfn $THEME_PLY /etc/alternatives/default.plymouth"
    echo "      sudo update-initramfs -u && sudo update-grub && sudo reboot"
fi

# Falls etwas fehlt: Reparaturversuch — direktes Setzen des Standard-Themes
# (umgeht den Ubuntu-Bug, dass 'plymouth-set-default-theme' auf Noble fehlt und
# update-alternatives evtl. nicht greift) + erneutes update-initramfs.
if [ "$MODULES_OK" -eq 0 ] || [ "$THEME_OK" -eq 0 ]; then
    echo ">>> Reparatur: setze default.plymouth erneut + update-initramfs..."
    apt-get install -y --no-install-recommends \
        plymouth plymouth-themes \
        2>/dev/null || true

    try_activate

    update-initramfs -u

    MODULES_OK=1
    for mod in script.so; do
        if ! lsinitramfs "$INITRD" 2>/dev/null | grep -q "$mod"; then
            echo "!!! IMMER NOCH fehlend nach Reparatur: $mod"
            MODULES_OK=0
        fi
    done
    if lsinitramfs "$INITRD" 2>/dev/null | grep -qi "$PLYMOUTH_THEME"; then
        THEME_OK=1
    fi
fi

if [ "$MODULES_OK" -eq 1 ] && [ "$THEME_OK" -eq 1 ]; then
    echo ">>> OK: Theme '$PLYMOUTH_THEME' + script.so in der initramfs."
else
    echo "!!! Hinweise zur manuellen Reparatur:"
    echo "        sudo apt-get install -y plymouth plymouth-themes"
    echo "        sudo update-initramfs -u"
    echo "        lsinitramfs $(uname -r)"
    echo "    Dieses Theme nutzt script.so (plymouth-Basis) und braucht KEIN label-pango/backgrounds."
    echo "    Prüfe auch: cat /boot/grub/grub.cfg | grep -i 'quiet splash'"
fi

fi   # Ende: Plymouth OK (else-Zweig)

echo

echo
echo "======================================================================"
echo "  Fertig!"
echo ""
echo "  User   : $MAGICQ_USER"
echo "  HOME   : $MAGICQ_HOME"
echo ""
echo "  Nächste Schritte:"
echo "  1. MagicQ ggf. manuell installieren:"
echo "       sudo dpkg -i magicq_ubuntu_*.deb"
echo "  2. Prüfen:  /opt/magicq/runmagicq.sh"
echo "  3. Openbox-Login testen mit:  startx"
echo "  4. Compact Wing per USB anschließen."
echo "  5. In MagicQ: Setup -> View Settings -> Panels -> FULL PANEL"
echo "  6. DHCP:  sudo netplan apply"
echo "  7. Keine Fensterrahmen: konfiguriert in ~/.config/openbox/rc.xml"
echo "  8. Autologin: Booten -> tty1 -> Openbox -> MagicQ Fullscreen"
echo "  9. Splashscreen: Boot-Logs verdeckt (quiet + splash)"
echo " 10. Qt5 xcb: libxcb-* installiert, QT_QPA_PLATFORM=xcb"
echo " 11. Maus-Cursor: transparent (Python geschriebenes XCursor-Theme) - kein Mauszeiger"
echo "======================================================================"
