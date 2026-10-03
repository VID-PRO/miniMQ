// DIY MagicQ Compact Mini Connect Wing
// Raspberry Pi Pico + 74HC4067 mux + 3x13 key matrix + USB HID keyboard
// PlatformIO (earlephilhower Arduino core, native TinyUSB HID)

// Temporäre Fader-Deaktivierung (Testing ohne Fader): auf 1 setzen zum Wiedereinschalten
#define ENABLE_FADERS 1

// ADC-Rohwert-Scan beim Boot (Diagnose der Mux-Verdrahtung) auf 1 setzen.
#define FADER_BOOT_SCAN 1

// Loop-Diagnose: die ersten FADER_LOOP_DIAG_MS millisekunden nach dem Boot
// alle Fader-Kanäle ca. alle 200 ms als Rohwert + Prozent ausgeben, OHNE
// Filter-/Bewegungslogik. Zeigt die Idle-Signalform (oszilliert sie?) direkt.
#define FADER_LOOP_DIAG 0
#define FADER_LOOP_DIAG_MS 5000

// Idle-Guard: Beim Boot wird jeder Kanal einmal mit kurzem (30 us) und einmal
// mit vollem Settle (3000 us) gemessen. Echter Poti: beide identisch (±Drift).
// Unbestückter/schwimmender Eingang: divergiert massiv und wird weggesperrt
// (keine Wheel-Events), bis er nächsten Boot wieder normal liest.
// FADER_IDLE_SPREAD = zulässige Abweichung in % zwischen den beiden Messungen.
#define FADER_IDLE_GUARD 1
#define FADER_IDLE_SPREAD 10

// Bewegungs-Diagnose: bei jeder bestätigten Fader-Bewegung ein DEBUG-Log
// (pct/delta/acc/steps), um Verluste zwischen ADC-Wert und Wheel-Steps zu sehen.
#define FADER_MOV_DEBUG 0

// Kalibrierung der Raster-Prozent-Beziehung (DROP-OUT-FREI):
// auf 1 setzen, flashen, NICHTS anfassen. Startwert des PB1-On-Screen-Faders
// notieren. Die FW sendet nach ~2s exakt 40 Rasten (25 ms Abstand) auf PB1.
// Endwert notieren -> Rasten% = (End-Start)/40. Danach auf 0 setzen und
// FADER_NOTCH_TENTH (unten) anpassen.
#define FADER_NUDGE_CAL 0

// Deterministischer Sperr-Lock: Bitmaske über den LOGISCHEN Fader-Index f
// (Bit N = Fader f=N sperren). KB: Die Mux-Eingänge ch4-ch7 (logisch f3-f6,
// also PB4-PB7) sind physisch NICHT verdrahtet (schwimmend, laden über Sekunden
// nach, driften ±2 %). Auch f07 (physisch ch3) lädt nach 5s auf ~70 % hoch und
// togglt als Phantom. Solche Kanäle spucken im Leerlauf Wheel-Events.
// Auf 0 setzen, sobald diese Fader wirklich angelötet sind.
#define FADER_FORCE_LOCK ((1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7))

// Temporärer HID-Selbsttest: auf 1 setzen, flashen, Cursor in TextEdit setzen,
// Pico neu einstecken. Die Firmware tippt nach 5 s "HID-OK" in den fokussierten
// Editor. Danach wieder auf 0 setzen.
#define HID_SELFTEST 0

// Matrix-Polarität: 0 = active-high (Zeilentreiber HIGH beim Scannen, Spalten
// mit INPUT_PULLDOWN, Taste = HIGH), 1 = active-low (Zeilentreiber LOW, Spalten
// mit INPUT_PULLUP, Taste = LOW). Gut fuer getauschte Diodenrichtung.
#define MATRIX_POL 0

#if MATRIX_POL == 0
#define MATRIX_ACTIVE    HIGH
#define MATRIX_IDLE      LOW
#define MATRIX_PULL      INPUT_PULLDOWN
#define MATRIX_PRESSED(v) ((v) == HIGH)
#else
#define MATRIX_ACTIVE    LOW
#define MATRIX_IDLE      HIGH
#define MATRIX_PULL      INPUT_PULLUP
#define MATRIX_PRESSED(v) ((v) == LOW)
#endif

// ============================================================
// ABSOLUTE MOUSE (ON-SCREEN-FADER PER MAUSRAD)
// ============================================================
// Die 11 Fader sollen zuerst den Mauszeiger per ABSOLUTER Maus auf den
// korrespondierenden On-Screen-Fader (PB1..PB10 + GM) am unteren Rand des
// MagicQ-Touchscreens setzen und dort dann das Mausrad scrollen, um den
// Faderwert zu ändern. Dazu wird ein zweites HID-Device registriert: eine
// absolute Maus (digitizer-style), die den ganzen Bildschirm in
// X=0..32767 / Y=0..32767 abbildet - identisch zum Encoder-Wing.
//
// WICHTIG (macOS): Die Maus muss als ERSTER HID-Report registriert werden
// (ordering 8, vor dem Keyboard 10/11), sonst wird der Cursor nie bewegt.
// (Code steht nach den Includes weiter unten.)

// ============================================================
// ON-SCREEN-FADER-ZIELPOSITIONEN (KALIBRIERUNG)
// ============================================================
// MagicQ im Vollbild/Maximiert: die On-Screen-Fader (Playback-Boxen) liegen am
// unteren Rand des Touchscreens. Zielkoordinaten (absolut 0..32767) stehen
// weiter unten nach den Includes in FADER_TARGET_X/Y.
// Index 0 = GM, 1..10 = PB1..PB10.
//
// KALIBRIEREN: FADER_CALIBRATE=1 setzen und flashen. Der Cursor wandert dann
// nacheinander auf alle 11 Zielpunkte (blinkt jeweils Startnummer 0..10, alle
// 4 s weiter). Sobald jede LED-Nummer sicher über dem richtigen On-Screen-
// Fader landet, die Koordinaten unten eintragen und FADER_CALIBRATE=0 setzen.
#define FADER_CALIBRATE 0

// ============================================================
// FADER -> MAUSRAD-UMSETZUNG
// ============================================================
// Die Fader werden NICHT mehr als binäre TEST-Trigger, sondern analog
// ausgewertet: Jede Faderbewegung nach oben erhöht den Wert des On-Screen-
// Faders per Mausrad-Scroll (+), nach unten verringert ihn (-). Die
// Umsetzung ist proportional: FADER_WHEEL_PCT pro Scroll-Raste.
constexpr int FADER_MAX_DELTA_PCT = 90;  // nur noch Extrem-Sprünge (~Ganzweg in 1 Scan) verwerfen
constexpr int FADER_DEADBAND     = 0;    // jede Nicht-Null-Abweichung zählt als Bewegung
constexpr int FADER_CONFIRM_HOLD = 2;    // Toleranz, ab der ein Pending als "stehen geblieben" gilt
// Grosse Spruenge werden SOFORT bestaetigt (kein Pending-Warten): der Fader
// soll auf einen schnellen Zug ohne ~2 Scan Verzoegerung reagieren. Der
// Pending-Filter bleibt fuer kleine Bewegungen (Geister-Filter).
constexpr int FADER_IMMEDIATE_PCT = 5;
// Skalierung ADC-Prozent -> Wheel-Rasten. Akku fährt in 0.1%-Einheiten.
// Kalibriert (drop-out-frei): 40 Rasten = 16 % On-Screen -> 1 Raste = 0.40 %.
// FADER_NOTCH_TENTH = 0.1%-Einheiten pro Raste (4 = 0.4%).
constexpr int FADER_NOTCH_TENTH  = 4;
// MagicQ verarbeitet maximal ~40 Wheel-Events/s (gemessen: 25 ms = 100 %
// Zustellung, 20 ms = 81 %, 12 ms = 56 %, 8 ms = 38 %). Der Drain sendet daher
// global getaktet EINEN Report pro FADER_WHEEL_INTERVAL_MS -> verlustfrei.
constexpr int FADER_WHEEL_INTERVAL_MS = 25;

// ============================================================
// FADER -> ABSOLUTE WERT-EINGABE (KEYBOARD-MODE, empfohlen)
// ============================================================
// Statt Mausrad-Scroll (MagicQ verarbeitet nur ~40 Wheel-Reports/s -> ~16 %/s
// Nachlauf) kann der Fader seinen Wert DIREKT per MagicQ-Tastatur-Befehl
// setzen. Verifiziertes Schema:
//     <Playback-Auswahl>   '@'   <Wert>   <Enter>
//     z.B. Taste "1"  dann AltGr+Q  dann 60  dann Numpad-Enter  -> PB1@60.
// Der Wert wird absolut übernommen, es gibt weder Drift noch Report-Verlust;
// auch grosse Sprünge (0->100) sind exakt (wenige Befehle, MagicQ springt absolut).
// FADER_OUT_MODE: 1 = SET (Tastatur), 0 = WHEEL (Mausrad).
// (Die eine globale Definition steht beim DRAG-Block weiter unten.)
// Hysterese: erst neu tippen, wenn Ziel und letzter getippter Wert um mind.
// FADER_SET_STEP % auseinanderliegen (verhindert "1@60 1@60..."-Spam).
constexpr int FADER_SET_STEP = 2;
// Maximaler Sprung pro SET-Befehl (Ziel-LastTyped, beide Richtungen).
// 25 = voller 0->100-Weg in ~6 Befehlen, fein bleibt die Sprungfolge exakt.
constexpr int FADER_SET_JUMP = 25;
// Tastatur-Tempo: Abstand zwischen zwei Tastendrücken + Pause nach Enter.
constexpr unsigned long FADER_KEY_MS = 15;
constexpr unsigned long FADER_CMD_GAP_MS = 40;
// '@'-Zeichen. Tastatur-Layout (FUR DIESE FIRMWARE ENFACH UEBERSCHRIEBEN):
//   - US-Layout: '@' = Shift+2, ueber die en_US-ASCII-Tabelle ('@', RALT=0).
//   - Mac/macOS DEUTSCHE Belegung: 'l' (+Option) = '@'  (RALT=1)
//   - Windows DEUTSCHE Belegung: 'q' (+AltGr) = '@'      (RALT=1)
// (Geraet ist auf US-Layout, daher Standard = '@' ohne RALT.)
#define FADER_AT_CODE '@'
#define FADER_AT_RALT 0
// Enter der MagicQ-Befehlszeile: Numpad-Enter. ACHTUNG: Keyboard.press() will
// fuer Nicht-ASCII-Tasten den Wert 136+Usage. Numpad-Enter = Usage 0x58 -> 0xB8.
#define FADER_ENTER_CODE (136 + 0x58)
// Linux: Bei ausgeschaltetem NumLock geben Numpad-Usages Pfeil-/Navigations-
// Funktionen statt Ziffern. Mit FADER_LINUX_NUMLOCK=1 wird beim Boot der
// NumLock-Zustand ueber den LED-Report gelesen und NUR bei Bedarf (aus) einmalig
// getoggelt. (Auf macOS/Windows unnoetig, aber unschaedlich.)
#define FADER_LINUX_NUMLOCK 1

