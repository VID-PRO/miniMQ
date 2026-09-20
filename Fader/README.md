# DIY MagicQ Compact Mini Connect Wing (Projekthandbuch)

Dieses Handbuch beschreibt den Eigenbau eines Lichtpults, das dem modernen **MagicQ Compact Mini Connect Layout** nachempfunden ist. Das Pult steuert die MagicQ-Software über **Tastaturbefehle (HID-Emulation)** über die integrierte Playback-Shortcuts-Funktion der Software.

> **Hinweis:** Die Firmware verwendet ein **3×13-Tasterraster**. In diesem Handbuch wird die Keymap nach dem **verifizierten** Zustand dokumentiert (per Diagnose-Dump der Firmware geprüft). Die Beschriftung der einzelnen Tastern kann sich an Ihren Bedürfnissen orientieren – maßgeblich ist die Zuordnung Zelle → Tastencode in der Tabelle unten.

## 1. Das Layout (Physische Anordnung)

Das Pult besitzt **3 Tasterreihen à 13 Tasten** und darunter die Fader-Sektion. Oben: die **S-Reihe** (Playback wählen) mit **MASTER GO** ganz rechts. Mitte: die **GO-Reihe**. Unten: die **Flash-Reihe (F1–F10)** mit **SWOP/PREV** links und **MASTER PAUSE** ganz rechts.

```
                                      [ MINI CONNECT WING ]
=====================================================================================================
  ALT  |  NEXT |  S1  |  S2  |  S3  |  S4  |  S5  |  S6  |  S7  |  S8  |  S9  |  S10|  MASTER GO
  DBO  |  NEXT |  GO1 |  GO2 |  GO3 |  GO4 |  GO5 |  GO6 |  GO7 |  GO8 |  GO9 |  GO10|  RELEASE
  SWOP |  PREV |  F1  |  F2  |  F3  |  F4  |  F5  |  F6  |  F7  |  F8  |  F9  |  F10 | MASTER PAUSE
=====================================================================================================

    ||                    ||      ||      ||      ||      ||      ||      ||      ||      ||      ||
  Grand                 [Fader] [Fader] [Fader] [Fader] [Fader] [Fader] [Fader] [Fader] [Fader] [Fader]
  Master                   1       2       3       4       5       6       7       8       9       10
```

### 1.1 Firmware-Keymap (verifiziert)

Die Zuordnung wird in `src/main.cpp` als `MATRIX[3][13]` definiert und per Diagnose-Kombination S10+GO10 (tippt die Zellentabelle in den Editor) geprüft:

| Zelle | Taste(n) | Tastencode | Funktion in MagicQ „Playback shortcuts" |
|-------|----------|-----------|------------------------------------------|
| 0,0   | ALT      | `Ctrl+Alt+0` (halten) | **Dead Black Out (DBO)** kurzschließen |
| 0,1   | NEXT     | `[`        | Nächste Playback-Seite |
| 0,2–0,11 | S1–S10 | `1 2 3 4 5 6 7 8 9 0` | **Select Playback 1–10** |
| 0,12  | MASTER GO | `Space`   | **Manual GO** |
| 1,0   | DBO      | `F11`      | **Dead Black Out** (Mac-Tastaturbelegung) |
| 1,1   | NEXT     | `[`        | Nächste Playback-Seite (2. Key) |
| 1,2–1,11 | GO1–GO10 | `q w e r t y u i o p` | **GO Playback 1–10** |
| 1,12  | RELEASE | `-`       | **Release Playback** |
| 2,0   | SWOP     | `` ` ``    | **Add/Swap** |
| 2,1   | PREV     | `]`        | Vorherige Playback-Seite |
| 2,2–2,11 | F1–F10  | `\ z x c v b n m , .` | **Flash Playback 1–10** (momentan) |
| 2,12  | MASTER PAUSE | `#`  | **Manual STOP** |

**Besondere Verhaltensweisen der Firmware:**

* **Flash (F1–F10):** MagicQ-„Test"-Tasten toggeln das Playback auf 100% – die Firmware sendet daher beim Drücken einen Tastendruck (**an**) und beim Loslassen erneut (**aus**). Ergebnis: ein **momentanes** Flashen, solange der Finger auf der Taste liegt.
* **S + GO (gleiches Playback) = PAUSE/STOP:** Solange S_n und GO_n zusammen gehalten werden, sendet die Firmware die **STOP-Taste** (`a s d f g h j k l ;`). STOP ist in MagicQ ein Toggle – jede Betätigung pausiert/startet. Damit kein „ws"-Geistertastendruck entsteht, wird jede S-/GO-Taste ~40 ms verzögert; die Chord-Erkennung entscheidet, ob STOP oder die Einzeltaste gesendet wird.
* **ALT:** wie ein Modifier gehalten (`Ctrl+Alt+0`), bis die Taste losgelassen wird.
* **MASTER GO / MASTER PAUSE:** liegen bei diesem Layout oben rechts (GO) bzw. unten rechts (PAUSE) statt vertikal gestapelt.

