# MagicQ miniQ on Dell Wyse 3040 – Offline-Installation

Dieser Ordner enthält **alle Pakete**, die `setup_magicq_wyse.sh` auf einem
frischen **Ubuntu 24.04 LTS (noble), amd64** braucht – inklusive Abhängigkeiten.
Installation erfolgt ganz **ohne Internet**.

```
offline/
├── install_offline.sh            # Installer: lokale APT-Quelle aktivieren + Setup ausführen
├── magicq_ubuntu_1.9.8.3.deb     # MagicQ selbst (optional, hier hinterlegt)
├── heads.all                     # ChamSys-Fixture-Datenbank (für MagicQ-Funktionsprüfer)
└── repo/                         # Lokales APT-Repository (flat repository)
    ├── Packages(.gz)             #   Paketindex
    ├── pool/                     #   532 .deb Pakete (~240 MB, inkl. liblzo2-2 für MagicQ)
    │   └── SOURCES.txt           #   Nachweis der ursprünglichen Ubuntu-Quellen
    └── SHA256SUMS                #   Integritätsnachweis aller .deb
```

`heads.all` ist die ChamSys-Fixture-Datenbank (382 MB, Download von
<https://secure.chamsys.co.uk/downloads/files/heads.all>). Sie wird **nicht** vom
Installer gebraucht; in MagicQ lässt sich damit der Fixture-Bestand
aktualisieren (MagicQ → Setup → Fixture Library).

## Verwendung

```bash
# Nach "sudo adduser chamsys" (siehe Haupt-README Abschnitt 5):
cd /media/usb/offline               # Pfad anpassen an eingehängten USB-Stick
sudo ./install_offline.sh                                        # ohne MagicQ
sudo ./install_offline.sh /pfad/magicq_ubuntu_1.9.8.3.deb        # mit MagicQ

# Nur Repo prüfen (nichts installieren):
sudo ./install_offline.sh --check-only

# Netzwerk-Apt-Quellen DANACH wiederherstellen (dauerhaft offline ist Standard):
sudo ./install_offline.sh --restore
```

## Was das Skript tut

1. Sichert und deaktiviert die Netzwerk-Apt-Quellen (Backup in
   `.apt-sources-backup/`).
2. Aktiviert `deb [trusted=yes] file:.../repo ./` als einzige Quelle.
3. `apt-get update` + `apt-get --simulate`-Probe, dass **alle** vom
   Setup-Skript benötigten Pakete auflösbar sind (bricht vor jeder Änderung ab,
   falls etwas fehlt).
4. Führt `setup_magicq_wyse.sh` aus (optional mit MagicQ-`.deb`).

## MagicQ

MagicQ liegt hier als `magicq_ubuntu_1.9.8.3.deb` (198 MB) – `install_offline.sh`
findet es automatisch und installiert es per `dpkg -i` + `apt-get -f install`.
Aktuellere Versionen bekommst du nach Login: <https://www.chamsys.co.uk/mqdownload/>
→ "Ubuntu (64bit)". Ein eigenes `.deb` einfach neben das Skript legen oder als
Argument übergeben, es überschreibt die mitgelieferte Version.

## Paketumfang

Umfasst: Openbox + X11 (`xserver-xorg`, `xinit`, `x11-xserver-utils`), Qt5
(`libqt5core5t64`, `libqt5gui5t64`, …), xcb-Plugin-Bibliotheken (Fix für
"could not load QT plugin xcb"), GStreamer + FFmpeg, ALSA (`libasound2t64`),
Mesa/OpenGL, `udev`/`udisks2` (USB-Auto-Mount), plymouth-Theme-Komponenten,
`x11-apps`, `python3` sowie alle transitiven Abhängigkeiten (`libc6`,
`libstdc++6`, `zlib1g`, …) **und die MagicQ-Laufzeitabhängigkeiten**
(`liblzo2-2`, `liblzma5`, `libx11-6`, …). Die auf noble entfernten Paketnamen
`plymouth-theme-ubuntu-logo`, `plymouth-theme-script` und `xcursor-invisible`
werden vom Setup-Skript tolerant behandelt (getrennte, optionale Versuche).

Stand: noble + noble-updates + noble-security, **2026-10-06**.