// ============================================================
// FULL-PANEL-FADER PER MAUS ZIEHEN (Modus DRAG)
// ============================================================
// Im Full Panel (MagicQ-Hauptansicht mit Playback-Fadern) wird der On-Screen-
// Fader direkt angefasst und gezogen:
//     1. absolute Maus auf den Fader (FADER_DRAG_X, Mitte der Faderbahn)
//     2. linke Maustaste DRUECKEN
//     3. auf die Hoehe springen, die zum ADC-Prozentwert passt
//        (Y = FADER_DRAG_Y_TOP + (100-pct)/100 * (Y_BOTTOM - Y_TOP))
//     4. loslassen
// Ein Ziehen pro bestaetigter Faderbewegung; Restschritte laufen im selben
// Draw weiter, bis der Cursor exakt auf der Zielhoehe steht. Der Weg ist
// beschleunigt (FADER_DRAG_GAIN), weil MagicQ/Windows ein Absolutmaus-Drag
// verlangsamt verarbeitet; FADER_DRAG_GAIN=1 = originalgetreuer Weg.
// FADER_OUT_MODE: 2 = DRAG (Full-Panel-Ziehen), 1 = SET (Befehlszeile),
//                 0 = WHEEL (Mausrad).
#define FADER_OUT_MODE 2
// Full-Panel-Faderbahn: X je logischem Fader (Index 1..10 = PB1..PB10, 0 = GM
// unbenutzt) sowie Y fuer 100 % (oben) und 0 % (unten) in absolutem Screen-
// Raum 0..32767.
// KALIBRIEREN: FADER_CALIBRATE=1, flashen -> Cursor blinkt nacheinander auf
// die 11 X-Punkte (4 s je Punkt, Index sichtbar). X-Werte hier eintragen,
// dann vertikal kalibrieren: PB1 auf 100 % (oben) und auf 0 % (unten).
// KALIBRIERT (Full Panel, interaktisch via FADER_CALIBRATE=2 ermittelt):
// PB1-Mitte X1=2800, PB10-Mitte X10=25300 -> Abstand exakt 2500 je Fader.
// Index 0 = Grand Master (sitzt links vom Sub Master), eigene Y-Bahn.
const int FADER_DRAG_X[11] = { 400,   2800,  5300, 7800, 10300, 12800,
                               15300, 17800, 20300, 22800, 25300 };
constexpr int FADER_DRAG_Y_TOP    = 26800;  // Y bei 100 % (PB1..PB10)
constexpr int FADER_DRAG_Y_BOTTOM = 29300;  // Y bei   0 % (PB1..PB10)
// Grand-Master-Bahn: X = FADER_DRAG_X[0] = 400, eigene Y-Werte (kalibriert).
constexpr int FADER_DRAG_GM_Y_TOP    = 26850;
constexpr int FADER_DRAG_GM_Y_BOTTOM = 29400;
// Beschleunigung des Ziehwegs (1 = original). Groessere Werte = kuerzere
// Mausbewegung pro Fader-Weg, damit MagicQ mitkommt (Drag-Verarbeitung ist
// verlangsamt). Siehe FADER_DRAG_DEBUG zum Nachjustieren.
constexpr float FADER_DRAG_GAIN = 8.0f;
// Ziehweg pro bestaetigte Bewegung begrenzen (Cursor-Stufen).
constexpr int FADER_DRAG_MAXSTEP = 800;
// Wann der gehaltene Griff wieder losgelassen wird: nach FADER_DRAG_HOLD_IDLE_MS
// ohne Bewegung (Finger steht) oder sicherheitshalber nach HOLD_MAX_MS.
constexpr unsigned long FADER_DRAG_HOLD_IDLE_MS = 90;
constexpr unsigned long FADER_DRAG_HOLD_MAX_MS  = 1200;
// Debug: jede Drag-Aktion protokollieren (zum Kalibrieren/Justieren).
#define FADER_DRAG_DEBUG 1

#include <Arduino.h>
#include <Keyboard.h>
#include <USB.h>
#include <tusb.h>
#include <tusb-hid.h>
#include "class/hid/hid_device.h"
#include <LittleFS.h>

// ============================================================
// KALIBRIERDATEN (Laufzeit, persistent in LittleFS)
// ============================================================
// Die Full-Panel-Positionen kommen aus dem Flash (siehe loadFaderCal/saveFaderCal);
// ohne Datei gelten die kompilierten Defaults. Neu kalibrieren: Pico mit
// gedrueckter DBO-Taste starten -> Kalibrier-UI, NEXT Page speichert.
struct FaderCal {
    uint16_t x[11];    // X-Mitte je Fader (0 = GM, 1..10 = PB1..PB10)
    uint16_t yTop;     // Y bei 100 % (PB)
    uint16_t yBot;     // Y bei   0 % (PB)
    uint16_t gmTop;    // Y bei 100 % (GM)
    uint16_t gmBot;    // Y bei   0 % (GM)
};

static const FaderCal FADER_CAL_DEFAULT = {
    { 400, 2800, 5300, 7800, 10300, 12800, 15300, 17800, 20300, 22800, 25300 },
    26800, 29300, 26850, 29400
};
static FaderCal fcal = FADER_CAL_DEFAULT;

#define FADER_CAL_FILE "/fcal.bin"

bool loadFaderCal(void) {
    if (!LittleFS.begin()) return false;
    File f = LittleFS.open(FADER_CAL_FILE, "r");
    if (!f || f.size() != (int)sizeof(FaderCal)) {
        if (f) f.close();
        LittleFS.end();
        return false;
    }
    FaderCal tmp;
    size_t got = f.read((uint8_t *)&tmp, sizeof(tmp));
    f.close();
    LittleFS.end();
    if (got != sizeof(tmp)) return false;
    fcal = tmp;
    return true;
}

bool saveFaderCal(void) {
    if (!LittleFS.begin()) return false;
    File f = LittleFS.open(FADER_CAL_FILE, "w");
    if (!f) { LittleFS.end(); return false; }
    size_t put = f.write((const uint8_t *)&fcal, sizeof(fcal));
    f.close();
    LittleFS.end();
    return put == sizeof(fcal);
}

void printFaderCal(const char *tag) {
    Serial.printf("[CAL]%s X:", tag);
    for (uint8_t i = 0; i < 11; i++) Serial.printf(" %d", (int)fcal.x[i]);
    Serial.printf("  YTOP=%d YBOT=%d GMTOP=%d GMBOT=%d\n",
                  (int)fcal.yTop, (int)fcal.yBot, (int)fcal.gmTop, (int)fcal.gmBot);
}

// ============================================================
// ABSOLUTE MOUSE IMPLEMENTATION (s. Konfig oben)
// ============================================================
static const uint8_t desc_abs_mouse[] = { TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(1)) };
static uint8_t absMouseLocalID = 0;
static bool absMouseRunning = false;

// On-Screen-Fader-Zielkoordinaten (Index 0 = GM, 1..10 = PB1..PB10).
const int16_t FADER_TARGET_X[] = { 2000, 2000, 6500, 9500, 12500, 15500, 18500, 21500, 24000, 26500, 29000 };
const int16_t FADER_TARGET_Y[] = { 29000, 29000, 29000, 29000, 29000, 29000, 29000, 29000, 29000, 29000, 29000 };

void absMouseBegin() {
  if (absMouseRunning) return;
  USB.disconnect();
  absMouseLocalID = USB.registerHIDDevice(desc_abs_mouse, sizeof(desc_abs_mouse), 8, 0x0002);
  USB.connect();
  absMouseRunning = true;
}

// Sendet Position+x-Achsen-Wheel+Buttons in EINEM HID-Report (absolut).
// x,y = 0..32767 (Bildschirm links/oben nach rechts/unten).
#define MOUSE_LEFT 0x01
void absMouseReport(int16_t x, int16_t y, int8_t wheel, uint8_t buttons) {
  if (!absMouseRunning) {
    Serial.printf("[ABSMOUSE] skip: not running\n");
    return;
  }
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 1000) {
    lastLog = millis();
    Serial.printf("[ABSMOUSE] attempt rid=%u buttons=%u x=%d y=%d wheel=%d mounted?%d\n",
                  USB.findHIDReportID(absMouseLocalID), buttons, x, y, wheel, (int)tud_mounted());
  }
  CoreMutex m(&USB.mutex);
  tud_task();
  if (tud_hid_ready()) {
    tud_hid_abs_mouse_report(USB.findHIDReportID(absMouseLocalID), buttons, x, y, wheel, 0);
  }
  tud_task();
}

// Cursor auf Punkt setzen, ohne zu klicken (Kalibrierung).
void absMouseMove(int16_t x, int16_t y) {
  absMouseReport(x, y, 0, 0);
}