## 2. Benötigte Hardware & Einkaufsliste

* **1x Raspberry Pi Pico** (Standard oder Pico H).
* **1x 74HC4067 Multiplexer Board** (16-Kanal Analog-Multiplexer zum Einlesen der Fader).
* **11x Schiebepotentiometer (Fader)**:
  * **Empfehlung**: Bourns PTA6043 oder ALPS RS6011Y.
  * **Spezifikation**: **10k Ohm, LINEAR** (Wichtig: Gekennzeichnet mit "B10K" oder "10K Lin"). **60mm** Regelweg entspricht dem Original.
* **38x Mechanische Tastenschalter** (3×13-Raster, davon 38 Zellen belegt, 1 Reserve):
  * **Empfehlung**: Cherry MX Black oder Cherry MX Red (lineares Tippgefühl ohne hörbares Klicken für schnelles, lautloses Flashen).
* **38x Tastenkappen (Keycaps)**: Standardgröße 1U. Farbschema-Vorschlag: Grau (S), Blau (Flash), Grün (GO).
* **38x Dioden**: Modell **1N4148** (Unabdingbar zur Vermeidung von "Ghosting" in der Tastermatrix).

## 3. Hardware-Verkabelung

### 3.1 Die Fader (Über den 74HC4067 Multiplexer)
Der Pico besitzt nur 3 analoge Eingänge (ADC), wir benötigen für 10 Playbacks + 1 Grand Master jedoch 11 Eingänge. Der Multiplexer schaltet diese extrem schnell auf einen einzigen Pin um.

* **Stromversorgung Fader**: Verbinden Sie den linken Pin aller 11 Fader mit **3V3(OUT)** (Pico Pin 36) und den rechten Pin aller Fader mit **GND** (z. B. Pico Pin 38).
* **Signalleitungen Fader**: Der mittlere Pin (Schleifer) von Fader 1 bis 10 geht an die Eingänge **C1 bis C10** des Multiplexers. Der Grand Master Fader geht an den ersten Eingang **C0**.
* **Multiplexer an Raspberry Pi Pico**:
  * **VCC** -> 3V3 (Pico Pin 36)
  * **GND** -> GND (Pico Pin 38)
  * **SIG / COM** -> **GP27** (Pico Pin 32 / ADC1)
  * **S0** -> **GP17** (Pico Pin 22)
  * **S1** -> **GP18** (Pico Pin 24)
  * **S2** -> **GP19** (Pico Pin 25)
  * **S3** -> **GP20** (Pico Pin 26)

### 3.2 Die Tastermatrix (3x13 Matrix) & Dioden-Schaltplan
Um Pins zu sparen, verdrahten wir die Tasten in 3 Zeilen (Rows) und 13 Spalten (Columns). An jedem Schalter wird eine Diode **1N4148** angelötet, um Fehlauslösungen beim gleichzeitigen Drücken mehrerer Tasten (Ghosting) zu verhindern.

**Es gibt zwei gültige Kombinationen aus Dioden-Richtung und Scan-Polarität — die Abschnitte müssen zusammenpassen!**

**Variante A "Active-Low" (Original, Dioden-Kathode Richtung Row):**
Diode: Anode Richtung Column, **Kathode (schwarzer Ring) Richtung Row/Switch**. Scaling: Zeilen auf LOW, Spalten mit internen Pull-ups, Taste erkannt bei LOW.

```text
Zeilen-Leitung (Row von Pico-Ausgang, z.B. GP2)
       |   (Ausgang, wird beim Scannen auf LOW gezogen)
       |
       v
       +-----------------------+
                               |
                        [Cherry MX Switch]
                               |
                               | (Anderer Pin des Schalters)
                               v
                             \   /  (1N4148 Diode)
                              \ /
                             -----  (Anode, kein Ring)
                               |
                               v
                       (Kathode / schwarzer Ring)
                               |
                               v
Spalten-Leitung (Column zu Pico-Eingang, z.B. GP6)
```

Leitpfad beim Tastendruck: **Column** (Pull-up, HIGH) -> **Anode** -> **Kathode** -> **Switch** -> **Row** (LOW). Die Diode leitet den Strom von der Spalte zur niedrigen Zeile und zieht die Spalte auf LOW.

**Variante B "Active-High" (Standard bei vielen PCBs, Dioden-Kathode Richtung Column):**
Diode: **Kathode (schwarzer Ring) Richtung Column**, Anode Richtung Row/Switch. Scaling: Zeilen auf HIGH, Spalten mit internen Pull-downs, Taste erkannt bei HIGH.

```text
Zeilen-Leitung (Row von Pico-Ausgang, z.B. GP2)
       |   (Ausgang, wird beim Scannen auf HIGH gezogen)
       |
       v
       +-----------------------+
                               |
                        [Cherry MX Switch]
                               |
                               | (Anderer Pin des Schalters)
                               v
                             \   /  (1N4148 Diode)
                              \ /
                             -----  (Kathode / schwarzer Ring)
                               |
                               |
                               v
Spalten-Leitung (Column zu Pico-Eingang, z.B. GP6)
```

