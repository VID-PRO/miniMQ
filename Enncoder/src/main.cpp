#include <Arduino.h>
#include <Keyboard.h>
#include <Rotary.h>
#include <Adafruit_MCP23X17.h>
#include <USB.h>
#include <tusb.h>
#include <tusb-hid.h>
#include "class/hid/hid_device.h"
#include <LittleFS.h>

// ==========================================
// 1. PIN-DEFINITIONEN (Raspberry Pi Pico)
// ==========================================

// --- Drehencoder Pins (CLK, DT) - 8 Stück direkt auf GPIO 0-15 ---
// Rotary(pin1, pin2) - Pin-Reihenfolge entspricht CLK/DT pro Encoder
Rotary enc1(0, 1);
Rotary enc2(2, 3);
Rotary enc3(4, 5);
Rotary enc4(6, 7);
Rotary enc5(8, 9);
Rotary enc6(10, 11);
Rotary enc7(12, 13);
Rotary enc8(14, 15);

Rotary* encoders[8] = { &enc1, &enc2, &enc3, &enc4, &enc5, &enc6, &enc7, &enc8 };

// --- I²C für MCP23017 ---
// Pico: GP16 = SDA, GP17 = SCL
#define I2C_SDA 16
#define I2C_SCL 17

// --- Freie Pico-GPIOs für direkte Tasten (gegen GND, Pullup aktiv) ---
// F5..F8 = GP18..GP21, Shift = GP22, Group = GP26, FX = GP27
const int fKeyPins[] = {18, 19, 20, 21};
const int shiftKeyPin = 22;   // Shift-Taste (Modifier, gehalten)
const int groupKeyPin = 26;
const int fxKeyPin = 27;

// --- Tasten am MCP23017 (I/O-Expander, 16 Pins, Adresse 0x20) ---
// GPA0..GPA7 = Encoder-SW 1..8
const uint8_t encBtnMCP[] = {0, 1, 2, 3, 4, 5, 6, 7};   // GPA0-GPA7

// --- Onboard-LED (GP25) als Diagnose-Anzeige ---
const int ledPin = 25;

// PIN-FINDER-Modus: scannt GP20..GP28 und zeigt via LED-Blitzcode,
// welcher GPIO bei Tastendruck auf LOW geht.
//   1x=GP20 2x=GP21 3x=GP22 4x=GP23 5x=GP24 6x=GP26 7x=GP27 8x=GP28
#define PIN_FINDER 0

// ==========================================
// ABSOLUTE MOUSE (CURSOR-POSITIONIERUNG)
// ==========================================
// Die 8 Drehencoder sollen den Mauszeiger zuerst auf den korrespondierenden
// On-Screen-Encoder in MagicQ setzen und DORT scrollen/klicken. Dazu wird ein
// zweites HID-Device registriert: eine absolute Maus (digitizer-style), die
// den ganzen Bildschirm in X=0..32767 / Y=0..32767 abbildet.
//
// Eigene Routine statt MouseAbsolute: dessen click()/press() rufen move(0,0,0)
// auf, was bei absoluter Maus den Cursor in die Ecke (0,0) springen lässt.

static const uint8_t desc_abs_mouse[] = { TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(1)) };
static uint8_t absMouseLocalID = 0;
static bool absMouseRunning = false;

// Eigene relative Maus (nur für Klicks), registriert als ALLERERSTER
// HID-Report. Die HID_Mouse-Bibliothek nutzt intern ordering 20 (hinter dem
// Keyboard 10/11) - macOS ignoriert Pointer-Buttons dann vollständig.
static const uint8_t desc_rel_mouse[] = { TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1)) };
static uint8_t relMouseLocalID = 0;
static bool relMouseRunning = false;

// ====== DEBUG ======
// 1 = ausführliche Serial-Ausgabe, 0 = normal.
#define DEBUG_VERBOSE 1
#define DEBUG_VERSION "ENC-DEBUG-v7-CLICKFIX"
#define DEBUG_MSG(...) do { if (DEBUG_VERBOSE) { Serial.printf(__VA_ARGS__); } } while (0)

