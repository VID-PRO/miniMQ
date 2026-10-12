#include <Arduino.h>
#include <Rotary.h>
#include <USB.h>
#include <tusb.h>
#include <tusb-hid.h>
#include "class/hid/hid_device.h"
#include <hardware/watchdog.h>
#include <LittleFS.h>

// ==========================================
// 1. PIN-DEFINITIONEN (Raspberry Pi Pico / RP2040 Zero)
// ==========================================
// Das Encoder-Modul ist in ZWEI Bausteine aufgeteilt: linker und rechter
// Baustein, jeder mit 4 Drehencodern. Beide verwenden das IDENTISCHE Pinout
// (gleiche Firmware, gleiches Layout) - nur die Default-Zielpunkte unterscheiden
// sich je nach Seite (ENC_SIDE). Die Kalibrierung ist pro RP2040 getrennt und
// liegt in dessen eigenem LittleFS (/ecal.bin).

// --- Drehencoder Pins (CLK=A, DT=B) - 4 Stück direkt auf GPIO ---
// ENC-A-i und ENC-B-i sind die beiden Quadratur-Ausgaenge des Encoders i.
// ENC_A wird als CLK behandelt (Pin 1 der Rotary-Bibliothek).
static const uint8_t ENC_A[4] = {29, 28, 27, 26};   // ENC-A-1..4
static const uint8_t ENC_B[4] = { 3,  4,  5,  6};   // ENC-B-1..4
// --- Encoder-Switches (gegen GND, Pullup, aktiv-LOW) ---
static const uint8_t ENC_SW[4] = {15, 7, 14, 8};    // ENC-SW-1..4

Rotary enc1(29, 3);
Rotary enc2(28, 4);
Rotary enc3(27, 5);
Rotary enc4(26, 6);

Rotary* encoders[4] = { &enc1, &enc2, &enc3, &enc4 };

// --- Onboard-LED (GP25) als Diagnose-Anzeige ---
const int ledPin = 25;

// --- Wertebereich der absoluten Maus ---
#define ENC_COORD_MAX 32767

// ==========================================
// 2. MODUL-SEITE (LEFT/RIGHT)
// ==========================================
// Welche Haelfte dieser RP2040 bedient - steuert NUR die kompilierten
// Default-Zielpunkte (X/Y fuer On-Screen-Encoder 1..4 rechts bzw. 5..8 links).
// Nach der Kalibrierung (siehe unten) spielt die Seite keine Rolle mehr: die
// 4 Zielpositionen kommen dann aus /ecal.bin dieses Moduls.
//   0 = linker Baustein  (On-Screen-Encoder 1..4)
//   1 = rechter Baustein (On-Screen-Encoder 5..8)
#ifndef ENC_SIDE
#define ENC_SIDE 0
#endif
#if ENC_SIDE == 1
#define ENC_SIDE_NAME "RIGHT"
#else
#define ENC_SIDE_NAME "LEFT"
#endif

// Drehrichtung: 0 = wie beim Vorgaenger verifiziert (CW -> Wert -)
//               1 = invertieren, falls die neue Verdrahtung anders zaehlt
#define ENC_INVERT_DIR 0

// ==========================================
// ABSOLUTE MOUSE (CURSOR-POSITIONIERUNG)
// ==========================================
// Die 4 Drehencoder setzen den Mauszeiger auf den korrespondierenden
// On-Screen-Encoder in MagicQ und scrollen/klicken DORT. Dazu wird ein zweites
// HID-Device registriert: eine absolute Maus (digitizer-style), die den ganzen
// Bildschirm in X=0..32767 / Y=0..32767 abbildet.
//
// Eigene Routine statt MouseAbsolute: dessen click()/press() rufen move(0,0,0)
// auf, was bei absoluter Maus den Cursor in die Ecke (0,0) springen laesst.

// Relative Maus (nur fuer die HID-Reihenfolge), wird als ERSTER HID-Report
// registriert (ordering 8): macOS akzeptiert Pointing-Devices in einem
// Composite nur, wenn ein Maus-Report VOR evtl. Keyboard-Reports steht.
static const uint8_t desc_rel_mouse[] = { TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1)) };
static uint8_t relMouseLocalID = 0;
static bool relMouseRunning = false;