Leitpfad beim Tastendruck: **Row** (HIGH) -> **Switch** -> **Anode** -> **Kathode** -> **Column** (Pull-down, LOW). Die Diode leitet den Strom von der hohen Zeile zur Spalte und zieht die Spalte auf HIGH.

**Achtung bei bestehender Verdrahtung:** Falls die Dioden bereits gelötet sind und Richtung **Column** zeigen (Originalhandbuch), müssen Sie **nicht** umlöten — die **C++-Firmware (`src/main.cpp`) scannt bereits Active-High** und funktioniert damit. Die CircuitPython-Variante (`firmware/code.py`) scannt Active-Low und benötigt Dioden Richtung Row.

**Pinbelegung am Pico für die Matrix:**
* **Zeilen (Ausgänge)**: Row 0 (S-Reihe) = **GP4**, Row 1 (GO-Reihe) = **GP2**, Row 2 (Flash-Reihe) = **GP3**
* **Spalten (Eingänge)**: Spalte 1 bis 13 an **GP6 bis GP16, GP21, GP22**

## 4. Installation & Bootloader flashen

### Variante A: CircuitPython (`firmware/code.py`)

1. Laden Sie die aktuelle stabile `.uf2`-Datei für den Raspberry Pi Pico von **circuitpython.org/downloads** herunter.
2. Halten Sie die **BOOTSEL**-Taste auf dem Pico gedrückt und schließen Sie ihn per USB an den PC an.
3. Lassen Sie die Taste los. Ein Laufwerk namens `RPI-RP2` öffnet sich.
4. Ziehen Sie die heruntergeladene `.uf2`-Datei auf das Laufwerk. Der Pico startet neu und heißt ab jetzt **`CIRCUITPY`**.
5. Kopieren Sie den Quellcode aus `firmware/code.py` direkt auf das `CIRCUITPY`-Laufwerk.

### Variante B: PlatformIO / C++ (`platformio.ini` im Projekt-Root)

Die C++-Version ist die **latency-optimierte** Variante (kompiliert zu nativem ARM-Code) und nutzt das eingebaute USB-HID-Keyboard des **earlephilhower Arduino-Cores** — derselbe Ansatz wie im Schwesterprojekt `chamsys-encoder`.

```bash
# Projekt öffnen und bauen:
cd Documents/Arduino/chamsys-wing
pio run           # baut firmware.uf2

# Auf den Pico flashen (BOOTSEL-Haltezustand nötig):
pio run -t upload
```

Die erzeugte Datei liegt unter `.pio/build/pico/firmware.uf2` (alternativ per Drag & Drop auf das `RPI-RP2`-Laufwerk, nachdem der Pico im BOOTSEL-Modus gestartet wurde).

## 5. Einrichtung in MagicQ

1. Starten Sie MagicQ, wählen Sie **Setup** -> **View Settings**.
2. Wechseln Sie in den Reiter **Keypad Encoders**.
3. Suchen Sie die Zeile **MagicQ PC Keyboard Mode** und ändern Sie diese auf **Playback shortcuts**.
4. Stellen Sie das Betriebssystem Ihres PCs für die DIY-Tastatur idealerweise auf das **US-Tastaturlayout** um, damit Sonderzeichen wie `@` fehlerfrei vom Pico eingetippt werden können.
5. **macOS-Tipp**: Falls die physische Shift-Taste des Mac sporadisch aussetzt, solange der Pico als zusätzliche USB-Tastatur angeschlossen ist — das ist ein bekanntes macOS-Verhalten bei mehreren HID-Tastaturen (der Pico injiziert Shift für Zeichen wie `#` und `@`). Die Firmware sendet deshalb Kleinbuchstaben (MagicQ wertet Keycodes case-insensitiv aus). Wer es gar nicht will, trennt den Pico, wenn nur am Mac-Board getippt wird.
6. **Linux/Windows-Hinweis**: Die Flash-Tasten (F1–F10) funktionieren auf dem Mac, **nicht** auf dem PC (MagicQ-PC hat in den „Playback shortcuts" keine F-Tasten-Bindung). Die Firmware nutzt daher stattdessen die **Test-Tasten** (`\ z x c v b n m , .`), die auf jedem System funktionieren und togglen (100% Playback an/aus). Die Firmware kompensiert das Toggeln automatisch zu **momentanem Flash**.

## Projektstruktur

```
chamsys-wing/
├── README.md            # Dieses Handbuch
├── platformio.ini       # PlatformIO-Konfiguration (earlephilhower Core)
├── src/
│   └── main.cpp         # C++ Quellcode (latency-optimiert, USB-HID)
└── firmware/
    └── code.py          # CircuitPython Quellcode (alternative Variante)
```