void relMouseReport(int16_t x, int16_t y, int8_t wheel, uint8_t buttons) {
  if (!relMouseRunning) {
    DEBUG_MSG("[RELMOUSE] skip: not running\n");
    return;
  }
  // Debug VOR der Mutex-Sperre: Serial läuft über USB-CDC und darf nicht
  // unter dem USB-Mutex blockieren, sonst geht der Druck verloren.
  DEBUG_MSG("[RELMOUSE] attempt rid=%u buttons=%u x=%d y=%d wheel=%d mounted?%d\n",
            USB.findHIDReportID(relMouseLocalID), buttons, x, y, wheel, (int)tud_mounted());
  CoreMutex m(&USB.mutex);
  tud_task();
  if (tud_hid_ready()) {
    tud_hid_mouse_report(USB.findHIDReportID(relMouseLocalID), buttons, x, y, wheel, 0);
  }
  tud_task();
}

void relMouseClick(uint8_t button) {
  DEBUG_MSG("[CLICK] down\n");
  relMouseReport(0, 0, 0, button);
  delay(15);
  DEBUG_MSG("[CLICK] up\n");
  relMouseReport(0, 0, 0, 0);
  delay(15);
}

void absMouseBegin() {
  if (absMouseRunning) {
    return;
  }
  // WICHTIG (macOS): Die Maus muss als ERSTER HID-Report registriert werden.
  // macOS akzeptiert Pointing-Devices in einem Composite nur, wenn der Maus-Report
  // vor den Keyboard-Reports steht (sonst wird der Cursor nie bewegt).
  USB.disconnect();
  relMouseLocalID = USB.registerHIDDevice(desc_rel_mouse, sizeof(desc_rel_mouse), 8, 0x0002);
  absMouseLocalID = USB.registerHIDDevice(desc_abs_mouse, sizeof(desc_abs_mouse), 9, 0x0002);
  USB.connect();
  absMouseRunning = true;
  relMouseRunning = true;
  // USB.connect() bewirkt Re-Enumeration -> der Mac verliert kurz die
  // USB-Verbindung. Diese Prints gehen VERLOREN, deshalb erfolgt jetzt KEINE
  // Ausgabe hier; der Status wird in loop() beim / nach dem Mount ausgegeben.
}