// ============================================================
// Pin mapping (see README)
// ============================================================
// Mux select bits (74HC4067)
constexpr uint8_t MUX_S0 = 17;
constexpr uint8_t MUX_S1 = 18;
constexpr uint8_t MUX_S2 = 19;
constexpr uint8_t MUX_S3 = 20;
constexpr uint8_t MUX_ADC = A1;      // GP27 / ADC1

// Key matrix: 3 rows (outputs), 13 cols (inputs, pull-down)
// Reihenfolge = physische Verdrahtung (gemessen): GP4=obere S-Reihe,
// GP2=mittlere GO-Reihe, GP3=untere Flash/ALT-Reihe.
constexpr uint8_t ROW_PINS[3]  = {4, 2, 3};
constexpr uint8_t COL_PINS[13] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 21, 22};

constexpr uint8_t NUM_FADERS = 11;   // 0 = Grand Master, 1-10 = Playbacks

// Fader-Polarität: 0 = normal (hoch = hoher ADC-Wert), 1 = invertiert
// (physikalisch nach oben schieben = höherer %). Passend zur Poti-Verdrahtung.
#define FADER_INVERT 1

// Fader-Kanalreihenfolge: 0 = Fader f an Mux-Kanal f (0=GM, 1..10=PB1..10).
// 1 = gespiegelt an der Mux-Halfte (0=GM an ch10, 1=PB1 an ch9, ... 10=PB10 an ch0).
#define FADER_CH_REVERSE 1

// ============================================================
// Timing-Konstanten (hier zentral einstellbar)
// ============================================================
constexpr uint32_t KEY_DEBOUNCE_MS = 25;   // Matrixtaste: Zustand muss so lange stabil sein
constexpr uint32_t CHORD_MS        = 40;   // S+GO-Fenster ("Pause statt Einzeltaste")
constexpr uint32_t KEEPALIVE_MS    = 60;   // Intervall zum Neu-Senden gehaltener Tasten

// ============================================================
// FADER IM "MAUSEAD"-MODUS (stufenlos per On-Screen-Fader)
// ============================================================
// Die Fader steuern die On-Screen-Fader am unteren MagicQ-Touchscreenrand:
// Jede Faderbewegung positioniert den Cursor per ABSOLUTER Maus auf den
// zugehörigen On-Screen-Fader (PB1..PB10, GM) und scrollt dort proportional
// mit dem Mausrad. Bewegung nach oben -> Faderwert +, nach unten -> Faderwert -.
// Robust gegen Cursor-Sprünge: Settling-Skipping in readFaderPct() + EMA +
// Glitch-Abweisung in updateFaderState().
constexpr uint8_t FADER_SAMPLES = 12;     // ADC-Reads pro Fader (nach Settling-Verwurf)
constexpr uint8_t FADER_ENDSTOP = 1;      // % innerhalb dieses Rands wird auf 0/100 geschnappt

// ============================================================
// Keymap: datengetriebene Tabelle aus Zellcode + Zelltyp.
// Zelltypen steuern das Verhalten (einfache Taste / Flash-Toggle /
// DBO-Kombination / S- und GO-Taste der S+GO-Akkordlogik).
// ============================================================
// MagicQ "Playback shortcuts" (läuft auf Mac + PC/Linux):
//   S1..S10 = 1..0, GO = Q..P, Flash = '\' z x c v b n m , .
//   SWOP = '`', MASTER PAUSE = '#', MASTER GO = Space,
//   DBO (Phys.) = F11, NEXT = '[', PREV = ']',
//   ALT = DBO-Schnellbefehl: Control + Option + 0 (wie Modifier halten)
// Flash-Tasten sind MagicQ "Test"-Keys: sie TOGGLEN das Playback 100% an/aus.
// Daher senden wir beim DRUECKEN die Taste (an) und beim LOSLASSEN
// die Taste erneut (aus) -> das ergibt ein momentanes Flash.
enum KeyType : uint8_t {
    KT_NONE  = 0,   // unbenutzte Zelle
    KT_KEY   = 1,   // einfache Taste, solange gehalten
    KT_FLASH = 2,   // MagicQ Test-Key: Toggle bei Druck + Toggle bei Loslassen
    KT_DBO   = 3,   // ALT -> Controller-Paste: Control+Option+0 halten
    KT_SEL   = 4,   // S1..S10 (Select): nimmt an der S+GO-Akkordlogik teil
    KT_GO    = 5,   // GO1..GO10: nimmt an der S+GO-Akkordlogik teil
};

struct KeyCell { unsigned char code; uint8_t type; };

// Zellen-Tabelle per Dump verifiziert (Keycap -> Zelle r.c):
//   ALT=(0,0), S1..S10=(0,2..11), MASTER GO=(0,12),
//   DBO=(1,0), GO1..GO10=(1,2..11), RELEASE=(1,12),
//   SWOP=(2,0), PREV=(2,1), Flash F1..F10=(2,2..11), MASTER PAUSE=(2,12)
constexpr KeyCell KEYMAP[3][13] = {
    // ALT                      NEXT          S1    S2    S3    S4    S5    S6    S7    S8    S9    S10   MASTER GO
    { {0, KT_DBO},              {'[', KT_KEY},
      {'1', KT_SEL}, {'2', KT_SEL}, {'3', KT_SEL}, {'4', KT_SEL}, {'5', KT_SEL},
      {'6', KT_SEL}, {'7', KT_SEL}, {'8', KT_SEL}, {'9', KT_SEL}, {'0', KT_SEL},
      {' ', KT_KEY} },
    // DBO                       NEXT          GO1   GO2   GO3   GO4   GO5   GO6   GO7   GO8   GO9   GO10  RELEASE
    { {KEY_F11, KT_KEY},         {'[', KT_KEY},
      {'q', KT_GO}, {'w', KT_GO}, {'e', KT_GO}, {'r', KT_GO}, {'t', KT_GO},
      {'y', KT_GO}, {'u', KT_GO}, {'i', KT_GO}, {'o', KT_GO}, {'p', KT_GO},
      {'-', KT_KEY} },
    // SWOP                      PREV          F1    F2    F3    F4    F5    F6    F7    F8    F9    F10   MASTER PAUSE
    { {'`', KT_KEY},             {']', KT_KEY},
      {'\\', KT_FLASH}, {'z', KT_FLASH}, {'x', KT_FLASH}, {'c', KT_FLASH},
      {'v', KT_FLASH}, {'b', KT_FLASH}, {'n', KT_FLASH}, {'m', KT_FLASH},
      {',', KT_FLASH}, {'.', KT_FLASH}, {'#', KT_KEY} },
};
// Hinweis: GO wird in Kleinbuchstaben gesendet (MagicQ matched den Keycode,
// nicht die Shift-Taste). Das Vermeiden von Shift-Events reduziert die
// macOS-Stoerung der physischen Shift-Taste bei mehreren USB-Tastaturen.

// S + GO (desselben Playbacks) gleichzeitig = STOP/PAUSE auf diesem Playback.
// MagicQ-Keyboard "S+GO" ist ein Schritt (ohne Fade), kein Pause - daher
// senden wir stattdessen die STOP-Taste (A S D F G H J K L ;). STOP ist ein
// Toggle: jede Betätigung schaltet Play/Pause um. Prioritaet: Jede S-/GO-Taste
// eines Playbacks wird ~40ms gehalten, ob der Partner mitkommt. Kommt er,
// wird NIE eine einzelne S-/GO-Taste gesendet (kein ws-Stroezeug), sondern nur
// die STOP-Taste. Kommt er nicht, wird die Taste nach dem Fenster gesendet.
const unsigned char STOP_KEYS[10] = {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'};
const unsigned char SEL_KEYS[10]  = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
const unsigned char GO_KEYS[10]   = {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'};
const uint32_t chordMs = CHORD_MS;
static uint8_t  pairState[10];       // 0=idle, 1=S unten, 2=GO unten, 3=Chord(STOP)
static uint32_t soloSince[10];       // Zeitpunkt des Solo-Druckes
static bool     keySent[10][2];      // [S,GO] aktuell am Host gedrueckt

// ---- State ----
#if ENABLE_FADERS
static int       faderLastPct[NUM_FADERS];  // Referenz/Roh-%; -1 = Boot-Sentinel
static int       faderAcc[NUM_FADERS];      // Akkumulator für proportionale Wheel-Steps
static int8_t    faderPending[NUM_FADERS];  // Richtungs-Persistenz: 0=kein, +1/-1 richtung
static int       faderPendingPct[NUM_FADERS]; // Kandidaten-Ziel des gemerkten Sprungs
static bool      faderLocked[NUM_FADERS];   // Idle-Guard: schwimmender/instabiler Kanal
#if FADER_OUT_MODE != 0
static int       faderTarget[NUM_FADERS];   // anzuzeigender absoluter %-Wert (DRAG/SET)
#endif
#if FADER_OUT_MODE == 1
static int       faderLastTyped[NUM_FADERS];// zuletzt per Keyboard-Befehl getippt
#endif
#endif
static bool      keyState[3][13];
static bool      keyRaw[3][13];
static uint32_t  keySince[3][13];

// Gibt die Playback-Nummer (1..10) fuer die Zelle (r,c) zurueck, oder 0.
int playbackIndex(uint8_t r, uint8_t c) {
    if ((r == 0 || r == 1) && c >= 2 && c <= 11) return (int)c - 1;
    return 0;
}

// Type a full text string as USB key events, e.g. "PB5 @ 42"
void typeText(const char *s) {
    while (*s) {
        Keyboard.write(*s);
        s++;
    }
}

void blinkLed() {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
}

void debugKey(const char *event, unsigned char key) {
    Serial.print(event);
    Serial.print(" key=0x");
    Serial.println(key, HEX);
}

// Tippt die aktuell gedrueckten Zellen als "M R2C3 R0C1" in den Editor.
// Nur Ziffern und R,C -> layoutsicher (auch auf deutscher Tastatur).
void dumpMatrixText() {
    typeText("M");
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], MATRIX_ACTIVE);
        delayMicroseconds(10);
        for (uint8_t c = 0; c < 13; c++) {
            if (KEYMAP[r][c].type == KT_NONE) continue;
            if (MATRIX_PRESSED(digitalRead(COL_PINS[c]))) {
                char buf[8];
                snprintf(buf, sizeof(buf), " R%dC%d", r, c);
                typeText(buf);
            }
        }
        digitalWrite(ROW_PINS[r], MATRIX_IDLE);
    }
    typeText("\n");
}

