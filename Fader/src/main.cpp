// DIY MagicQ Compact Mini Connect Wing
// Raspberry Pi Pico + 74HC4067 mux + 3x13 key matrix + USB HID keyboard
// PlatformIO (earlephilhower Arduino core, native TinyUSB HID)

// Temporäre Fader-Deaktivierung (Testing ohne Fader): auf 1 setzen zum Wiedereinschalten
#define ENABLE_FADERS 1

// Temporärer HID-Selbsttest: auf 1 setzen, flashen, Cursor in TextEdit setzen,
// Pico neu einstecken. Die Firmware tippt nach 5 s "HID-OK" in den fokussierten
// Editor. Danach wieder auf 0 setzen.
#define HID_SELFTEST 0

#include <Arduino.h>
#include <Keyboard.h>

// ---- Pin mapping (see README) ----
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

// ---- Keycodes ----
// Zellen-Tabelle per Dump verifiziert (Keycap -> Zelle r.c):
//   ALT=(0,0), S1..S10=(0,2..11), MASTER GO=(0,12),
//   DBO=(1,0), GO1..GO10=(1,2..11), RELEASE=(1,12),
//   SWOP=(2,0), PREV=(2,1), Flash F1..F10=(2,2..11), MASTER PAUSE=(2,12)
// MagicQ "Playback shortcuts" (läuft auf Mac + PC/Linux):
//   S1..S10 = 1..0, GO = Q..P, Flash = '\' z x c v b n m , .
//   SWOP = '`', MASTER PAUSE = '#', MASTER GO = Space,
//   DBO (Phys.) = F11, INSERT (1,1) = '[', PREV = ']',
//   ALT = DBO-Schnellbefehl: Control + Option + 0 (wie Modifier halten)
// Flash-Tasten sind MagicQ "Test"-Keys: sie TOGGLEN das Playback 100% an/aus.
// Daher senden wir beim DRUECKEN die Taste (an) und beim LOSLASSEN
// die Taste erneut (aus) -> das ergibt ein momentanes Flash.
#define KEY_DBO_COMBO 0x01   // Markierung: ALT-Funktion (Ctrl+Alt+0) statt Einzeltaste

bool isFlashKey(unsigned char k) {
    return (k == '\\' || k == 'z' || k == 'x' || k == 'c' ||
            k == 'v'  || k == 'b' || k == 'n' || k == 'm' ||
            k == ','  || k == '.');
}

constexpr unsigned char MATRIX[3][13] = {
    {KEY_DBO_COMBO, '[', '1', '2', '3', '4', '5',
     '6',           '7', '8', '9', '0', ' '},                       // ALT, INSERT?, S1..S10, MASTER GO
    {KEY_F11,       '[', 'q', 'w', 'e', 'r', 't',
     'y',           'u', 'i', 'o', 'p', '-'},                       // DBO, INSERT, GO1..GO10, RELEASE
    {'`',           ']',
     '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
     ',',  '.', '#'}                                                // SWOP, PREV, Flash1..10, MASTER PAUSE
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
const unsigned char SEL_KEYS[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
const unsigned char GO_KEYS[10]  = {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'};
const uint32_t CHORD_MS = 40;
static uint8_t  pairState[10];       // 0=idle, 1=S unten, 2=GO unten, 3=Chord(STOP)
static uint32_t soloSince[10];       // Zeitpunkt des Solo-Druckes
static bool     keySent[10][2];      // [S,GO] aktuell am Host gedrueckt

// ---- State ----
#if ENABLE_FADERS
static int       lastFader[NUM_FADERS];
static uint32_t  lastMove[NUM_FADERS];
static bool      sendReady[NUM_FADERS];
static int       faderTarget[NUM_FADERS];
#endif
static bool      keyState[3][13];
static bool      keyRaw[3][13];
static uint32_t  keySince[3][13];

// Liefert die Playback-Nummer (1..10) fuer die Zelle (r,c), oder 0.
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

// Tippt die aktuell gedrückten Zellen als "M R2C3 R0C1" in den Editor.
// Nur Ziffern und R,C -> layoutsicher (auch auf deutscher Tastatur).
void dumpMatrixText() {
    typeText("M");
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], HIGH);
        delayMicroseconds(10);
        for (uint8_t c = 0; c < 13; c++) {
            if (digitalRead(COL_PINS[c]) == HIGH) {
                char buf[8];
                snprintf(buf, sizeof(buf), " R%dC%d", r, c);
                typeText(buf);
            }
        }
        digitalWrite(ROW_PINS[r], LOW);
    }
    typeText("\n");
}

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
    while (!Serial && (millis() - serialWait < 5000)) { delay(10); }
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
        lastFader[i]   = -1;
        faderTarget[i] = -1;
        lastMove[i]    = 0;
        sendReady[i]   = false;
    }