static const uint8_t desc_abs_mouse[] = { TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(1)) };
static uint8_t absMouseLocalID = 0;
static bool absMouseRunning = false;

// ====== DEBUG ======
#define DEBUG_VERBOSE 1
#define DEBUG_VERSION "ENC-DUAL-v8-SPLIT-4ENC"
#define DEBUG_MSG(...) do { if (DEBUG_VERBOSE) { Serial.printf(__VA_ARGS__); } } while (0)

void absMouseBegin() {
  if (absMouseRunning) {
    return;
  }
  USB.disconnect();
  relMouseLocalID = USB.registerHIDDevice(desc_rel_mouse, sizeof(desc_rel_mouse), 8, 0x0002);
  absMouseLocalID = USB.registerHIDDevice(desc_abs_mouse, sizeof(desc_abs_mouse), 9, 0x0002);
  USB.connect();
  absMouseRunning = true;
  relMouseRunning = true;
  // USB.connect() bewirkt Re-Enumeration -> der Rechner verliert kurz die
  // USB-Verbindung. Diese Prints gehen VERLOREN, Status kommt in loop().
}

// Sendet Position+x-Achsen-Wheel+Buttons in EINEM HID-Report (absolut).
// x,y = 0..32767 (Bildschirm links/oben nach rechts/unten).
void absMouseReport(int16_t x, int16_t y, int8_t wheel, uint8_t buttons) {
  if (!absMouseRunning) {
    DEBUG_MSG("[ABSMOUSE] skip: not running\n");
    return;
  }
  DEBUG_MSG("[ABSMOUSE] attempt rid=%u buttons=%u x=%d y=%d wheel=%d mounted?%d\n",
            USB.findHIDReportID(absMouseLocalID), buttons, x, y, wheel, (int)tud_mounted());
  CoreMutex m(&USB.mutex);
  tud_task();
  if (tud_hid_ready()) {
    tud_hid_abs_mouse_report(USB.findHIDReportID(absMouseLocalID), buttons, x, y, wheel, 0);
  }
  tud_task();
}

// Cursor auf Punkt setzen, ohne zu klicken.
void absMouseMove(int16_t x, int16_t y) {
  absMouseReport(x, y, 0, 0);
}

// Linksklick GENAU an der uebergebenen Position ueber die ABSOLUTE Maus.
void absMouseClickAt(int16_t x, int16_t y) {
  DEBUG_MSG("[CLICKHANDLE] pos(%d,%d)\n", x, y);
  absMouseReport(x, y, 0, 0x00); // erst Cursor positionieren
  delay(40);
  absMouseReport(x, y, 0, 0x01); // MOUSE_LEFT druecken
  delay(20);
  absMouseReport(x, y, 0, 0x00); // loslassen
  delay(10);
}

// ==========================================
// ON-SCREEN-ENCODER-ZIELPOSITIONEN (KALIBRIERUNG)
// ==========================================
// MagicQ im Vollbild/Maximiert: die On-Screen-Encoder des Konsolen-Layouts
// liegen an festen Bildschirmpositionen. Jeder der 4 lokalen Encoder hat eine
// EIGENE Zielposition. Zur Laufzeit liegen sie in encCal.x[4]/y[4] (>/ecal.bin
// in LittleFS). Defaults je Seite (ENC_SIDE): links = Encoder 1..4, rechts =
// Encoder 5..8 einer 8er-Anordnung. Die Kalibrierung ist PRO RP2040 getrennt.
//
// KALIBRIEREN (ohne Neuflashen): Modul mit gedruecktem ENCODER-SW 1 starten ->
// Kalibrier-UI:
//   Klick auf Encoder i          -> Encoder i auswaehlen (Cursor springt hin)
//   drehen an Encoder 1           -> X grob   (+/-ENC_CAL_X_COARSE)
//   drehen an Encoder 2           -> X fein   (+/-ENC_CAL_X_FINE)
//   drehen an Encoder 3           -> Y grob   (+/-ENC_CAL_Y_COARSE)
//   drehen an Encoder 4           -> Y fein   (+/-ENC_CAL_Y_FINE)
//   erneuter Klick auf den gewaehlten Encoder -> diesen Encoder speichern
//   sind alle 4 gespeichert -> Neustart des RP2040.
struct EncCal {
    uint16_t x[4];
    uint16_t y[4];
};
static EncCal encCal;
static EncCal ENC_CAL_DEFAULT;   // wird in setup() nach ENC_SIDE befuellt
#define ENC_CAL_FILE "/ecal.bin"