#if ENABLE_FADERS
// Gibt alle Fader-Zustaende als " GM=42% PB1=87% ..." aus (analoge Werte).
void dumpFaderText() {
    typeText("F");
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        char buf[16];
        if (f == 0) {
            snprintf(buf, sizeof(buf), " GM=%d%%", faderLastPct[f]);
        } else {
            snprintf(buf, sizeof(buf), " PB%d=%d%%", f, faderLastPct[f]);
        }
        typeText(buf);
    }
    typeText("\n");
}

// Liest einen Fader-Kanal des Mux mit Mittelung über mehrere ADC-Reads und
// Endstop-Schnapp auf 0/100. Gibt 0..100 zurück. 'f' ist der logische Fader
// (0=GM, 1..10=PB1..10), der auf den physischen Mux-Kanal gemappt wird.
//
// Robust: Die ersten FADER_SKIP_READS Messwerte nach dem Kanalwechsel werden
// verworfen (Mux/Signalleitungs-Settling), erst der Rest wird gemittelt.
// Settling-Zeit nach dem Kanalwechsel. Determiniert die Scan-Dauer und damit
// die Reaktionszeit des Faders (11 Fader x Settle). 1500 us reicht fuer die
// verdrahteten Poti-Kanaele; Newtonschaetzung war 3000 us.
constexpr uint16_t FADER_SETTLE_US = 1500;
constexpr uint8_t FADER_SKIP_READS = 5;  // erst so viele ADC-Reads verwerfen (Settling)
int readFaderPct(uint8_t f) {
#if FADER_CH_REVERSE
    uint8_t ch = (uint8_t)(NUM_FADERS - 1 - f);
#else
    uint8_t ch = f;
#endif
    digitalWrite(MUX_S0, (ch & 1) ? HIGH : LOW);
    digitalWrite(MUX_S1, (ch & 2) ? HIGH : LOW);
    digitalWrite(MUX_S2, (ch & 4) ? HIGH : LOW);
    digitalWrite(MUX_S3, (ch & 8) ? HIGH : LOW);
    delayMicroseconds(FADER_SETTLE_US);               // signal settling (Mux/ADC)

    uint32_t sum = 0;
    int n = 0;
    for (uint8_t s = 0; s < FADER_SAMPLES; s++) {
        int raw = analogRead(MUX_ADC);
        if (s < FADER_SKIP_READS) continue;         // Settling-Samples verwerfen
        if (raw < 100 || raw > 65530) continue;     // einzelner Ausreisser (Guard-Rail)
        sum += raw;
        n++;
        delayMicroseconds(10);                      // Abstand zwischen Reads
    }
    if (n == 0) n = 1;
    int pct = (int)((sum * 100L) / ((uint32_t)n * 65535L));
#if FADER_INVERT
    pct = 100 - pct;
#endif

    // Potis erreichen selten exakt 0/100: nahe den Enden sauber einklemmen.
    if (pct <= FADER_ENDSTOP)        pct = 0;
    else if (pct >= 100 - FADER_ENDSTOP) pct = 100;
    return pct;
}

// Messung mit nur 30 us Settle und Mittelung über 3 Reads. Reines Diagnose-
// Werkzeug für den Idle-Guard: ein echter Poti antwortet sofort (Wert muss mit
// readFaderPct übereinstimmen), ein schwebender Mux-Eingang füllt den ADC nicht
// und driftet je nach Settle-Zeit weit auseinander.
int readFaderPctShort(uint8_t f) {
#if FADER_CH_REVERSE
    uint8_t ch = (uint8_t)(NUM_FADERS - 1 - f);
#else
    uint8_t ch = f;
#endif
    digitalWrite(MUX_S0, (ch & 1) ? HIGH : LOW);
    digitalWrite(MUX_S1, (ch & 2) ? HIGH : LOW);
    digitalWrite(MUX_S2, (ch & 4) ? HIGH : LOW);
    digitalWrite(MUX_S3, (ch & 8) ? HIGH : LOW);
    delayMicroseconds(30);

    uint32_t sum = 0;
    int n = 0;
    for (uint8_t s = 0; s < 3; s++) {
        int raw = analogRead(MUX_ADC);
        if (raw < 100 || raw > 65530) continue;
        sum += raw;
        n++;
        delayMicroseconds(100);
    }
    if (n == 0) n = 1;
    int pct = (int)((sum * 100L) / ((uint32_t)n * 65535L));
#if FADER_INVERT
    pct = 100 - pct;
#endif
    if (pct <= FADER_ENDSTOP)        pct = 0;
    else if (pct >= 100 - FADER_ENDSTOP) pct = 100;
    return pct;
}

// Baut den Akkumulator in Raten ab (GETAKTET): global höchstens EIN Wheel-Report
// pro FADER_WHEEL_INTERVAL_MS. MagicQ wertet maximal ~40 Wheel-Events/s aus;
// enger getaktete Bursts verliert es proportional. Mit 25 ms Abstand (0.4 %
// je Report -> ~16 %/s) ist die Zustellung verlustfrei nachgeweisn (NUDGE-CAL).
// Der Rest bleibt im Acc und wird in den Folgescans verwendet.
void drainFader(uint8_t f) {
#if FADER_IDLE_GUARD
    if (faderLocked[f]) return;
#endif
    if (f == 0) return;
    int steps = faderAcc[f] / FADER_NOTCH_TENTH;
    if (steps == 0) return;
    static uint32_t lastWheel = 0;
    if (millis() - lastWheel < (uint32_t)FADER_WHEEL_INTERVAL_MS) return;
    int8_t w = (steps > 0) ? 1 : -1;
    faderAcc[f] -= w * FADER_NOTCH_TENTH;
    lastWheel = millis();
    absMouseReport(FADER_TARGET_X[f], FADER_TARGET_Y[f], w, 0);
}

// ============================================================
// SET-MODUS (FADER_OUT_MODE == 1): absolute Wert-Eingabe per Keyboard
// ============================================================
// Tippt pro Fader einen MagicQ-Befehl in die Befehlszeile:
//     <Playback-Auswahl>'@'<Wert><Numpad-Enter>   z.B. "1@60<Enter>"
#if FADER_OUT_MODE == 1
struct SetStep { uint8_t code; bool ralt; };

static SetStep  setSteps[7];
static uint8_t  setLen = 0;
static uint8_t  setFader = 0;
static uint8_t  setIdx = 0;
static uint32_t setLastKey = 0;
static uint32_t setIdleUntil = 0;
static bool     setActive = false;

// Numpad-Ziffern fuer die WERT-Eingabe (MagicQ liest den Zahlenwert vom
// Nummernblock). Keyboard.press() braucht 136+Usage: Keypad 0 = Usage 0x62,
// Keypad 1..9 = 0x59..0x61.
static const uint8_t FADER_KP_DIGITS[10] = {
    136 + 0x62, // 0
    136 + 0x59, 136 + 0x5A, 136 + 0x5B, 136 + 0x5C, 136 + 0x5D,
    136 + 0x5E, 136 + 0x5F, 136 + 0x60, 136 + 0x61  // 1..9
};

// Baut den Befehl "SEL(obere Reihe) '@' <wert über Numpad> <Numpad-Enter>"
// fuer Fader f auf.
void buildSetCommand(uint8_t f, int value) {
    int v = value;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    setLen = 0;
    setSteps[setLen++] = { SEL_KEYS[f - 1], false };               // Playback toppen (1..9,0)
    setSteps[setLen++] = { FADER_AT_CODE, (FADER_AT_RALT > 0) };   // '@' (AltGr+Q)
    char b[4];
    snprintf(b, sizeof(b), "%d", v);
    for (char *p = b; *p; p++) setSteps[setLen++] = { FADER_KP_DIGITS[*p - '0'], false };
    setSteps[setLen++] = { FADER_ENTER_CODE, false };              // Numpad-Enter
    setFader = f;
    setIdx = 0;
    setActive = true;
}

// Einzelner Tastendruck (ggf. mit RALT-Flankierung fuer '@').
void tapSetStep(const SetStep &s) {
    if (s.ralt) Keyboard.press(KEY_RIGHT_ALT);
    Keyboard.press(s.code);
    Keyboard.release(s.code);
    if (s.ralt) Keyboard.release(KEY_RIGHT_ALT);
}

// Jeden Loop: einen Tastendruck des laufenden Befehls abarbeiten (getaktet),
// sonst den Fader mit dem groessten Restbedarf rundum bedienen.
void updateSetOutput(void) {
#if FADER_OUT_MODE != 1
    return;
#endif
#if FADER_OUT_MODE != 1
    return;
#endif
    if (setActive) {
        if ((uint32_t)(millis() - setLastKey) < FADER_KEY_MS) return;
        tapSetStep(setSteps[setIdx]);
        setIdx++;
        setLastKey = millis();
        if (setIdx == setLen) {
            setActive = false;
            setIdleUntil = millis() + FADER_CMD_GAP_MS;
        }
        return;
    }
    // Pause nach dem letzten Enter (Befehlszeile leeren lassen).
    if ((uint32_t)(millis() - setIdleUntil) < FADER_KEY_MS) return;
    static uint8_t rr = 0;
    for (uint8_t n = 0; n < NUM_FADERS; n++) {
        uint8_t f = (rr + n) % NUM_FADERS;
        if (f == 0) continue;
#if FADER_IDLE_GUARD
        if (faderLocked[f]) continue;
#endif
        int diff = faderTarget[f] - faderLastTyped[f];
        if (diff > FADER_SET_STEP || diff < -FADER_SET_STEP) {
            int step = diff;
            if (step > FADER_SET_JUMP) step = FADER_SET_JUMP;
            else if (step < -FADER_SET_JUMP) step = -FADER_SET_JUMP;
            // Ziel des naechsten Befehls sofort merken -> naechster Durchlauf
            // rechnet den neuen Rest diff neu; der Fader steigt 25 -> 50 -> 75 -> 100.
            int newTyped = faderLastTyped[f] + step;
            faderLastTyped[f] = newTyped;
            rr = (f + 1) % NUM_FADERS;
            buildSetCommand(f, newTyped);
            return;
        }
    }
}
#endif // FADER_OUT_MODE == 1