#endif

    // Matrix rows (outputs, inactive = LOW)
    for (uint8_t p : ROW_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
    // Matrix cols (inputs, pull-down) - Active-High-Erkennung,
    // passt zur Diode mit Kathode Richtung Column (Anode Richtung Row/Switch)
    for (uint8_t p : COL_PINS) { pinMode(p, INPUT_PULLDOWN); }

    // Matrix-Selbsttest beim Boot: zeigt pro Reihe den Rohzustand aller Spalten.
    // HIGH = gedrückt/kurzgeschlossen, LOW = offen (normal ohne Tastendruck)
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], HIGH);
        delayMicroseconds(10);
        Serial.print("row ");
        Serial.print(r);
        Serial.print(" cols: ");
        for (uint8_t c = 0; c < 13; c++) {
            // 1 = aktiv/HIGH, 0 = offen/LOW
            Serial.print(digitalRead(COL_PINS[c]) == HIGH ? "1" : "0");
        }
        Serial.println();
        digitalWrite(ROW_PINS[r], LOW);
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

void loop() {
    uint32_t now = millis();

    // ---- 1. Key matrix scan (active-high) ----
    for (uint8_t r = 0; r < 3; r++) {
        digitalWrite(ROW_PINS[r], HIGH);         // row active
        delayMicroseconds(10);                   // allow lines to settle
        for (uint8_t c = 0; c < 13; c++) {
            unsigned char key = MATRIX[r][c];
            if (key == 0) continue;              // unused cell

            bool pressed = (digitalRead(COL_PINS[c]) == HIGH);

            // Prellen: Zustand muss 25 ms stabil sein, bevor er als Ereignis gilt.
            if (pressed != keyRaw[r][c]) {
                keyRaw[r][c] = pressed;
                keySince[r][c] = now;
            }

            if (pressed && !keyState[r][c] && (now - keySince[r][c]) > 25) {
                int pb = playbackIndex(r, c);
                if (key == KEY_DBO_COMBO) {
                    // MagicQ Dead Black Out (Mac): Control + Option + 0
                    // Wie eine Modifier-Taste halten bis zum Loslassen:
                    Keyboard.press(KEY_LEFT_CTRL);
                    Keyboard.press(KEY_LEFT_ALT);
                    Keyboard.press('0');
                    debugKey("PRESS DBO", key);
                } else if (isFlashKey(key)) {
                    // Test-Taste AN toggeln; sofort wieder loslassen, damit
                    // macOS kein Auto-Repeat (,,,,) und MagicQ kein Flackern bekommt.
                    Keyboard.press(key);
                    Keyboard.release(key);
                    debugKey("FLASH ON", key);
                } else if (pb != 0) {
                    // S- oder GO-Taste: hier NICHT senden - die S+GO-Phase
                    // entscheidet nach dem Fenster (STOP-Chord oder Einzeltaste).
                } else {
                    Keyboard.press(key);
                    debugKey("PRESS", key);
                }
                keyState[r][c] = true;
                digitalWrite(LED_BUILTIN, HIGH);        // LED an solange Taste gehalten
            } else if (!pressed && keyState[r][c] && (now - keySince[r][c]) > 25) {
                int pb = playbackIndex(r, c);
                keyState[r][c] = false;
                digitalWrite(LED_BUILTIN, LOW);         // Taste losgelassen -> LED aus
                if (isFlashKey(key)) {
                    // Test-Taste AUS toggeln; sofort loslassen (kein Auto-Repeat).
                    Keyboard.press(key);
                    Keyboard.release(key);
                    debugKey("FLASH OFF", key);
                } else if (key == KEY_DBO_COMBO) {
                    Keyboard.release(KEY_LEFT_CTRL);
                    Keyboard.release(KEY_LEFT_ALT);
                    Keyboard.release('0');
                } else if (pb != 0) {
                    // S-/GO-Taste: Loslassen uebernimmt die S+GO-Phase.
                } else {
                    Keyboard.release(key);
                }
                debugKey("RELEASE", key);
            }
        }
        digitalWrite(ROW_PINS[r], LOW);          // row inactive
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
        if (now - soloSince[i] >= CHORD_MS) {        // Fenster abgelaufen -> Einzeltaste
            if (pairState[i] == 1 && !keySent[i][0]) { Keyboard.press(KC_S); keySent[i][0] = true; debugKey("SEL", KC_S); }
            if (pairState[i] == 2 && !keySent[i][1]) { Keyboard.press(KC_G); keySent[i][1] = true; debugKey("GO", KC_G); }
        }
    }

    // Keep-alive: solange eine Taste gehalten wird, sendet das Pico periodisch
    // das DOWN-Report erneut (wie ein Auto-Repeat einer echten Tastatur).
    // macOS/MagicQ schalten sonst einen einzelnen DOWN-Event als "Tap" ab.
    static uint32_t lastKeep = 0;
    if (now - lastKeep > 60) {
        lastKeep = now;
        for (uint8_t r = 0; r < 3; r++) {
            for (uint8_t c = 0; c < 13; c++) {
                if (playbackIndex(r, c) != 0) continue;   // S/GO uebernimmt die Phase
                if (keyState[r][c] && !isFlashKey(MATRIX[r][c])) {
                    unsigned char k = MATRIX[r][c];
                    if (k == KEY_DBO_COMBO) {
                        Keyboard.press(KEY_LEFT_CTRL);
                        Keyboard.press(KEY_LEFT_ALT);
                        Keyboard.press('0');
                    } else if (k != 0) {
                        Keyboard.press(k);
                    }
                }
            }
        }
        // Gehaltene, bereits gesendete S-/GO-Tasten verlaengern
        for (int pb = 1; pb <= 10; pb++) {
            int i = pb - 1;
            if (pairState[i] == 1 && keySent[i][0]) Keyboard.press(SEL_KEYS[i]);
            if (pairState[i] == 2 && keySent[i][1]) Keyboard.press(GO_KEYS[i]);
        }
    }

    // Diagnose-Kombo: S10 (row0 col11) + GO10 (row1 col11) gleichzeitig halten
    // -> tippt gedrückte Zellen als "M:r.c ...;" in den Editor.
    static bool diagReported = false;
    if (keyState[0][11] && keyState[1][11]) {
        if (!diagReported) {
            diagReported = true;
            dumpMatrixText();
        }
    } else if (diagReported) {
        diagReported = false;
    }

    // ---- 2. Fader scan ----
#if ENABLE_FADERS
    for (uint8_t f = 0; f < NUM_FADERS; f++) {
        // Select channel on the mux
        digitalWrite(MUX_S0, (f & 1) ? HIGH : LOW);
        digitalWrite(MUX_S1, (f & 2) ? HIGH : LOW);
        digitalWrite(MUX_S2, (f & 4) ? HIGH : LOW);
        digitalWrite(MUX_S3, (f & 8) ? HIGH : LOW);
        delayMicroseconds(30);                  // signal settling
        uint16_t raw = analogRead(MUX_ADC);     // 0..65535 on RP2040
        int pct = (int)((raw * 100L) / 65535L);

        if (abs(pct - lastFader[f]) > 1) {
            lastFader[f]   = pct;
            faderTarget[f] = pct;
            lastMove[f]    = now;
            sendReady[f]   = true;
        }

        if (sendReady[f] && (now - lastMove[f] > 150)) {
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

            blinkLed();
            sendReady[f] = false;
        }
    }
#endif

    delay(5);
}