#define ENC_CAL_X_COARSE 500
#define ENC_CAL_X_FINE   50
#define ENC_CAL_Y_COARSE 500
#define ENC_CAL_Y_FINE   50

bool loadEncCal() {
  if (!LittleFS.begin()) {
    Serial.println("[CAL] WARN LittleFS nicht verfuegbar - Werte werden NICHT gespeichert");
    return false;
  }
  File f = LittleFS.open(ENC_CAL_FILE, "r");
  if (!f || f.size() != (int)sizeof(EncCal)) {
    if (f) f.close();
    LittleFS.end();
    return false;
  }
  EncCal tmp;
  size_t got = f.read((uint8_t *)&tmp, sizeof(tmp));
  f.close();
  LittleFS.end();
  if (got != sizeof(tmp)) return false;
  encCal = tmp;
  return true;
}

bool saveEncCal() {
  if (!LittleFS.begin()) return false;
  File f = LittleFS.open(ENC_CAL_FILE, "w");
  if (!f) { LittleFS.end(); return false; }
  size_t put = f.write((const uint8_t *)&encCal, sizeof(encCal));
  f.close();
  LittleFS.end();
  return put == sizeof(encCal);
}

// ==========================================
// 3. ZEIT-/VERHALTENS-KONFIGURATION
// ==========================================
const unsigned long DEBOUNCE_MS = 50;    // Entprellzeit der Encoder-Switches

// Encoder-Steuerung per absoluter Maus (Buttons-only Busking).
// Jedes Rad positioniert den Cursor zuerst per ABSOLUTER Maus auf den passenden
// On-Screen-Encoder und sendet dort dann Mausrad-Scrolling bzw. Linksklick.
// Kein Keyboard-Mode-Toggle: funktioniert in jedem MagicQ Keyboard-Mode.
constexpr signed char MOUSE_WHEEL_STEP = 1;   // Rasten pro MagicQ-Encoder-Schritt

// Kurzer LED-Blink bei jeder Aktion (Diagnose), nicht-blockierend.
const unsigned long LED_FLASH_MS = 8;
unsigned long lastLedOnMs = 0;
bool ledFlashPending = false;

void ledFlash() {
  digitalWrite(ledPin, HIGH);
  lastLedOnMs = millis();
  ledFlashPending = true;
}

void serviceLed() {
  if (ledFlashPending && (millis() - lastLedOnMs >= LED_FLASH_MS)) {
    ledFlashPending = false;
    digitalWrite(ledPin, LOW);
  }
}

// ==========================================
// 4. STATUS-VARIABLEN (SPEICHER)
// ==========================================
bool lastEncBtnState[] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastEncDebounceTime[] = {0, 0, 0, 0};
bool encBtnStable[] = {HIGH, HIGH, HIGH, HIGH};

// ==========================================
// SETUP
// ==========================================

// Neustart des RP2040 (Watchdog-Reset).
void encReboot() {
  Serial.println("[CAL] alle 4 Encoder gespeichert -> Neustart");
  Serial.flush();
  delay(300);
  watchdog_enable(500, true);
  for (;;) { delay(10); }
}

void encCalBlink(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(ledPin, HIGH);
    delay(120);
    digitalWrite(ledPin, LOW);
    delay(120);
  }
  delay(400);
}