#if FADER_LINUX_NUMLOCK
// LED-Report der Host-Tastatur: aktueller NumLock-Zustand (Linux-relevant).
static volatile bool kbdLedNum  = false;
static volatile bool kbdLedGot  = false;
static bool numlockEnsureDone   = false;
static uint32_t numlockGiveUpAt = 0;

void kbdLedCb(bool numlock, bool capslock, bool scrolllock, bool compose,
              bool kana, void *cbData) {
    (void)capslock; (void)scrolllock; (void)compose; (void)kana; (void)cbData;
    kbdLedNum = numlock;
    kbdLedGot = true;
}

// Einmalig nach dem Mount: NumLock einschalten, falls der Host meldet, dass
// er AUS ist. Kein blindes Toggle (wuerde einen bereits aktiven NumLock
// ausschalten); ohne LED-Report nach 5 s still aufgeben.
void ensureNumlock(void) {
#if FADER_OUT_MODE == 1
    if (numlockEnsureDone) return;
    if (!tud_mounted()) return;
    if (kbdLedGot && !kbdLedNum) {
        Keyboard.press(KEY_NUM_LOCK);
        Keyboard.release(KEY_NUM_LOCK);
        numlockEnsureDone = true;
        return;
    }
    if (kbdLedGot && kbdLedNum) { numlockEnsureDone = true; return; }
    if (numlockGiveUpAt == 0) numlockGiveUpAt = millis() + 5000;
    if ((uint32_t)(millis() - numlockGiveUpAt) > 0) numlockEnsureDone = true;
#endif
}
#endif

// ============================================================
// DRAG-MODUS (FADER_OUT_MODE == 2): Full-Panel-Fader per Maus ziehen
// ============================================================
// Je bestaetigter Faderbewegung wird der On-Screen-Fader einmal angefasst
// (linke Maustaste) und auf die Zielhoehe gezogen (losgelassen). Der Rest des
// Wegs laeuft in den Folgedraws weiter, bis der Cursor auf der Zielhoehe steht.
static int16_t  dragCurY[NUM_FADERS];   // aktuelle Cursor-Hoehe auf der Faderbahn
static bool     dragPrimed[NUM_FADERS]; // Cursor steht auf der Bahn (X korrekt)
static uint32_t dragLast = 0;           // letzter Drag-Schritt (Zeit)

// Soll-Hoehe fuer Fader f bei Ziel-Prozent pct (0..100). Fader 0 = GM mit
// eigener Bahn.
int16_t dragTargetY(uint8_t f, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int32_t top = (f == 0) ? (int32_t)fcal.gmTop : (int32_t)fcal.yTop;
    int32_t bot = (f == 0) ? (int32_t)fcal.gmBot : (int32_t)fcal.yBot;
    int32_t span = bot - top;
    return (int16_t)(top + span * (100 - pct) / 100);
}

void updateDragOutput(void) {
#if FADER_OUT_MODE != 2
    return;
#endif
    static uint8_t rr = 0;
    static uint8_t heldFader = 0xFF;    // welcher Fader gerade "gegriffen" ist
    static uint32_t heldSince = 0;
    static uint32_t heldMoveAt = 0;     // letzter Cursor-Schritt waehrend des Griffs

    // 1) Loslassen, wenn nichts mehr bewegt wird (Finger steht) oder der
    //    Griff zu lange haelt (Sicherheit, falls MagicQ den Griff verliert).
    if (heldFader != 0xFF) {
        uint8_t h = heldFader;
        int wantH = dragTargetY(h, faderTarget[h]);
        bool atTarget = (dragCurY[h] == wantH);
        uint32_t idle = millis() - heldMoveAt;
        bool timeout = (millis() - heldSince) > FADER_DRAG_HOLD_MAX_MS;
        if ((atTarget && idle >= FADER_DRAG_HOLD_IDLE_MS) || timeout) {
            absMouseReport(fcal.x[h], dragCurY[h], 0, 0);   // loslassen
#if FADER_DRAG_DEBUG
            Serial.printf("DRAG-release f%02d y=%d (%s)\n", h, dragCurY[h],
                          timeout ? "timeout" : "idle");
#endif
            heldFader = 0xFF;
        }
    }

    for (uint8_t n = 0; n < NUM_FADERS; n++) {
        uint8_t f = (rr + n) % NUM_FADERS;      // Index 0 = Grand Master ist dabei
#if FADER_IDLE_GUARD
        if (faderLocked[f]) { dragPrimed[f] = false; continue; }
#endif
        int want = dragTargetY(f, faderTarget[f]);

        // Anderer Fader braucht den Griff? Dann erst den aktuellen loslassen.
        if (heldFader != 0xFF && heldFader != f && dragCurY[heldFader] != want) {
            continue;
        }
        if (!dragPrimed[f]) {
            // Auf die Bahn bringen (Cursor auf die Fadenmitte).
            int mid = ((f == 0) ? (int)fcal.gmTop + (int)fcal.gmBot
                                : (int)fcal.yTop + (int)fcal.yBot) / 2;
            absMouseReport(fcal.x[f], mid, 0, 0);
            dragCurY[f] = want;
            dragPrimed[f] = true;
#if FADER_DRAG_DEBUG
            Serial.printf("DRAG-prime f%02d tgt=%d%% y=%d\n", f, faderTarget[f], want);
#endif
            rr = (f + 1) % NUM_FADERS;
            return;
        }
        if (dragCurY[f] == want) continue;                 // steht schon richtig
        if ((uint32_t)(millis() - dragLast) < 15) return;  // Draw-Tempo

        // Ziehweg: Restweg, gespraengelt geschaerft (Gain) und begrenzt.
        int deltaY = want - dragCurY[f];
        int step = (int)(deltaY / FADER_DRAG_GAIN);
        if (step == 0) step = (deltaY > 0) ? 1 : -1;
        if (step > FADER_DRAG_MAXSTEP) step = FADER_DRAG_MAXSTEP;
        if (step < -FADER_DRAG_MAXSTEP) step = -FADER_DRAG_MAXSTEP;

        int16_t fromY = dragCurY[f];
        int16_t toY = (int16_t)(dragCurY[f] + step);
        dragCurY[f] = toY;
        dragLast = millis();
        heldMoveAt = dragLast;

        if (heldFader != f) {
            // Erst greifen: Cursor auf die aktuelle (modellierte) Hoehe setzen
            // und die linke Maustaste druecken. Ab hier bleibt der Griff
            // durchgehend gedrueckt -> Faderhoehe = Cursorhoehe, kein
            // Click-Jump mehr bei jedem Schritt.
            absMouseReport(fcal.x[f], fromY, 0, MOUSE_LEFT);
            delay(6);
            heldFader = f;
            heldSince = millis();
            heldMoveAt = millis();
#if FADER_DRAG_DEBUG
            Serial.printf("DRAG-grab f%02d at y=%d (tgt %d%% want %d)\n", f, fromY,
                          faderTarget[f], want);
#endif
        }
        absMouseReport(fcal.x[f], toY, 0, MOUSE_LEFT);   // ziehen
#if FADER_DRAG_DEBUG
        Serial.printf("DRAG f%02d tgt=%d%% %d->%d (want %d)\n", f, faderTarget[f],
                      fromY, toY, want);
#endif
        rr = (f + 1) % NUM_FADERS;
        return;
    }
}

