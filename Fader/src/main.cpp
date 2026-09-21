// DIY MagicQ Compact Mini Connect Wing
// Raspberry Pi Pico + 74HC4067 mux + 3x13 key matrix + USB HID keyboard
// PlatformIO (earlephilhower Arduino core, native TinyUSB HID)

// Temporäre Fader-Deaktivierung (Testing ohne Fader): auf 1 setzen zum Wiedereinschalten
#define ENABLE_FADERS 1

// ADC-Rohwert-Scan beim Boot (Diagnose der Mux-Verdrahtung) auf 1 setzen.
#define FADER_BOOT_SCAN 1

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

#include <Arduino.h>
#include <Keyboard.h>

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

// ============================================================
// Timing / Deadzone-Konstanten (hier zentral einstellbar)
// ============================================================
constexpr uint32_t KEY_DEBOUNCE_MS = 25;   // Matrixtaste: Zustand muss so lange stabil sein
constexpr uint32_t CHORD_MS        = 40;   // S+GO-Fenster ("Pause statt Einzeltaste")
constexpr uint32_t KEEPALIVE_MS    = 60;   // Intervall zum Neu-Senden gehaltener Tasten

constexpr uint8_t  FADER_SAMPLES     = 8;  // ADC-Reads, die pro Fader gemittelt werden
constexpr int      FADER_DEADZONE    = 2;  // % Abstand zum letzten Sendewert, ab dem neu gesendet wird
constexpr uint8_t  FADER_ENDSTOP     = 1;  // % innerhalb dieses Rands wird auf 0/100 geschnappt
constexpr uint32_t FADER_QUIET_MS    = 150; // Senden erst nach dieser Ruhezeit (Sendekoaleszenz)

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
static int       faderSent[NUM_FADERS];    // zuletzt gesendeter % (roh, -1 = noch nie)
static int       faderTarget[NUM_FADERS];  // zuletzt gemessener %-Wert
static uint32_t  lastMove[NUM_FADERS];
static bool      sendReady[NUM_FADERS];
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
// Gibt alle Fader-Level als " GM=99 PB1=42 ..." aus (gesendete Werte).
void dumpFaderText() {
    typeText("F");
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        char buf[16];
        int v = (faderSent[f] < 0) ? 0 : faderSent[f];   // nie bewegt -> 0
        if (f == 0) {
            snprintf(buf, sizeof(buf), " GM=%d", v);
        } else {
            snprintf(buf, sizeof(buf), " PB%d=%d", f, v);
        }
        typeText(buf);
    }
    typeText("\n");
}

// Liest einen Fader-Kanal des Mux mit Mittelung über mehrere ADC-Reads und
// Endstop-Schnapp auf 0/100. Gibt 0..100 zurück.
int readFaderPct(uint8_t f) {
    digitalWrite(MUX_S0, (f & 1) ? HIGH : LOW);
    digitalWrite(MUX_S1, (f & 2) ? HIGH : LOW);
    digitalWrite(MUX_S2, (f & 4) ? HIGH : LOW);
    digitalWrite(MUX_S3, (f & 8) ? HIGH : LOW);
    delayMicroseconds(30);                          // signal settling

    uint32_t sum = 0;
    for (uint8_t s = 0; s < FADER_SAMPLES; s++) {
        sum += analogRead(MUX_ADC);
        delayMicroseconds(10);                      // Abstand zwischen Reads
    }
    int pct = (int)((sum * 100L) / ((uint32_t)FADER_SAMPLES * 65535L));

    // Potis erreichen selten exakt 0/100: nahe den Enden sauber einklemmen.
    if (pct <= FADER_ENDSTOP)        pct = 0;
    else if (pct >= 100 - FADER_ENDSTOP) pct = 100;
    return pct;
}

// Sendet den aktuellen Zielwert eines Faders als Keyboard-Kommando.
void sendFader(uint8_t f) {
    char cmd[32];
    if (f == 0) {
        snprintf(cmd, sizeof(cmd), "gm @ %d", faderTarget[f]);
    } else {
        snprintf(cmd, sizeof(cmd), "pb%d @ %d", f, faderTarget[f]);
    }
    // Kleinbuchstaben: MagicQ ist case-insensitiv. Weniger Shift-Events
    // -> weniger Störung der physischen Shift-Taste auf macOS.
    typeText(cmd);
    Keyboard.write('\n');   // ENTER executes the command (en_US maps LF to Enter)

    faderSent[f] = faderTarget[f];
    Serial.printf("FADER %d -> %d%%\n", f, faderSent[f]);
    blinkLed();
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
        faderSent[i]   = -1;
        faderTarget[i] = 0;
        lastMove[i]    = 0;
        sendReady[i]   = false;
    }

#if FADER_BOOT_SCAN
    // Boot-Diagnose: Roh- und %-Wert jedes Mux-Kanals prüft die Verdrahtung
    // (Fader = Poti: hochziehen des Werten muss im Prozentwert sichtbar sein).
    Serial.println("Fader ADC scan (ch raw%):");
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        digitalWrite(MUX_S0, (f & 1) ? HIGH : LOW);
        digitalWrite(MUX_S1, (f & 2) ? HIGH : LOW);
        digitalWrite(MUX_S2, (f & 4) ? HIGH : LOW);
        digitalWrite(MUX_S3, (f & 8) ? HIGH : LOW);
        delayMicroseconds(30);
        uint16_t raw = analogRead(MUX_ADC);
        Serial.printf("  ch%d raw=%5u %d%%\n", f, raw,
                      (int)((raw * 100L) / 65535L));
    }
#endif
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

    // Start USB HID
    Keyboard.begin();

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

    // ---- 2. Fader scan ----
#if ENABLE_FADERS
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        int pct = readFaderPct(f);

        // Deadzone: erst senden, wenn sich der Wert um mehr als FADER_DEADZONE
        // % vom zuletzt GESENDETEN Wert wegbewegt hat. Das verhindert, dass
        // ADC-Rauschen am Ruhewert endlos identische Kommandos tippt.
        // Jede merkliche Änderung setzt den Ruhe-Timer der Koaleszenz zurück.
        bool moved = (faderSent[f] < 0) || (abs(pct - faderSent[f]) > FADER_DEADZONE);
        if (moved) {
            faderTarget[f] = pct;
            lastMove[f]    = now;
            sendReady[f]   = true;
        }

        if (sendReady[f] && (now - lastMove[f] > FADER_QUIET_MS)) {
            sendFader(f);
            sendReady[f] = false;
        }
    }
#endif

    delay(5);
}