#include <Arduino.h>
#include <Keyboard.h>
#include <Rotary.h>
#include <Adafruit_MCP23X17.h>

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
// 2. ZEIT-/VERHALTENS-KONFIGURATION
// ==========================================
const unsigned long DEBOUNCE_MS = 50;    // Entprellzeit aller Tasten
const unsigned long SHIFT_KEEPALIVE_MS = 60; // Nachsendung Shift DOWN, solange gehalten

// Kurzer LED-Blink bei jedem gesendeten Tastendruck (Diagnose)
void ledFlash() {
  digitalWrite(ledPin, HIGH);
  delay(8);
  digitalWrite(ledPin, LOW);
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
const char encChars[] = {'1', '2', '3', '4', '5', '6', '7', '8'};

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

void setup() {
  Serial.begin(115200);

  // Onboard-LED als Boot-/Diagnose-Anzeige (3x Blinken)
  pinMode(ledPin, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }

  // USB-HID-Tastatur starten (eingebaut im Arduino-Pico-Core)
  Keyboard.begin();

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
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  unsigned long currentMillis = millis();

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

  // Encoder-Klick-Status: ein I²C-Read für alle 8 SW statt 8 Einzelreads.
  // GPA0..GPA7 (Encoder-SW 1..8) = Bits 0..7 des GPIOAB-Registers (aktiv-Low).
  uint16_t encSw = mcpOK ? mcp.readGPIOAB() : 0xFFFF;

  for (int i = 0; i < 8; i++) {
    // Drehung: process() liefert DIR_CW, DIR_CCW oder DIR_NONE
    unsigned char dir = encoders[i]->process();
    if (dir == DIR_CW) {
      Keyboard.write(encChars[i]);
      Keyboard.write('+');
    } else if (dir == DIR_CCW) {
      Keyboard.write(encChars[i]);
      Keyboard.write('-');
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
          Keyboard.write(encChars[i]);
        }
      }
    }
    lastEncBtnState[i] = readingEnc;
  }

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

  delay(2);
#endif
}