// Aktualisiert einen Fader analog: Jede Bewegung wandert in parallele,
// proportionale Wheel-Steps auf dem zugehörigen On-Screen-Fader.
//
// Prinzip (Richtungs-Persistenz, gegen Ghosting):
//  - Der gelesene Roh-%-Wert wird direkt gegen eine Referenz gemessen.
//  - Einzelne Sensor-Artifakte (|Delta| > FADER_MAX_DELTA_PCT) verwerfen.
//  - Richtungs-Persistenz: Jede Bewegung ab der Deadband wird zunaechst als
//    "Pending" gemerkt (Richtung + Kandidaten-Ziel), die Referenz BLEIBT am
//    Ausgangspunkt. Der NAECHSTE Scan bestaetigt den Sprung nur, wenn er
//        (a) in DIESELBE Richtung weitergeht ODER
//        (b) nahe beim Kandidaten-Ziel stehen bleibt.
//    Ghosting (A->B->A) kehrt zurueck -> weder a noch b -> Pending wird
//    verworfen, NICHT gesendet. Echte Bewegung wird so von Flackern getrennt.
//  - Bei Bestaetigung faellt delta = pct - Vor-Pending-Referenz an = der KOMPLETTE
//    Sprung, wird in faderAcc gelegt; Schrittrest bleibt erhalten (proportional).
//  - Kein Endstopp-Burst noetig: 100 % Faderweg = 250 Wheel-Steps =
//    0..100 % On-Screen (MagicQ-Kalibrierung, 0.4 %/Report). Proportional
//    ergibt exakte Endlagen, nachgefuehrt mit ~16 %/s (MagicQ-Report-Limit).
//  - Boot: faderLastPct = -1 (Sentinel). Der allererste Scan legt die Referenz
//    fest und sendet NICHTS (verhindert riesige Delta nach Mux-Warm-Up).
void updateFaderState(uint8_t f) {
    // Index 0 = Grand Master: wird im DRAG-Modus mitgezogen (eigene Bahn in
    // FADER_DRAG_X[0] / _GM_Y_*). Nur im WHEEL-Modus bleibt GM aussen vor.
#if FADER_OUT_MODE == 0
    if (f == 0) return;
#endif

#if FADER_IDLE_GUARD
    // Idle-Guard: Kanal beim Boot als instabil markiert (kurzer != langer
    // Settle -> schwimmender Eingang). Referenz mitschleppen, aber KEIN
    // einziges Wheel-Event senden, bis der Fader mal wieder normal bootet.
    if (faderLocked[f]) {
        faderLastPct[f] = readFaderPct(f);
        return;
    }
#endif

    int pct = readFaderPct(f);

    // Boot-Sentinel: erster Scan = Referenz, keine Reaktion.
    if (faderLastPct[f] < 0) {
        faderLastPct[f] = pct;
        faderAcc[f] = 0;
        faderPending[f] = 0;
        faderPendingPct[f] = 0;
#if FADER_OUT_MODE != 0
        // Boot-Position als Ziel merken, damit beim Boot NICHTS angetippt bzw.
        // nicht gezogen wird (fremdes Flackern im MagicQ vermeiden). Erst die
        // naechste Bewegung synchronisiert.
        faderTarget[f] = pct;
#endif
#if FADER_OUT_MODE == 1
        faderLastTyped[f] = pct;
#endif
        return;
    }

    int delta = pct - faderLastPct[f];

    // Sensor-Artifakt: Sprung >40 % in einem Scan -> Sample verwerfen,
    // Referenz folgt, damit kein Rest-Delta haengend bleibt.
    if (delta > FADER_MAX_DELTA_PCT || delta < -FADER_MAX_DELTA_PCT) {
        faderLastPct[f] = pct;
        faderPending[f] = 0;
        faderPendingPct[f] = 0;
        return;
    }

    bool confirmed = false;
    if (faderPending[f] != 0 && faderPendingPct[f] >= 0) {
        // Persistenz-Check gegen den Kandidaten-Sprung. Delta ist hier gegen
        // die VOR-Pending-Referenz -> bei Bestaetigung = voller Sprung.
        // "continued" = MINDESTENS so weit wie das Kandidaten-Ziel in derselben
        // Richtung (teilweiser Rueckzug zaehlt nicht).
        bool continued = (faderPending[f] > 0 && pct >= faderPendingPct[f]) ||
                         (faderPending[f] < 0 && pct <= faderPendingPct[f]);
        bool held = (pct >= faderPendingPct[f] - FADER_CONFIRM_HOLD) &&
                    (pct <= faderPendingPct[f] + FADER_CONFIRM_HOLD);
        if (continued || held) {
            confirmed = true;
        }
        // Pending loesen (bestaetigt oder verworfen).
        faderPending[f] = 0;
        faderPendingPct[f] = -1;
        if (!confirmed) {
            // Ghosting: zurueck an die Referenz / andere Richtung. Referenz
            // folgt leise, NICHTS senden.
            faderLastPct[f] = pct;
            return;
        }
    } else if (delta > FADER_DEADBAND || delta < -FADER_DEADBAND) {
        // GROSSER Sprung -> sofort bestaetigen (kein Pending): schnelle Zuege
        // reagieren ohne Verzoegerung.
        if (delta >= FADER_IMMEDIATE_PCT || delta <= -FADER_IMMEDIATE_PCT) {
            confirmed = true;
        } else {
            // Erste kleine Abweichung: als Pending merken, aber NICHT senden.
            // Referenz bleibt am Ausgangspunkt, damit delta bei Bestaetigung
            // komplett ist.
            faderPending[f] = (delta > 0) ? 1 : -1;
            faderPendingPct[f] = pct;
            return;
        }
    } else {
        // Rauschklein: Referenz folgt leise.
        faderLastPct[f] = pct;
        return;
    }

    // Bestaetigte Bewegung: Ziel merken (DRAG/SET-Modus absolut bzw. Wheel-Akku).
    faderLastPct[f] = pct;
#if FADER_OUT_MODE == 0
    faderAcc[f] += delta * 10;                    // Zehntel-Prozent, kein Direkt-Send hier
#else
    faderTarget[f] = pct;                        // absolutes Ziel (DRAG/SET)
    faderAcc[f] = 0;
#endif
#if FADER_MOV_DEBUG
    Serial.printf("MOV f%02d pct=%d d=%d acc=%d @%ums\n", f, pct, delta,
                  faderAcc[f], (unsigned)millis());
#endif
}
#endif // ENABLE_FADERS

// ============================================================
// Kalibrier-UI (blockierend): Cursor mit den Wing-Tasten stellen,
// Werte live per Serial ausgeben.
//   S1..S7 waehlen: X(PB1) | X(PB10) | Y(100%) | Y(0%) | X(GM) |
//                   Y(GM,100%) | Y(GM,0%)
//   F1 = -500, F2 = -50, F3 = +50, F4 = +500
//   NEXT Page = speichern (nur wenn allowSave) und zurueck zum Betrieb
// PB2..PB9 ergeben sich aus dem Abstand X(PB10)-X(PB1).
// ============================================================
#if ENABLE_FADERS && FADER_OUT_MODE == 2
static const char *CAL_ITEM[7] = {
    "X PB1", "X PB10", "Y PB 100%", "Y PB 0%", "X GM", "Y GM 100%", "Y GM 0%"
};

void faderCalUi(bool allowSave) {
    Serial.println("[CAL] S1=X(PB1) S2=X(PB10) S3=Y(PB,100%) S4=Y(PB,0%) S5=X(GM) S6=Y(GM,100%) S7=Y(GM,0%)");
    Serial.println("[CAL] F1 -500  F2 -50  F3 +50  F4 +500");
    if (allowSave) Serial.println("[CAL] NEXT Page = speichern");
    printFaderCal(" start");

    int calX1 = fcal.x[1], calX10 = fcal.x[10];
    int calYTop = fcal.yTop, calYBot = fcal.yBot;
    int calXGm = fcal.x[0];
    int calYGmTop = fcal.gmTop, calYGmBot = fcal.gmBot;
    int calSel = 0;
    bool calPrev[3][13];
    memset(calPrev, 0, sizeof(calPrev));
    uint32_t calLastPrint = 0;

    while (true) {
        for (uint8_t r = 0; r < 3; r++) {
            digitalWrite(ROW_PINS[r], MATRIX_ACTIVE);
            delayMicroseconds(10);
            for (uint8_t c = 0; c < 13; c++) {
                bool pressed = (digitalRead(COL_PINS[c]) == MATRIX_PRESSED(true));
                bool hit = pressed && !calPrev[r][c];
                calPrev[r][c] = pressed;
                if (!hit) continue;
                if (r == 0 && c == 1 && allowSave) {       // NEXT Page = SPEICHERN
                    fcal.x[1] = calX1; fcal.x[10] = calX10;
                    fcal.yTop = calYTop; fcal.yBot = calYBot;
                    fcal.x[0] = calXGm;
                    fcal.gmTop = calYGmTop; fcal.gmBot = calYGmBot;
                    // PB2..PB9 liegen exakt zwischen PB1 und PB10
                    for (uint8_t i = 2; i <= 9; i++) {
                        fcal.x[i] = (uint16_t)(calX1 + ((int32_t)calX10 - calX1) * (i - 1) / 9);
                    }
                    printFaderCal(" saved");
                    Serial.println(saveFaderCal() ? "[CAL] -> LittleFS ok"
                                                 : "[CAL] -> LittleFS FEHLER");
                    return;
                }
                if (r == 0 && c >= 2 && c <= 8) {           // S1..S7 = Auswahl
                    calSel = c - 2;
                    blinkLed();
                    Serial.printf("[CAL] %s\n", CAL_ITEM[calSel]);
                } else if (r == 2 && c >= 2 && c <= 5) {    // F1..F4 = verschieben
                    int amt = (c == 2) ? -500 : (c == 3) ? -50 : (c == 4) ? 50 : 500;
                    int *slot = (calSel == 0) ? &calX1 : (calSel == 1) ? &calX10 :
                                (calSel == 2) ? &calYTop : (calSel == 3) ? &calYBot :
                                (calSel == 4) ? &calXGm : (calSel == 5) ? &calYGmTop :
                                &calYGmBot;
                    *slot += amt;
                    if (*slot < 0) *slot = 0;
                    if (*slot > 32767) *slot = 32767;
                    blinkLed();
                }
            }
            digitalWrite(ROW_PINS[r], MATRIX_IDLE);
        }
        int cx = (calSel == 1) ? calX10 : (calSel == 4) ? calXGm : calX1;
        int cy = (calSel == 0) ? calYTop : (calSel == 1) ? calYBot :
                 (calSel == 2) ? calYTop : (calSel == 3) ? calYBot :
                 (calSel == 4) ? calYGmTop : (calSel == 5) ? calYGmTop : calYGmBot;
        absMouseMove(cx, cy);
        if ((uint32_t)(millis() - calLastPrint) > 800) {
            calLastPrint = millis();
            Serial.printf("[CAL] %-10s  X1=%d X10=%d YTOP=%d YBOT=%d XGM=%d GMTOP=%d GMBOT=%d\n",
                          CAL_ITEM[calSel], calX1, calX10, calYTop, calYBot,
                          calXGm, calYGmTop, calYGmBot);
        }
        delay(5);
    }
}

// DBO beim Boot gedrueckt? Zwei Abfragen im Abstand (Entprellung).
bool dboHeldAtBoot(void) {
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(ROW_PINS[1], MATRIX_ACTIVE);
        delayMicroseconds(50);
        bool on = (digitalRead(COL_PINS[0]) == MATRIX_PRESSED(true));
        digitalWrite(ROW_PINS[1], MATRIX_IDLE);
        if (!on) return false;
        delay(30);
    }
    return true;
}
#endif

