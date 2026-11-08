# ArtNet-ETH

4-fach Art-Net/DMX/RDM-Node mit echtem Ethernet auf Basis des
**WIZnet W5100S-EVB-Pico** (RP2040 + W5100S + RJ45).

Das Projekt ist eine Abwandlung von [`ArtNet-USB`](../ArtNet-USB): statt
„Ethernet über USB“ (CDC-NCM) wird hier der onboard-W5100S über SPI
angesteuert. USB dient nur noch als serielle Konsole und zum Flashen.

## Funktionsumfang

- 4 DMX-Ports, jeder einzeln als **Ausgang** (ArtDmx → DMX out) oder
  **Eingang** (DMX in → ArtDmx) konfigurierbar
- vollständiges **Art-Net 4**: ArtPoll/ArtPollReply, ArtDmx, ArtAddress
- **RDM-Gateway** (ArtRdm, OpCode `0x00CC`) auf den Ausgangsports
- **Web-Oberfläche** mit Kanalübersicht und Einstellungen
- **lokales OLED-Menü** mit 4 Tasten (Status, Ports, DHCP, Neustart,
  Werkseinstellungen)
- **DHCP-Client mit statischem Fallback** (Standard: `2.0.0.10/8`)
- kontinuierliche DMX-Ausgabe mit ~33 Hz, auch wenn der Controller nur
  Änderungen sendet

## Hardware

| Bauteil | Funktion |
|---|---|
| U5 | WIZnet W5100S-EVB-Pico (RP2040, W5100S, RJ45) |
| U11–U14 | MAX3485ESA, RS485-Transceiver je DMX-Port |
| U1–U4 | CT3-12AFAP-EP, 3-polige XLR-Einbaubuchse |
| TERM1–TERM4 | DP-1-R-T, zuschaltbare 120-Ω-Terminierung |
| TVS | SM712-02HTG, Überspannungsschutz A/B gegen GND |
| OLED | SSD1306 128×64 an I2C1 |
| KEYS | 4 Tasten gegen GND |

Die DMX-Ports sind als 3-polige XLR-Buchsen ausgeführt. Das ist im
DMX-Umfeld nicht standardkonform (Norm: 5-polig XLR), aber verbreitet.
Belegung: **1 = GND/Schirm, 2 = Data−, 3 = Data+**. Kabel entsprechend
beschriften, damit sie nicht mit symmetrischen Audiokabeln verwechselt
werden.

## Pin-Belegung

| Funktion | GPIO |
|---|---|
| DMX TX / DI (Treiber-Eingang) | GP2, GP3, GP4, GP5 |
| DMX RX / RO (Empfänger-Ausgang) | GP10, GP11, GP12, GP13 |
| DE/RE (Richtung, HIGH = senden) | GP6, GP7, GP8, GP9 |
| OLED SDA | GP26 (I2C1) |
| OLED SCL | GP27 (I2C1) |
| Taste UP | GP14 |
| Taste DOWN | GP15 |
| Taste OK | GP22 |
| Taste BACK | GP28 |
| Status-LED | GP25 |
| W5100S SPI0 MISO / CS / SCK / MOSI | GP16 / GP17 / GP18 / GP19 |
| W5100S RST / INT | GP20 / GP21 |

> **Wichtig:** GP16–GP21 sind auf dem W5100S-EVB-Pico fest mit dem
> onboard-W5100S verbunden und dürfen **nicht** für Tasten oder andere
> Funktionen verwendet werden. Der `KEYS`-Header im ersten
> Schaltbildentwurf lag auf GP17–GP21 und muss auf GP14/15/22/28
> verschoben werden.

### Adressierung der DMX-Ports

Jeder Port hat eine eigene Art-Net-Adresse aus **Net** (0–127),
**Subnet** (0–15) und **Universe** (0–15). Werkseinstellung:
Net 0, Subnet 0, Universe 0–3.

## Netzwerk

Der W5100S wird über `Wiznet5100lwIP` (lwIP, nicht blockierend)
betrieben. Beim Start:

1. Ist DHCP aktiviert, wird bis zu **8 s** auf ein Lease gewartet.
2. Kommt kein Lease, wird die konfigurierte statische Adresse verwendet
   (Standard `2.0.0.10`, Maske `255.0.0.0`).
3. Ohne DHCP (`DHCP client` aus) wird sofort die statische Adresse
   verwendet.

Die Web-Oberfläche ist danach unter `http://<aktuelle-IP>/` erreichbar.
Die aktuell verwendete IP und Maske stehen in der Kopfzeile; im
Formular wird die **statische Fallback-Adresse** bearbeitet.

Art-Net läuft auf UDP-Port **6454**. Broadcasts verwenden die Maske der
aktiven Schnittstelle, nicht die gespeicherte Fallback-Maske.