// Sendet Position+x-Achsen-Wheel+Buttons in EINEM HID-Report (absolut).
// x,y = 0..32767 (Bildschirm links/oben nach rechts/unten).
void absMouseReport(int16_t x, int16_t y, int8_t wheel, uint8_t buttons) {
  if (!absMouseRunning) {
    DEBUG_MSG("[ABSMOUSE] skip: not running\n");
    return;
  }
  // Debug VOR der Mutex-Sperre ausgeben, s. relMouseReport().
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

// Linksklick GENAU an der übergebenen Position über die ABSOLUTE Maus.
// macOS wertet einen Button im selben Report noch an der ALTEN Cursorposition
// aus, bevor die neue absolute Position wirkt. Deshalb zuerst eine reine
// Positionsmeldung (buttons=0), kurz warten, dann Button down/up mit
// denselben Koordinaten.
void absMouseClickAt(int16_t x, int16_t y) {
  DEBUG_MSG("[CLICKHANDLE] pos(%d,%d)\n", x, y);
  absMouseReport(x, y, 0, 0x00); // erst Cursor positionieren
  delay(40);
  absMouseReport(x, y, 0, 0x01); // MOUSE_LEFT drücken
  delay(20);
  absMouseReport(x, y, 0, 0x00); // loslassen
  delay(10);
}

// ==========================================
// ON-SCREEN-ENCODER-ZIELPOSITIONEN (KALIBRIERUNG)
// ==========================================
// MagicQ im Vollbild/Maximiert: Die 8 On-Screen-Encoder des Konsolen-Layouts
// liegen an festen Bildschirmpositionen. Hier die Zielkoordinaten (absolut in
// 0..32767, X=links-nach-rechts, Y=oben-nach-unten).
//
// Die 8 On-Screen-Encoder liegen im MagicQ-Konsolen-Layout in einem Raster:
// 2 Spalten x 4 Zeilen. Deshalb reichen VIER Stellschrauben, aus denen alle 8
// Positionen berechnet werden:
//   xA = X Spalte A (Encoder 1..4), xB = X Spalte B (Encoder 5..8)
//   yTop = Y Encoder 1, yBot = Y Encoder 4  (dazwischen linear interpoliert)
//
// KALIBRIEREN (ohne Neuflashen): Pico mit gedruecktem ENCODER 1 starten ->
// Kalibrier-UI. Encoder 1..4 = -500 / -50 / +50 / +500 verschieben,
// Encoder 5..8 = Stellschraube waehlen (LED blinkt 1..4), GROUP = speichern
// und zurueck. Die Werte liegen dauerhaft in LittleFS (/ecal.bin).
struct EncCal {
    uint16_t xA, xB, yTop, yBot;
};
static const EncCal ENC_CAL_DEFAULT = { 9000, 26000, 4000, 27000 };
static EncCal encCal = ENC_CAL_DEFAULT;
static int16_t ENC_TARGET_X[8];
static int16_t ENC_TARGET_Y[8];
#define ENC_CAL_FILE "/ecal.bin"

// Stellschrauben -> 8 Zielpositionen (Raster 2 Spalten x 4 Zeilen).
void applyEncCal() {
  for (int i = 0; i < 8; i++) {
    ENC_TARGET_X[i] = (int16_t)((i < 4) ? encCal.xA : encCal.xB);
    ENC_TARGET_Y[i] = (int16_t)(encCal.yTop + ((int32_t)encCal.yBot - encCal.yTop) * (i % 4) / 3);
  }
}

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

#define ENC_CALIBRATE 0

// ==========================================
// 2. ZEIT-/VERHALTENS-KONFIGURATION
// ==========================================
const unsigned long DEBOUNCE_MS = 50;    // Entprellzeit aller Tasten
const unsigned long SHIFT_KEEPALIVE_MS = 60; // Nachsendung Shift DOWN, solange gehalten

// ============================================================
// Encoder-Steuerung per absoluter Maus (Buttons-only Busking)
// ============================================================
// MagicQ am PC/Mac zeigt im Konsolen-Layout (Vollbild/maximiert) 8 virtuelle
// Drehencoder um den Touch-Screen-Bereich. Jede Aktion an einem physischen Rad
// positioniert den Mauszeiger zuerst per ABSOLUTER Maus genau auf den passenden
// On-Screen-Encoder (siehe ENC_TARGET_X/Y oben) und sendet dort dann das
// Mausrad-Scrolling bzw. den Linksklick.
//
// WICHTIG: Es wird KEIN Keyboard-Mode-Toggle (CAPS LOCK etc.) mehr verwendet.
// Das Mausrad-Scrolling über den On-Screen-Encodern funktioniert in jedem
// MagicQ Keyboard-Mode, auch in "Playback shortcuts" (den der Fader-Wing
// braucht).
//
// MagicQ-Syntax am PC: nicht "1+" / "1-" tippen. Encoder-Inkrement gibt es am
// PC nur über die On-Screen-Encoder (Maus) oder MIDI/OSC (im Demo-Modus gesperrt).
constexpr signed char MOUSE_WHEEL_STEP = 1;   // Rasten pro MagicQ-Encoder-Schritt

// Kurzer LED-Blink bei jedem gesendeten Tastendruck (Diagnose).
// Nicht-blockierend: setzt die LED an und lässt serviceLed() den Blink
// zeitgesteuert abbauen, damit der Loop keine 8 ms stillsteht.
const unsigned long LED_FLASH_MS = 8;
unsigned long lastLedOnMs = 0;
bool ledFlashPending = false;

void ledFlash() {
  digitalWrite(ledPin, HIGH);
  lastLedOnMs = millis();
  ledFlashPending = true;
}

// In loop() aufrufen: LED nach LED_FLASH_MS wieder abdunkeln.
void serviceLed() {
  if (ledFlashPending && (millis() - lastLedOnMs >= LED_FLASH_MS)) {
    ledFlashPending = false;
    digitalWrite(ledPin, LOW);
  }
}

// Sendet eine Tastenkombination mit gedrückter Strg-Taste
void sendCtrlKey(char c) {
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.write(c);
  Keyboard.release(KEY_LEFT_CTRL);
}

Adafruit_MCP23X17 mcp;
bool mcpOK = false;   // true, wenn der MCP23017 erkannt wurde
bool shiftStable = HIGH;

// ==========================================
// 3. MAPPINGS & CONFIGURATION
// ==========================================

// Attribute in MagicQ via Strg-Kombination: INT/POS/COL/BEAM
const char fKeyMapping[] = {'I', 'P', 'K', 'J'};

// Ctrl+Kombinationen: Group = Strg+g, FX = Strg+f
const char customKeyMapping[] = {'g', 'f'};

// ==========================================
// 4. STATUS-VARIABLEN (SPEICHER)
// ==========================================

bool lastEncBtnState[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long lastEncDebounceTime[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool encBtnStable[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

bool lastFKeyState[] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastFKeyDebounceTime[] = {0, 0, 0, 0};
bool fKeyStable[] = {HIGH, HIGH, HIGH, HIGH};

bool lastShiftState = HIGH;
unsigned long lastShiftDebounce = 0;

bool lastCustomKeyState[] = {HIGH, HIGH};
unsigned long lastCustomKeyDebounceTime[] = {0, 0};
bool customKeyStable[] = {HIGH, HIGH};

// ==========================================
// SETUP
// ==========================================

// ============================================================
// KALIBRIER-UI (Start mit gedrueckter SHIFT-Taste)
// ============================================================
// Encoder-Klicks 1..4 = -500 / -50 / +50 / +500 verschieben die gewaehlte
// Stellschraube, Encoder-Klicks 5..8 = Stellschraube 1..4 waehlen (LED blinkt
// so oft), GROUP = speichern (LittleFS) und zurueck zum Normalbetrieb.
// Der Mauszeiger folgt der Stellschraube, damit man sieht, was just verstellt
// wird - der Cursor wandert also mit und das ist Absicht.
void encCalBlink(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(ledPin, HIGH);
    delay(120);
    digitalWrite(ledPin, LOW);
    delay(120);
  }
  delay(500);
}

void encCalUi() {
  const char *pName[4] = { "X Spalte A (Enc 1)", "X Spalte B (Enc 5)",
                            "Y oben     (Enc 1)", "Y unten    (Enc 4)" };
  // Cursor-Index je Stellschraube: xA->Enc1, xB->Enc5, yTop->Enc1, yBot->Enc4
  const uint8_t pEnc[4] = { 0, 4, 0, 3 };
  uint16_t *pVal[4] = { &encCal.xA, &encCal.xB, &encCal.yTop, &encCal.yBot };
  const int32_t pStep[4] = { 500, 50, 50, 500 };   // Vorzeichen: +/-500, +/-50
  const int16_t pMin[4] = { 0, 0, 0, 0 };
  const int16_t pMax[4] = { 32767, 32767, 32767, 32767 };

  uint8_t sel = 0;
  uint16_t swPrev = 0xFFFF;
  bool groupPrev = HIGH;
  bool fPrev[4] = { HIGH, HIGH, HIGH, HIGH };

  Serial.println("[CAL] Encoder-Positionskalibrierung");
  Serial.println("[CAL] Enc1-4 = -500/-50/+50/+500   Enc5-8 = Stellschraube 1-4");
  Serial.println("[CAL] GROUP = speichern + fertig");
  applyEncCal();
  absMouseMove(ENC_TARGET_X[pEnc[sel]], ENC_TARGET_Y[pEnc[sel]]);
  encCalBlink(sel + 1);

  for (;;) {
    if (!mcpOK) {
      Serial.println("[CAL] ABBRUCH: MCP23017 fehlt - nicht kalibrierbar");
      return;
    }
    uint16_t sw = mcp.readGPIOAB();
    bool group = digitalRead(groupKeyPin);
    bool f[4];
    for (int i = 0; i < 4; i++) f[i] = digitalRead(fKeyPins[i]);

    // Encoder-Klicks 1..4: verstellen
    for (int i = 0; i < 4; i++) {
      bool pressed = ((sw >> encBtnMCP[i]) & 1) == 0;
      bool was = ((swPrev >> encBtnMCP[i]) & 1) == 0;
      if (pressed && !was) {
        int32_t v = (int32_t)(*pVal[sel]) + ((i < 2) ? -pStep[i] : pStep[i]);
        if (v < pMin[sel]) v = pMin[sel];
        if (v > pMax[sel]) v = pMax[sel];
        *pVal[sel] = (uint16_t)v;
        applyEncCal();
        absMouseMove(ENC_TARGET_X[pEnc[sel]], ENC_TARGET_Y[pEnc[sel]]);
        Serial.printf("[CAL] %s = %u\n", pName[sel], (unsigned)*pVal[sel]);
        encCalBlink(i + 1);
      }
    }

    // Encoder-Klicks 5..8: Stellschraube waehlen
    for (int i = 4; i < 8; i++) {
      bool pressed = ((sw >> encBtnMCP[i]) & 1) == 0;
      bool was = ((swPrev >> encBtnMCP[i]) & 1) == 0;
      if (pressed && !was) {
        sel = (uint8_t)(i - 4);
        Serial.printf("[CAL] Stellschraube %u: %s = %u\n", sel + 1, pName[sel],
                      (unsigned)*pVal[sel]);
        encCalBlink(sel + 1);
      }
    }

    // F1..F4: Stellschraube direkt waehlen (Alternative zu Enc5-8)
    for (int i = 0; i < 4; i++) {
      if (f[i] == LOW && fPrev[i] == HIGH) {
        sel = (uint8_t)i;
        Serial.printf("[CAL] Stellschraube %u: %s = %u\n", sel + 1, pName[sel],
                      (unsigned)*pVal[sel]);
        encCalBlink(sel + 1);
      }
      fPrev[i] = f[i];
    }

    // GROUP: speichern + beenden
    if (group == LOW && groupPrev == HIGH) {
      bool ok = saveEncCal();
      applyEncCal();
      Serial.printf("[CAL] gespeichert (%s): xA=%u xB=%u yTop=%u yBot=%u\n",
                    ok ? "LittleFS" : "FEHLER", encCal.xA, encCal.xB,
                    encCal.yTop, encCal.yBot);
      encCalBlink(8);
      return;
    }

    swPrev = sw;
    groupPrev = group;
    delay(10);   // Entprellen: Klicks werden erst nach ~20 ms ausgewertet
  }
}

// ENCODER 1 beim Start gedrueckt -> Kalibrier-UI (Klick auf GPA0 am MCP23017,
// zweimal gelesen damit ein Kontaktpreller nicht schon beim Start ausloest).
bool encCalBootKey() {
  if (!mcpOK) return false;
  uint16_t sw = mcp.readGPIOAB();
  if (sw & (1u << encBtnMCP[0])) return false;   // nicht gedrueckt -> normal
  delay(60);
  sw = mcp.readGPIOAB();
  return (sw & (1u << encBtnMCP[0])) == 0;
}

void setup() {
  Serial.begin(115200);

  // Warte kurz, bis der Serial-Monitor sicher verbunden ist, damit die
  // Boot-Ausgabe nicht verloren geht.
  delay(1500);

  // Onboard-LED als Boot-/Diagnose-Anzeige (3x Blinken)
  pinMode(ledPin, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }

  DEBUG_MSG("========== %s ==========\n", DEBUG_VERSION);

  // USB-HID starten (Keyboard + Absolute-Mouse + relative Mouse als Composite).
  // Die relative Maus wird als erster HID-Report registriert (ordering 8),
  // die Absolut-Maus folgt (9), das Keyboard zuletzt (10/11) - macOS verlangt
  // Maus-Reports VOR den Keyboard-Reports im Composite.
  Keyboard.begin();
  absMouseBegin();

  Serial.println("[BOOT] ready: encoders=F5-F8=shift=GP18-22, group=GP26, fx=GP27");

  // I²C für MCP23017 initialisieren
  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  // MCP23017 mit Pullups konfigurieren (Tasten schalten gegen GND)
  // Fehlgeschlagene Erkennung => mcpOK=false, die Encoder-Klicks werden
  // sicher übersprungen statt die I²C-Register zu lesen (BusIO-Crash).
  if (!mcp.begin_I2C(0x20)) {
    mcpOK = false;
    Serial.println("[WARN] MCP23017 nicht erkannt! (I2C-Adresse 0x20?) - Encoder-Klicks gehen nicht, Rest weiter");
  } else {
    mcpOK = true;
    Serial.println("[BOOT] MCP23017 erkannt (0x20)");
    // Encoder-SW (GPA0-GPA7): INPUT_PULLUP
    for (int i = 0; i < 8; i++) {
      mcp.pinMode(encBtnMCP[i], INPUT_PULLUP);
    }
  }

  // Encoder-Pins (CLK/DT): interner Pullup
  int encPins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  for (int i = 0; i < 16; i++) {
    pinMode(encPins[i], INPUT_PULLUP);
  }

  // F-Tasten (GP18-GP21): interne Pullups
  for (int i = 0; i < 4; i++) {
    pinMode(fKeyPins[i], INPUT_PULLUP);
  }

  // Shift (GP22): interner Pullup
  pinMode(shiftKeyPin, INPUT_PULLUP);

  // Group (GP26) / FX (GP27): interne Pullups
  pinMode(groupKeyPin, INPUT_PULLUP);
  pinMode(fxKeyPin, INPUT_PULLUP);

  // Kalibrierdaten aus LittleFS (sonst Defaults aus dem Raster 2x4).
  if (loadEncCal()) {
    Serial.printf("[CAL] /ecal.bin geladen: xA=%u xB=%u yTop=%u yBot=%u\n",
                  encCal.xA, encCal.xB, encCal.yTop, encCal.yBot);
  } else {
    Serial.printf("[CAL] Defaults: xA=%u xB=%u yTop=%u yBot=%u\n",
                  encCal.xA, encCal.xB, encCal.yTop, encCal.yBot);
  }
  applyEncCal();

  // Mit ENCODER 1 gedrueckt starten -> Positionskalibrierung.
  if (encCalBootKey()) {
    Serial.println("[CAL] ENCODER 1 beim Start gedrueckt -> Kalibrier-UI");
    encCalUi();
    Serial.println("[CAL] Kalibrierung beendet");
  }
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  unsigned long currentMillis = millis();

  // Status einmalig und nach jedem (Re-)Mount ausgeben. Die Boot-Prints nach
  // Keyboard.begin()/absMouseBegin() gehen sonst beim USB-Re-Enum verloren.
  static bool mountedWas = false;
  static unsigned long lastStatusPrint = 0;
  bool mountedNow = (tud_mounted() != 0);
  if (mountedNow && (!mountedWas || (currentMillis - lastStatusPrint > 2000))) {
    lastStatusPrint = currentMillis;

    // Live-Ping an den MCP23017 (Adresse 0x20): antwortet er beim I²C-Scan?
    Wire.beginTransmission(0x20);
    int ack = Wire.endTransmission();

    Serial.printf("[STATUS] mounted=1 mcpOK=%d ack0x20=%d relLocalID=%u absLocalID=%u relRID=%u absRID=%u\n",
                  mcpOK ? 1 : 0, ack,
                  relMouseLocalID, absMouseLocalID,
                  USB.findHIDReportID(relMouseLocalID), USB.findHIDReportID(absMouseLocalID));
    if (!mcpOK) {
      Serial.println("[WARN] MCP23017 NICHT erkannt -> Encoder-Klicks deaktiviert!");
    }
    if (ack != 0) {
      Serial.println("[WARN] I2C 0x20 antwortet nicht (ack != 0). Verdrahtung/Pullups/Adresse pruefen.");
    } else if (!mcpOK) {
      Serial.println("[INFO] MCP antwortet jetzt auf 0x20 -> ggf. Neustart, wird dann erkannt.");
    }

    // Vollständiger Adress-Scan, um den MCP bei abweichender Adresse zu finden.
    Serial.print("[I2CSCAN] ");
    bool any = false;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("0x%02X ", addr);
        any = true;
      }
    }
    if (!any) {
      Serial.print("kein Geraet gefunden!");
    }
    Serial.println();
  }
  mountedWas = mountedNow;

#if PIN_FINDER
  const int findPins[] = {20, 21, 22, 23, 24, 26, 27, 28};
  static int lastFound = -1;
  int found = -1;
  for (unsigned int i = 0; i < sizeof(findPins)/sizeof(findPins[0]); i++) {
    if (digitalRead(findPins[i]) == LOW) {
      found = i;
      break;
    }
  }
  if (found != lastFound) {
    lastFound = found;
    digitalWrite(ledPin, LOW);
    for (int b = 0; b <= found; b++) {
      digitalWrite(ledPin, HIGH);
      delay(150);
      digitalWrite(ledPin, LOW);
      delay(150);
    }
  }
  delay(50);
  return;
#else

  // ------------------------------------------
  // TEIL 1: SHIFT-TASTE (GP22) - Modifier, wird gehalten
  // ------------------------------------------

  bool readingShift = digitalRead(shiftKeyPin);
  if (readingShift != lastShiftState) {
    lastShiftDebounce = currentMillis;
  }
  if ((currentMillis - lastShiftDebounce) > DEBOUNCE_MS) {
    if (readingShift != shiftStable) {
      shiftStable = readingShift;
      ledFlash();
      if (readingShift == LOW) {
        Keyboard.press(KEY_LEFT_SHIFT);
      } else {
        Keyboard.release(KEY_LEFT_SHIFT);
      }
    }
  }
  lastShiftState = readingShift;

  // Keep-alive: macOS/MagicQ werten ein einzelnes Shift-DOWN als "Tap" ab.
  // Während die Shift-Taste gehalten wird, sendet das Pico periodisch das
  // DOWN-Report erneut (wie der Auto-Repeat einer echten Tastatur).
  if (shiftStable == LOW) {
    static unsigned long lastShiftKeep = 0;
    if (currentMillis - lastShiftKeep > SHIFT_KEEPALIVE_MS) {
      lastShiftKeep = currentMillis;
      Keyboard.press(KEY_LEFT_SHIFT);
    }
  }

  // ------------------------------------------
  // TEIL 2: ENCODER DREHUNG & KLICK (8 Stück)
  // ------------------------------------------

#if ENC_CALIBRATE
  // Kalibrier-Modus: Cursor wandert nacheinander auf jeden der 8 Zielpunkte.
  for (int i = 0; i < 8; i++) {
    absMouseMove(ENC_TARGET_X[i], ENC_TARGET_Y[i]);
    Serial.printf("CALIB target %d: x=%d y=%d\n", i + 1, ENC_TARGET_X[i], ENC_TARGET_Y[i]);
    for (int b = 0; b <= i; b++) {
      digitalWrite(ledPin, HIGH);
      delay(150);
      digitalWrite(ledPin, LOW);
      delay(150);
    }
    delay(4000);
  }
  return;
#else

  // Encoder-Klick-Status: ein I²C-Read für alle 8 SW statt 8 Einzelreads.
  // GPA0..GPA7 (Encoder-SW 1..8) = Bits 0..7 des GPIOAB-Registers (aktiv-Low).
  uint16_t encSw = mcpOK ? mcp.readGPIOAB() : 0xFFFF;

  // Raw-Änderungen am MCP ausgeben (aktiv-Low: Bit=0 => Taste gedrückt).
  static uint16_t lastEncSwRaw = 0xFFFF;
  if (DEBUG_VERBOSE && mcpOK && encSw != lastEncSwRaw) {
    Serial.printf("[ENCRAW] GPIOAB=0x%04X (gedrückt: ", encSw);
    for (int b = 0; b < 8; b++) {
      if (!(encSw & (1 << b))) {
        Serial.printf("ENC%d ", b + 1);
      }
    }
    Serial.println(")");
    lastEncSwRaw = encSw;
  }

  for (int i = 0; i < 8; i++) {
    // Drehung: process() liefert DIR_CW, DIR_CCW oder DIR_NONE
    unsigned char dir = encoders[i]->process();
    if (dir == DIR_CW || dir == DIR_CCW) {
      signed char step = (dir == DIR_CW) ? -MOUSE_WHEEL_STEP : MOUSE_WHEEL_STEP;
      // Cursor zuerst auf den On-Screen-Encoder positionieren, dann scrollen.
      absMouseReport(ENC_TARGET_X[i], ENC_TARGET_Y[i], step, 0);
      Serial.printf("ENC %d -> pos(%d,%d) wheel %+d\n", i + 1,
                    ENC_TARGET_X[i], ENC_TARGET_Y[i], step);
      ledFlash();
    }

    // Encoder-Klick: nur auswerten, wenn der Expander vorhanden ist.
    // Ohne MCP bleibt der Klick deaktiviert, aber Dreh- und F-Tasten
    // funktionieren trotzdem weiter.
    if (!mcpOK) continue;

    bool readingEnc = ((encSw >> encBtnMCP[i]) & 1) ? HIGH : LOW;
    if (readingEnc != lastEncBtnState[i]) {
      lastEncDebounceTime[i] = currentMillis;
    }
    if ((currentMillis - lastEncDebounceTime[i]) > DEBOUNCE_MS) {
      if (readingEnc != encBtnStable[i]) {
        encBtnStable[i] = readingEnc;
        if (readingEnc == LOW) {
          ledFlash();
          absMouseClickAt(ENC_TARGET_X[i], ENC_TARGET_Y[i]);
          Serial.printf("ENC %d -> click at (%d,%d)\n", i + 1,
                        ENC_TARGET_X[i], ENC_TARGET_Y[i]);
        }
      }
    }
    lastEncBtnState[i] = readingEnc;
  }
#endif

  // ------------------------------------------
  // TEIL 3: ATTRIBUT-TASTEN (F5 bis F8)
  // ------------------------------------------

  for (int i = 0; i < 4; i++) {
    bool readingFKey = digitalRead(fKeyPins[i]);
    if (readingFKey != lastFKeyState[i]) {
      lastFKeyDebounceTime[i] = currentMillis;
    }
    if ((currentMillis - lastFKeyDebounceTime[i]) > DEBOUNCE_MS) {
      if (readingFKey != fKeyStable[i]) {
        fKeyStable[i] = readingFKey;
        if (readingFKey == LOW) {
          ledFlash();
          sendCtrlKey(fKeyMapping[i]);
        }
      }
    }
    lastFKeyState[i] = readingFKey;
  }

  // ------------------------------------------
  // TEIL 4: CUSTOM-TASTEN (Group, FX)
  // ------------------------------------------

  const int customKeyPins[] = {groupKeyPin, fxKeyPin};
  for (int i = 0; i < 2; i++) {
    int readingCustomKey = digitalRead(customKeyPins[i]);

    if (readingCustomKey != lastCustomKeyState[i]) {
      lastCustomKeyDebounceTime[i] = currentMillis;
    }

    if ((currentMillis - lastCustomKeyDebounceTime[i]) > DEBOUNCE_MS) {
      if (readingCustomKey != customKeyStable[i]) {
        customKeyStable[i] = readingCustomKey;
        if (readingCustomKey == LOW) {
          Serial.print("[KEY] GPIO");
          Serial.print(customKeyPins[i]);
          Serial.print(" -> Ctrl+");
          Serial.print(customKeyMapping[i]);
          Serial.println();
          ledFlash();
          sendCtrlKey(customKeyMapping[i]);
        }
      }
    }
    lastCustomKeyState[i] = readingCustomKey;
  }

  serviceLed();

  delay(2);
#endif
}