void setup() {
    // Onboard LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Boot-Beweis: 3 kurze Blinks, sobald die Firmware läuft (ohne jede Taste)
    for (uint8_t i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    }

    // Debug output (USB serial, 115200)
    Serial.begin(115200);
    uint32_t serialWait = millis();
    while (!Serial && (millis() - serialWait < 1000)) { delay(10); }
    Serial.println("chamsys-wing boot OK");

    // Mux select pins
#if ENABLE_FADERS
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
    analogReadResolution(16);
    pinMode(MUX_ADC, INPUT);

    for (uint8_t i = 0; i < NUM_FADERS; i++) {
        faderLastPct[i] = -1;          // Boot-Sentinel: erster Scan legt Referenz fest
        faderAcc[i]     = 0;
        faderPending[i] = 0;
        faderPendingPct[i] = 0;
        faderLocked[i]  = false;
    }

#if FADER_BOOT_SCAN
    // Boot-Diagnose: Roh- und %-Wert jedes Mux-Kanals prüft die Verdrahtung
    // (Fader = Poti: hochziehen des Werten muss im Prozentwert sichtbar sein).
    delay(1500);
    Serial.println("Fader ADC scan (ch raw%):");
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
#if FADER_CH_REVERSE
        uint8_t ch = (uint8_t)(NUM_FADERS - 1 - f);
#else
        uint8_t ch = f;
#endif
        digitalWrite(MUX_S0, (ch & 1) ? HIGH : LOW);
        digitalWrite(MUX_S1, (ch & 2) ? HIGH : LOW);
        digitalWrite(MUX_S2, (ch & 4) ? HIGH : LOW);
        digitalWrite(MUX_S3, (ch & 8) ? HIGH : LOW);
        delayMicroseconds(30);
        uint16_t raw = analogRead(MUX_ADC);
        int pct = (int)((raw * 100L) / 65535L);
#if FADER_INVERT
        pct = 100 - pct;
#endif
        Serial.printf("  ch%d raw=%5u %d%%\n", ch, raw, pct);
    }
#endif
#endif

#if FADER_IDLE_GUARD
    // Idle-Guard (läuft NACH dem Boot-Scan/dem 1.5s-Warm-Up): kurzer vs.
    // langer Settle. Echter Poti antwortet in us und liest bei beiden gleich.
    // Schwimmende Mux-Eingänge sind nach dem Warm-Up noch niedrig (short ~1%)
    // und laden beim langen Settle nach -> Divergenz -> sperren. Zusätzlich
    // sperrt der deterministische FADER_FORCE_LOCK bekannte Leer-Kanäle.
    Serial.println("IDLE-GUARD (short vs long settle):");
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        int s = readFaderPctShort(f);
        int l = readFaderPct(f);
        bool spread = (abs(s - l) > FADER_IDLE_SPREAD);
        bool forced = ((FADER_FORCE_LOCK >> f) & 1u) != 0;
        faderLocked[f] = (spread || forced);
        Serial.printf("  f%02d short=%d%% long=%d%% %s\n", f, s, l,
                      faderLocked[f] ? "LOCKED" : "ok");
    }
    if (FADER_FORCE_LOCK) {
        Serial.print("  FORCE-LOCK aktiv fuer: ");
        for (uint8_t f = 0; f < NUM_FADERS; f++) {
            if ((FADER_FORCE_LOCK >> f) & 1u) Serial.printf("f%02d ", f);
        }
        Serial.println();
    }
#endif

    // Matrix rows (outputs, inactive = MATRIX_IDLE)
    for (uint8_t p : ROW_PINS) { pinMode(p, OUTPUT); digitalWrite(p, MATRIX_IDLE); }
    // Matrix cols (inputs) - Aktivpegel laut MATRIX_POL, passt zur jeweiligen
    // Diodenrichtung (active-high: Kathode Richtung Column, active-low: umgekehrt)
    for (uint8_t p : COL_PINS) { pinMode(p, MATRIX_PULL); }

    // Matrix-Selbsttest beim Boot: zeigt pro Reihe den Rohzustand aller Spalten.
    // Aktiv = gedrückt/kurzgeschlossen, sonst offen (normal ohne Tastendruck)
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], MATRIX_ACTIVE);
        delayMicroseconds(10);
        Serial.print("row ");
        Serial.print(r);
        Serial.print(" cols: ");
        for (uint8_t c = 0; c < 13; c++) {
            // 1 = aktiv, 0 = offen
            Serial.print(MATRIX_PRESSED(digitalRead(COL_PINS[c])) ? "1" : "0");
        }
        Serial.println();
        digitalWrite(ROW_PINS[r], MATRIX_IDLE);
    }

    // Start USB HID (Keyboard + Absolute-Mouse als Composite).
    // Die Absolut-Maus wird als erster HID-Report registriert (ordering 8),
    // das Keyboard folgt (10/11) - macOS verlangt Maus-Reports VOR den
    // Keyboard-Reports im Composite (gleiche Lösung wie beim Encoder-Wing).
    Keyboard.begin();
    absMouseBegin();

#if ENABLE_FADERS && FADER_OUT_MODE == 2
    // Kalibrierdaten: aus LittleFS laden, sonst Defaults. Mit gedrueckter
    // DBO-Taste starten -> Kalibrier-UI (NEXT Page speichert und beendet).
    bool calFromFlash = loadFaderCal();
    printFaderCal(calFromFlash ? " flash" : " default");
    if (dboHeldAtBoot()) {
        Serial.println("[CAL] DBO beim Boot gedrueckt -> Kalibrier-Modus");
        for (uint8_t i = 0; i < 4; i++) { digitalWrite(LED_BUILTIN, HIGH); delay(80); digitalWrite(LED_BUILTIN, LOW); delay(80); }
        faderCalUi(true);      // kehrt zurueck, sobald NEXT Page gedrueckt wurde
        Serial.println("[CAL] Kalibrierung beendet, Regelbetrieb laeuft");
    }
#endif

#if FADER_LINUX_NUMLOCK
    // NumLock-Zustand UEber den LED-Report verfolgen (Linux), damit die
    // Numpad-Wert-Eingabe auch bei ausgeschaltetem NumLock Ziffern liefert.
    Keyboard.onLED(kbdLedCb, nullptr);
#endif

#if FADER_NUDGE_CAL
    // Auslastungs-Kalibrierung: Wie dicht duerfen Wheel-Reports (wheel=1)
    // aufeinander folgen, bis MagicQ/macOS welche verliert?
    // Referenz: 25 ms Abstand -> 40 Rasten = 16 %. Jeweils 40 Rasten mit
    // 20/12/8/5 ms Abstand senden; PB1-Wert nach jedem Block notieren.
    // Der hoechste saubere Abstand wird das Drain-Tempo (FADER_SCROLL_INTERVAL).
    delay(4000);
    Serial.println("NUDGE-CAL S20: 40x wheel=1 @20ms");
    for (int i = 0; i < 40; i++) { absMouseReport(FADER_TARGET_X[1], FADER_TARGET_Y[1], 1, 0); delay(20); }
    Serial.println("NUDGE-CAL S20 done");
    delay(3000);
    Serial.println("NUDGE-CAL S12: 40x wheel=1 @12ms");
    for (int i = 0; i < 40; i++) { absMouseReport(FADER_TARGET_X[1], FADER_TARGET_Y[1], 1, 0); delay(12); }
    Serial.println("NUDGE-CAL S12 done");
    delay(3000);
    Serial.println("NUDGE-CAL S08: 40x wheel=1 @8ms");
    for (int i = 0; i < 40; i++) { absMouseReport(FADER_TARGET_X[1], FADER_TARGET_Y[1], 1, 0); delay(8); }
    Serial.println("NUDGE-CAL S08 done");
    delay(3000);
    Serial.println("NUDGE-CAL S05: 40x wheel=1 @5ms");
    for (int i = 0; i < 40; i++) { absMouseReport(FADER_TARGET_X[1], FADER_TARGET_Y[1], 1, 0); delay(5); }
    Serial.println("NUDGE-CAL S05 done");
#endif

#if FADER_CALIBRATE == 2
    // Interaktive Kalibrierung fest einkompiliert (siehe faderCalUi).
    faderCalUi(false);
#elif FADER_CALIBRATE == 1
    // Kalibrier-Modus: Cursor wandert nacheinander auf jeden der 11 Zielpunkte
    // und blinkt dabei seinen Index (i+1 mal) - so die X-Position der
    // Full-Panel-Faderbahn uebernehmen.
#if FADER_OUT_MODE == 2
    Serial.println("[CALIB] Full-Panel: Cursor auf jeden Fader, Index = Blinkzahl");
    for (uint8_t i = 0; i < NUM_FADERS; i++) {
        int cx = fcal.x[i];
        int cy = ((int)fcal.yTop + (int)fcal.yBot) / 2;   // Fadenmitte
        absMouseMove(cx, cy);
        Serial.printf("[CALIB] DRAG target %d: x=%d y=%d\n", i, cx, cy);
        for (uint8_t b = 0; b <= i; b++) {
            blinkLed();
            delay(150);
        }
        delay(4000);
    }
    Serial.println("[CALIB] jetzt vertikal: PB1 oben (100%) und unten (0%)");
    while (1) {
        absMouseMove(fcal.x[1], fcal.yTop);
        delay(2500);
        absMouseMove(fcal.x[1], fcal.yBot);
        delay(2500);
    }
#else
    Serial.println("[CALIB] beginne Fader-Kalibrierung");
    for (uint8_t i = 0; i < NUM_FADERS; i++) {
        absMouseMove(FADER_TARGET_X[i], FADER_TARGET_Y[i]);
        Serial.printf("[CALIB] target %d: x=%d y=%d\n", i, FADER_TARGET_X[i], FADER_TARGET_Y[i]);
        for (uint8_t b = 0; b <= i; b++) {
            blinkLed();
            delay(150);
        }
        delay(4000);
    }
    Serial.println("[CALIB] fertig");
    while (1) { delay(1000); }