// KALIBRIER-UI (Start mit gedruecktem ENCODER-SW 1):
//   Klick auf Encoder i           -> Encoder i auswaehlen (Cursor springt hin)
//   drehen Encoder 1 / 2          -> X grob / X fein
//   drehen Encoder 3 / 4          -> Y grob / Y fein
//   erneuter Klick auf Encoder i  -> Position von Encoder i SPEICHERN
//   alle 4 gespeichert            -> Neustart des RP2040
void encCalUi() {
  bool saved[4] = { false };
  uint8_t sel = 0xFF;
  uint8_t done = 0;

  Serial.println("[CAL] Positionskalibrierung der 4 On-Screen-Encoder");
  Serial.println("[CAL] Klick auf Encoder = auswaehlen | Enc1 X grob, Enc2 X fein,");
  Serial.println("[CAL] Enc3 Y grob, Enc4 Y fein | Klick auf gewaehlten = speichern");

  while (true) {
    // 1) Klicks (direkt an den GPIOs, aktiv-LOW). Entprellt ueber die
    //    blockierende UI-Schleife (Delay ~10 ms je Durchlauf).
    static bool last[4] = {HIGH, HIGH, HIGH, HIGH};
    for (int i = 0; i < 4; i++) {
      bool pressed = (digitalRead(ENC_SW[i]) == LOW);
      bool risingEdge = pressed && !last[i];
      last[i] = pressed;
      if (!risingEdge) continue;

      if (sel == 0xFF || sel != i) {
        sel = (uint8_t)i;
        Serial.printf("[CAL] Encoder %u gewaehlt: x=%u y=%u%s\n", sel + 1,
                      encCal.x[sel], encCal.y[sel],
                      saved[sel] ? " (bereits gespeichert)" : "");
        encCalBlink(sel + 1);
      } else if (!saved[i]) {
        saved[i] = true;
        done++;
        Serial.printf("[CAL] Encoder %u gespeichert: x=%u y=%u (%u/4)\n", sel + 1,
                      encCal.x[sel], encCal.y[sel], done);
        encCalBlink(done);
        if (done >= 4) {
          if (saveEncCal()) {
            Serial.println("[CAL] /ecal.bin geschrieben");
            encCalBlink(4);
            encReboot();
          } else {
            Serial.println("[CAL] FEHLER beim Schreiben von /ecal.bin - "
                           "LittleFS vorhanden? (platformio.ini board_build.filesystem_size=64)");
          }
        }
      } else {
        Serial.printf("[CAL] Encoder %u war schon gespeichert - "
                      "erst mit Enc1-4 weiterverschieben\n", sel + 1);
      }
    }

    // 2) Drehregler X1/X2 = X grob/fein, X3/X4 = Y grob/fein des gewaehlten Encoders.
    if (sel != 0xFF) {
      for (int k = 0; k < 4; k++) {
        unsigned char dir = encoders[k]->process();
        if (dir == DIR_NONE) continue;
        bool cx = (k < 2);
        bool coarse = ((k == 0) || (k == 2));
        int32_t step = coarse ? ENC_CAL_X_COARSE : ENC_CAL_X_FINE;
        if (k >= 2) step = coarse ? ENC_CAL_Y_COARSE : ENC_CAL_Y_FINE;
        int32_t delta = (dir == DIR_CW) ? step : -step;
        uint16_t *target = cx ? &encCal.x[sel] : &encCal.y[sel];
        int32_t v = (int32_t)(*target) + delta;
        if (v < 0) v = 0;
        if (v > ENC_COORD_MAX) v = ENC_COORD_MAX;
        *target = (uint16_t)v;
        absMouseMove((int16_t)encCal.x[sel], (int16_t)encCal.y[sel]);
        Serial.printf("[CAL] Encoder %u %s %s %+ld -> %u\n", sel + 1,
                      cx ? "X" : "Y", coarse ? "grob" : "fein", (long)delta,
                      (unsigned)*target);
      }
    }

    delay(10);
  }
}