> Wenn das Netzwerk beim Booten nicht bereit ist, versucht die Firmware
> den Aufbau aus `loop()` erneut. Ein nachträglich eingestecktes Kabel
> führt bei aktivem DHCP allerdings nicht automatisch zu einem neuen
> Lease – in diesem Fall neu starten oder statisch konfigurieren.
>
> Der W5100-Treiber der Arduino-Pico-Bibliothek meldet keinen
> PHY-Linkstatus. Die Anzeige „link up“ im OLED bedeutet daher, dass der
> Netzwerk-Stack bereit ist, nicht zwingend, dass ein Kabel steckt.

## Web-Oberfläche

- **Channels**: 512 Kanäle des gewählten Ports, Suche und „Active only“
- **Settings**:
  - statische Fallback-IP und Netzmaske
  - DHCP-Client ein/aus
  - ShortName/LongName für ArtPollReply
  - pro Port: Richtung (output/input), Net, Subnet, Universe
  - Speichern + Neustart, nur Neustart, Werkseinstellungen

## OLED-Menü und Tasten

| Taste | Funktion |
|---|---|
| UP / DOWN | Statusseite wechseln, Menüeintrag wählen, Wert ändern |
| OK | Menü öffnen, Eintrag auswählen, nächstes Feld |
| BACK | Menü verlassen, Port-Editor beenden und speichern |

Statusseiten: IP/Netzwerkstatus, Portübersicht mit Aktivitätsanzeige,
Systeminformationen (Laufzeit, FPS, DHCP/statisch).

Menüpunkte: Port 1–4 bearbeiten, DHCP ein/aus, Speichern + Neustart,
Werkseinstellungen, Zurück. Eine Änderung der DHCP-Einstellung im Menü
wird gespeichert und ist beim nächsten Neustart aktiv.

## DMX/RDM-Technik

- **DMX-Ausgabe**: eigener PIO/DMA-Transmitter (`DmxOut`) auf `pio0`,
  ein State Machine pro Port, Break/MAB und 250 kbaud nach DMX512.
- **DMX-Eingang**: PIO-UART-Empfänger auf `pio1`, ein State Machine pro
  Port. Die empfangenen Frames werden als ArtDmx gesendet.
- **RDM**: `RdmPort` schaltet DE/RE für die Anfrage auf Senden und
  horcht danach auf die Antwort. ArtRdm wird nur auf Ausgangsports
  beantwortet.
- Alle 8 PIO-State-Machines werden benötigt (4× TX auf `pio0`,
  4× RX auf `pio1`).

## Bauen und Flashen

Voraussetzung: PlatformIO mit dem Arduino-Pico-Core
(`earlephilhower`). Das Board ist auf `wiznet_5100s_evb_pico`
eingestellt.

```sh
pio run -d ArtNet-ETH
pio run -d ArtNet-ETH -t upload
pio device monitor -d ArtNet-ETH
```

Alternativ die `.uf2` aus `.pio/build/w5100s/firmware.uf2` im
BOOTSEL-Modus auf das Board kopieren.

Die serielle Konsole läuft über native USB mit 115200 Baud und zeigt
unter anderem DHCP-Status, verwendete IP, OLED-Erkennung und
Port-Richtungen an.

## Hinweise zum Schaltbild

1. **MAX3485ESA vs. 5-V-Versorgung**: Der MAX3485 ist ein 3,3-V-Typ.
   Wird er im Schaltbild aus 5 V versorgt, ist das außerhalb der
   Spezifikation. Entweder den Transceiver an 3,3 V betreiben oder –
   falls tatsächlich ein 5-V-Typ (MAX485) bestückt wird – die
   RO-Leitung zum RP2040 pegelangepasst/abgesichert führen.
2. **560-Ω-Bias**: A → +3,3 V, B → GND. Bias nur einmal pro Bus
   bestücken. Die 120-Ω-Terminierung (`TERM1..4`) nur an den beiden
   physischen Busenden einschalten, nicht an jedem Node.
3. **XLR3**: siehe oben – 3-polige XLR ist keine normgerechte
   DMX-Verbindung. Pin 2 = Data−, Pin 3 = Data+.
4. **KEYS-Header**: auf GP14/15/22/28 verschieben (siehe
   Pin-Belegung), da GP16–GP21 vom W5100S belegt sind.
5. **TVS/SM712**: A und B über SM712 gegen GND schützen; Schirm von
   GND trennen bzw. nach Norm behandeln.

## Unterschiede zu ArtNet-USB

| | ArtNet-USB | ArtNet-ETH |
|---|---|---|
| Netzwerk | CDC-NCM über USB | W5100S + RJ45 |
| DHCP | DHCP-Server für den Host | DHCP-Client mit statischem Fallback |
| USB | Netzwerk + Konsole | nur Konsole/Flashen |
| Tasten | keine | 4 Tasten + OLED-Menü |
| DMX/RDM | 4 Ports | 4 Ports, gleiche PIO-Aufteilung |

Die DMX/RDM-Bibliotheken (`DmxOut`, `ArtnetRdm`, `ArtnetConfig`) und
der SSD1306-Treiber sind aus den bestehenden Projekten übernommen.