#endif
#endif

#if HID_SELFTEST
    // HID-Selbsttest: 5 s warten (Zeit, in TextEdit zu klicken), dann tippen.
    Serial.println("SELFTEST: beginne HID-Typing in 5s");
    delay(5000);
    Serial.println("SELFTEST: tippe jetzt 123");
    typeText("123");
    Serial.println("SELFTEST: fertig");
#endif
}

// Behandelt eine einzelne Matrixzelle: entprellt, erzeugt press/release
// je nach Zelltyp (KEY, FLASH, DBO). S-/GO-Zellen überlässt es der Akkordphase.
void processKey(uint8_t r, uint8_t c) {
    const KeyCell cell = KEYMAP[r][c];
    if (cell.type == KT_NONE) return;

    bool pressed = MATRIX_PRESSED(digitalRead(COL_PINS[c]));

    // Prellen: Zustand muss 25 ms stabil sein, bevor er als Ereignis gilt.
    if (pressed != keyRaw[r][c]) {
        keyRaw[r][c] = pressed;
        keySince[r][c] = millis();
    }
    bool stable = (millis() - keySince[r][c]) > KEY_DEBOUNCE_MS;
    bool isChord = (cell.type == KT_SEL || cell.type == KT_GO);

    if (pressed && stable && !keyState[r][c]) {
        keyState[r][c] = true;
        digitalWrite(LED_BUILTIN, HIGH);        // LED an solange Taste gehalten
        if (cell.type == KT_DBO) {
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_ALT);
            Keyboard.press('0');
            debugKey("PRESS DBO", cell.code);
        } else if (cell.type == KT_FLASH) {
            // Test-Taste AN toggeln; sofort wieder loslassen, damit
            // macOS kein Auto-Repeat (,,,,) und MagicQ kein Flackern bekommt.
            Keyboard.press(cell.code);
            Keyboard.release(cell.code);
            debugKey("FLASH ON", cell.code);
        } else if (!isChord) {
            Keyboard.press(cell.code);
            debugKey("PRESS", cell.code);
        }
        // S-/GO-Taste: hier NICHT senden - die S+GO-Phase entscheidet
        // nach dem Fenster (STOP-Chord oder Einzeltaste).
    } else if (!pressed && stable && keyState[r][c]) {
        keyState[r][c] = false;
        digitalWrite(LED_BUILTIN, LOW);         // Taste losgelassen -> LED aus
        if (cell.type == KT_DBO) {
            Keyboard.release(KEY_LEFT_CTRL);
            Keyboard.release(KEY_LEFT_ALT);
            Keyboard.release('0');
            debugKey("RELEASE DBO", cell.code);
        } else if (cell.type == KT_FLASH) {
            // Test-Taste AUS toggeln; sofort loslassen (kein Auto-Repeat).
            Keyboard.press(cell.code);
            Keyboard.release(cell.code);
            debugKey("FLASH OFF", cell.code);
        } else if (!isChord) {
            Keyboard.release(cell.code);
            debugKey("RELEASE", cell.code);
        }
        // S-/GO-Taste: Loslassen uebernimmt die S+GO-Phase.
    }
}

void loop() {
    uint32_t now = millis();

#if FADER_LINUX_NUMLOCK
    ensureNumlock();
#endif

    // ---- 1. Key matrix scan (active level laut MATRIX_POL) ----
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], MATRIX_ACTIVE);   // row active
        delayMicroseconds(10);                      // allow lines to settle
        for (uint8_t c = 0; c < 13; c++) {
            processKey(r, c);
        }
        digitalWrite(ROW_PINS[r], MATRIX_IDLE);     // row inactive
    }

    // ---- S + GO = PAUSE (Phase) ----
    // Jede S- und GO-Taste wird erst nach CHORD_MS gesendet. Kommt innerhalb
    // des Fensters der Partner dazu, wird NUR die STOP-Taste getippt (nie die
    // einzelne S-/GO-Taste). Gehaltene Tasten werden per Keep-Alive verlängert.
    for (int pb = 1; pb <= 10; pb++) {
        int i = pb - 1;
        uint8_t c = (uint8_t)(1 + pb);
        bool curS = keyState[0][c];
        bool curG = keyState[1][c];
        unsigned char KC_S = SEL_KEYS[i];                       // '1'..'0'
        unsigned char KC_G = GO_KEYS[i];                        // 'q'..'p'
        unsigned char KC_T = STOP_KEYS[i];

        if (pairState[i] == 3) {                    // Chord aktiv: warten bis Greifen weg
            if (!(curS && curG)) {
                if (curS) { pairState[i] = 1; soloSince[i] = now; }
                else if (curG) { pairState[i] = 2; soloSince[i] = now; }
                else pairState[i] = 0;
            }
            continue;
        }

        if (curS && curG) {                          // Chord erkannt
            if (keySent[i][0]) { Keyboard.release(KC_S); keySent[i][0] = false; }
            if (keySent[i][1]) { Keyboard.release(KC_G); keySent[i][1] = false; }
            Keyboard.press(KC_T);
            Keyboard.release(KC_T);
            debugKey("PAUSE", KC_T);
            pairState[i] = 3;
            continue;
        }

        if (pairState[i] == 0) {                     // idle -> Solo-Druck abwarten
            if (curS) { pairState[i] = 1; soloSince[i] = now; }
            else if (curG) { pairState[i] = 2; soloSince[i] = now; }
            continue;
        }

        // Solo pending: komme nur, wenn solange nur EINE der beiden gehalten ist
        bool solo = (pairState[i] == 1) ? (curS && !curG) : (curG && !curS);
        if (!solo) {
            if (pairState[i] == 1 && keySent[i][0]) { Keyboard.release(KC_S); keySent[i][0] = false; }
            if (pairState[i] == 2 && keySent[i][1]) { Keyboard.release(KC_G); keySent[i][1] = false; }
            pairState[i] = 0;
            continue;
        }
        if (now - soloSince[i] >= chordMs) {         // Fenster abgelaufen -> Einzeltaste
            if (pairState[i] == 1 && !keySent[i][0]) { Keyboard.press(KC_S); keySent[i][0] = true; debugKey("SEL", KC_S); }
            if (pairState[i] == 2 && !keySent[i][1]) { Keyboard.press(KC_G); keySent[i][1] = true; debugKey("GO", KC_G); }
        }
    }

    // Keep-alive: solange eine Taste gehalten wird, sendet das Pico periodisch
    // das DOWN-Report erneut (wie ein Auto-Repeat einer echten Tastatur).
    // macOS/MagicQ schalten sonst einen einzelnen DOWN-Event als "Tap" ab.
    static uint32_t lastKeep = 0;
    if (now - lastKeep > KEEPALIVE_MS) {
        lastKeep = now;
        for (uint8_t r = 0; r < 3; r++) {
            for (uint8_t c = 0; c < 13; c++) {
                if (!keyState[r][c]) continue;
                const KeyCell cell = KEYMAP[r][c];
                if (cell.type == KT_KEY) {
                    Keyboard.press(cell.code);
                } else if (cell.type == KT_DBO) {
                    Keyboard.press(KEY_LEFT_CTRL);
                    Keyboard.press(KEY_LEFT_ALT);
                    Keyboard.press('0');
                }
                // FLASH (Kurzreport), SEL/GO (Akkordphase) werden hier nicht angefasst
            }
        }
        // Gehaltene, bereits gesendete S-/GO-Tasten verlaengern
        for (int pb = 1; pb <= 10; pb++) {
            int i = pb - 1;
            if (pairState[i] == 1 && keySent[i][0]) Keyboard.press(SEL_KEYS[i]);
            if (pairState[i] == 2 && keySent[i][1]) Keyboard.press(GO_KEYS[i]);
        }
    }

    // Diagnose-Kombos:
    //   S10 (row0 col11) + GO10 (row1 col11) -> gedrückte Zellen als Text
    //   S1  (row0 col2)  + GO1  (row1 col2)  -> Fader-Level als Text
    static bool diagMatrixReported = false;
    if (keyState[0][11] && keyState[1][11]) {
        if (!diagMatrixReported) {
            diagMatrixReported = true;
            dumpMatrixText();
        }
    } else if (diagMatrixReported) {
        diagMatrixReported = false;
    }

#if ENABLE_FADERS
    static bool diagFaderReported = false;
    if (keyState[0][2] && keyState[1][2]) {
        if (!diagFaderReported) {
            diagFaderReported = true;
            dumpFaderText();
        }
    } else if (diagFaderReported) {
        diagFaderReported = false;
    }
#endif

    // ---- 2. Fader scan (Mausrad-Modus: analoge Pegel -> On-Screen-Fader) ----
#if ENABLE_FADERS
#if FADER_LOOP_DIAG
    static uint32_t diagLast = 0;
    static int diagCount = 0;
    if (diagCount < (int)(FADER_LOOP_DIAG_MS / 200)) {
        if (diagLast == 0) diagLast = millis();
        if ((millis() - diagLast) >= 200) {
            diagLast = millis();
            Serial.printf("DIAG %4d ms:", (int)millis());
            for (uint8_t f = 0; f < NUM_FADERS; f++) {
                int pct = readFaderPct(f);
                Serial.printf(" f%02d=%02d", (int)f, pct);
            }
            Serial.println();
            diagCount++;
        }
    }
#endif
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        updateFaderState(f);
    }
#if FADER_OUT_MODE == 2
    // DRAG-Modus: Full-Panel-Fader per Maus greifen und auf Zielhoehe ziehen.
    updateDragOutput();
#elif FADER_OUT_MODE == 1
    // SET-Modus: absolut getippte Werte (Keyboard-Befehle) nachfuehren.
    updateSetOutput();
#else
    // Wheel-Modus: akkumulierte Rasten getaktet per Mausrad senden.
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        drainFader(f);
    }
#endif
#endif

    delay(5);
}