// ENCODER-SW 1 beim Start gedrueckt -> Kalibrier-UI (zweimal gelesen, damit
// ein Kontaktpreller nicht schon beim Start ausloest).
bool encCalBootKey() {
  if (digitalRead(ENC_SW[0]) != LOW) return false;
  delay(60);
  return digitalRead(ENC_SW[0]) == LOW;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(ledPin, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }

  // Encoder-Pins (A/B): interner Pullup.
  int encPins[] = {29, 28, 27, 26, 3, 4, 5, 6};
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(encPins[i], INPUT_PULLUP);
  }

  // Encoder-Switches (gegen GND): interner Pullup, aktiv-LOW.
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(ENC_SW[i], INPUT_PULLUP);
  }

  // Default-Zielpunkte je Seite.
  if (ENC_SIDE == 1) {
    // rechter Baustein: On-Screen-Encoder 5..8
    ENC_CAL_DEFAULT = EncCal{ { 26000, 26000, 26000, 26000 },
                              { 7000, 13000, 20000, 27000 } };
  } else {
    // linker Baustein: On-Screen-Encoder 1..4
    ENC_CAL_DEFAULT = EncCal{ { 9000, 9000, 9000, 9000 },
                              { 4000, 13000, 20000, 27000 } };
  }
  encCal = ENC_CAL_DEFAULT;

  // USB-HID starten (absolute + relative Maus als Composite). Es gibt KEINE
  // Tastatur mehr: dieses Modul hat nur die 4 Drehencoder mit Klick.
  absMouseBegin();

  DEBUG_MSG("========== %s (Seite %s) ==========\n", DEBUG_VERSION, ENC_SIDE_NAME);

  // Kalibrierdaten aus LittleFS (sonst die Defaults der Seite).
  if (loadEncCal()) {
    Serial.println("[CAL] /ecal.bin geladen:");
  } else {
    Serial.println("[CAL] Defaults (kein /ecal.bin):");
  }
  for (int i = 0; i < 4; i++) {
    Serial.printf("[CAL]   Enc %u: x=%u y=%u\n", i + 1, encCal.x[i], encCal.y[i]);
  }

  // Mit ENCODER-SW 1 gedrueckt starten -> Positionskalibrierung.
  if (encCalBootKey()) {
    Serial.println("[CAL] ENCODER-SW 1 beim Start gedrueckt -> Kalibrier-UI");
    encCalUi();
    Serial.println("[CAL] Kalibrierung beendet");
  }
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  unsigned long currentMillis = millis();

  static bool mountedWas = false;
  static unsigned long lastStatusPrint = 0;
  bool mountedNow = (tud_mounted() != 0);
  if (mountedNow && (!mountedWas || (currentMillis - lastStatusPrint > 2000))) {
    lastStatusPrint = currentMillis;
    Serial.printf("[STATUS] mounted=1 relLocalID=%u absLocalID=%u relRID=%u absRID=%u\n",
                  relMouseLocalID, absMouseLocalID,
                  USB.findHIDReportID(relMouseLocalID), USB.findHIDReportID(absMouseLocalID));
  }
  mountedWas = mountedNow;

  for (int i = 0; i < 4; i++) {
    // Drehung: process() liefert DIR_CW, DIR_CCW oder DIR_NONE
    unsigned char dir = encoders[i]->process();
    if (dir == DIR_CW || dir == DIR_CCW) {
      signed char step = (dir == DIR_CW) ? -MOUSE_WHEEL_STEP : MOUSE_WHEEL_STEP;
#if ENC_INVERT_DIR
      step = -step;
#endif
      // Cursor zuerst auf den On-Screen-Encoder positionieren, dann scrollen.
      absMouseReport((int16_t)encCal.x[i], (int16_t)encCal.y[i], step, 0);
      Serial.printf("ENC %d -> pos(%u,%u) wheel %+d\n", i + 1,
                    encCal.x[i], encCal.y[i], step);
      ledFlash();
    }

    // Encoder-Klick (direkt am GPIO, aktiv-LOW, entprellt).
    bool readingEnc = (digitalRead(ENC_SW[i]) == LOW) ? LOW : HIGH;
    if (readingEnc != lastEncBtnState[i]) {
      lastEncDebounceTime[i] = currentMillis;
    }
    if ((currentMillis - lastEncDebounceTime[i]) > DEBOUNCE_MS) {
      if (readingEnc != encBtnStable[i]) {
        encBtnStable[i] = readingEnc;
        if (readingEnc == LOW) {
          ledFlash();
          absMouseClickAt((int16_t)encCal.x[i], (int16_t)encCal.y[i]);
          Serial.printf("ENC %d -> click at (%d,%d)\n", i + 1,
                        encCal.x[i], encCal.y[i]);
        }
      }
    }
    lastEncBtnState[i] = readingEnc;
  }

  serviceLed();
  delay(2);
}