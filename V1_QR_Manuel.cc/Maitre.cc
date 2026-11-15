
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ==================================================
// PMI BPOST - ESP32-S3 MAITRE
//
// QR1 et QR2 : detection locale
// QR3 et QR4 : detection via esclave
// OLED : position du QR
// Relais R1 a R4 : commandes par l'esclave
// ==================================================

// ---------- LECTEURS QR ----------

#define QR1_RX 15
#define QR1_TX 16

#define QR2_RX 17
#define QR2_TX 18

#define QR_BAUD 9600

HardwareSerial uartQR1(1);
HardwareSerial uartQR2(2);

// ---------- OLED ----------

#define OLED_SDA 8
#define OLED_SCL 9

U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE,
    OLED_SCL,
    OLED_SDA
);

// ---------- LIAISON I2C ----------

#define LINK_SDA 10
#define LINK_SCL 11

#define SLAVE_ADDR 0x42
#define EVENT_SIGNATURE 0xA5
#define ACK_SIGNATURE 0x5A

const uint32_t LINK_POLL_MS = 30;

// ---------- RELAIS ----------

const uint8_t RELAY_PINS[4] = {
    7, 6, 5, 4
};

// Module relais actif a LOW
#define RELAY_ON LOW
#define RELAY_OFF HIGH

bool relayActive[4] = {
    false, false, false, false
};

// ---------- FACES ----------

enum Face : uint8_t {
    AUCUNE = 0,
    GAUCHE = 1,
    DROITE = 2,
    AVANT = 3,
    ARRIERE = 4
};

Face lastFace = AUCUNE;

bool displayDirty = true;

// ---------- ETATS QR ----------

struct QRState {
    bool receiving = false;
    bool detectedOnce = false;
    uint32_t lastByteMs = 0;
    uint32_t lastDetectionMs = 0;
};

QRState stateQR1;
QRState stateQR2;

const uint32_t QR_GAP_MS = 80;
const uint32_t QR_LOCKOUT_MS = 1000;

// ---------- ETAT I2C ----------

uint32_t lastLinkPoll = 0;
uint32_t lastLinkErrorLog = 0;

uint8_t lastSlaveSequence = 0;
bool hasSlaveSequence = false;

// ==================================================
// NOM DES FACES
// ==================================================

const char* faceName(Face face) {

    switch (face) {
        case GAUCHE:  return "GAUCHE";
        case DROITE:  return "DROITE";
        case AVANT:   return "AVANT";
        case ARRIERE: return "ARRIERE";
        default:      return "AUCUNE";
    }
}

// ==================================================
// DETECTION QR
// ==================================================

void qrDetected(uint8_t face) {

    if (face < GAUCHE || face > ARRIERE) {
        return;
    }

    lastFace = static_cast<Face>(face);
    displayDirty = true;

    Serial.printf(
        "[QR%u] DETECTE - FACE %s\n",
        face,
        faceName(lastFace)
    );
}

// ==================================================
// FIN D'UNE LECTURE QR
// ==================================================

void finishQR(QRState &state, uint8_t face) {

    if (!state.receiving) {
        return;
    }

    state.receiving = false;

    uint32_t now = millis();

    if (state.detectedOnce &&
        now - state.lastDetectionMs < QR_LOCKOUT_MS) {
        return;
    }

    state.lastDetectionMs = now;
    state.detectedOnce = true;

    qrDetected(face);
}

// ==================================================
// LECTURE UART QR
// ==================================================

void pollQR(
    HardwareSerial &uart,
    QRState &state,
    uint8_t face
) {

    while (uart.available() > 0) {

        int value = uart.read();

        if (value < 0) {
            break;
        }

        uint8_t ch = (uint8_t)value;

        if (ch == '\r' ||
            ch == '\n' ||
            ch == 0x03) {

            finishQR(state, face);
        }

        else if (ch == 0x02) {

            state.receiving = false;
        }

        else if (ch >= 0x20 && ch != 0x7F) {

            state.receiving = true;
            state.lastByteMs = millis();
        }
    }

    if (state.receiving &&
        millis() - state.lastByteMs >= QR_GAP_MS) {

        finishQR(state, face);
    }
}

// ==================================================
// COMMANDE D'UN RELAIS
// ==================================================

void setRelay(uint8_t index, bool enable) {

    if (index >= 4) {
        return;
    }

    if (relayActive[index] == enable) {
        return;
    }

    relayActive[index] = enable;

    digitalWrite(
        RELAY_PINS[index],
        enable ? RELAY_ON : RELAY_OFF
    );

    displayDirty = true;

    Serial.printf(
        "[RELAIS R%u] %s\n",
        index + 1,
        enable ? "ACTIVE" : "DESACTIVE"
    );
}

// ==================================================
// COMMANDE DES 4 RELAIS AVEC UN MASQUE
// ==================================================
//
// bit 0 = bouton B1
// bit 1 = bouton B2
// bit 2 = bouton B3
// bit 3 = bouton B4
//
// Exemple : 00000101
// => R1 et R3 actifs
// ==================================================

void updateRelays(uint8_t buttonMask) {

    for (uint8_t i = 0; i < 4; i++) {

        bool enable =
            (buttonMask & (1 << i)) != 0;

        setRelay(i, enable);
    }
}

// ==================================================
// DESSIN D'UNE FACE
// ==================================================

void drawFaceBox(
    int x,
    int y,
    int w,
    int h,
    const char* label,
    Face face
) {

    bool selected = (lastFace == face);

    oled.setDrawColor(1);

    if (selected) {
        oled.drawBox(x, y, w, h);
    } else {
        oled.drawFrame(x, y, w, h);
    }

    oled.setFont(u8g2_font_5x7_tr);

    int textW = oled.getStrWidth(label);

    oled.setDrawColor(selected ? 0 : 1);

    oled.drawStr(
        x + (w - textW) / 2,
        y + (h + 6) / 2,
        label
    );

    oled.setDrawColor(1);
}

// ==================================================
// AFFICHAGE OLED
// ==================================================

void drawDisplay() {

    oled.clearBuffer();
    oled.setDrawColor(1);

    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(23, 12, "POSITION QR");

    oled.drawHLine(0, 17, 128);

    // Colis central
    oled.drawFrame(40, 49, 48, 38);
    oled.drawStr(49, 71, "COLIS");

    // Liaisons graphiques
    oled.drawVLine(64, 44, 5);
    oled.drawVLine(64, 87, 5);

    oled.drawHLine(37, 67, 3);
    oled.drawHLine(88, 67, 3);

    // Faces
    drawFaceBox(
        44, 29, 40, 15,
        "AVANT", AVANT
    );

    drawFaceBox(
        34, 92, 60, 15,
        "ARRIERE", ARRIERE
    );

    drawFaceBox(
        2, 60, 35, 15,
        "GCH", GAUCHE
    );

    drawFaceBox(
        91, 60, 35, 15,
        "DRT", DROITE
    );

    // Etats relais
    oled.drawHLine(0, 110, 128);

    oled.setFont(u8g2_font_5x7_tr);

    char line[28];

    snprintf(
        line,
        sizeof(line),
        "R1:%s  R2:%s",
        relayActive[0] ? "ON" : "OFF",
        relayActive[1] ? "ON" : "OFF"
    );

    oled.drawStr(2, 118, line);

    snprintf(
        line,
        sizeof(line),
        "R3:%s  R4:%s",
        relayActive[2] ? "ON" : "OFF",
        relayActive[3] ? "ON" : "OFF"
    );

    oled.drawStr(2, 126, line);

    oled.sendBuffer();

    displayDirty = false;
}

// ==================================================
// COMMUNICATION AVEC L'ESCLAVE
// ==================================================
//
// L'esclave envoie 4 octets :
//
// [0] Sequence de detection QR
// [1] Face detectee
// [2] Etat des 4 boutons
// [3] Signature 0xA5
//
// ==================================================

void pollSlave() {

    uint32_t now = millis();

    if (now - lastLinkPoll < LINK_POLL_MS) {
        return;
    }

    lastLinkPoll = now;

    size_t count = Wire1.requestFrom(
        (uint8_t)SLAVE_ADDR,
        (uint8_t)4
    );

    if (count != 4) {

        while (Wire1.available()) {
            Wire1.read();
        }

        // Securite logicielle :
        // relais OFF si communication perdue
        updateRelays(0);

        if (now - lastLinkErrorLog >= 3000) {

            Serial.println(
                "[I2C] Esclave non accessible"
            );

            lastLinkErrorLog = now;
        }

        return;
    }

    uint8_t sequence = Wire1.read();
    uint8_t face = Wire1.read();
    uint8_t buttonMask = Wire1.read();
    uint8_t signature = Wire1.read();

    if (signature != EVENT_SIGNATURE ||
        (buttonMask & 0xF0) != 0) {

        updateRelays(0);

        Serial.println(
            "[I2C] Trame invalide"
        );

        return;
    }

    // ------------------------------------------
    // 1. ACTUALISATION DES RELAIS
    // ------------------------------------------

    updateRelays(buttonMask);

    // ------------------------------------------
    // 2. DETECTION QR3 / QR4
    // ------------------------------------------

    if (sequence == 0) {
        return;
    }

    if (face != AVANT && face != ARRIERE) {
        return;
    }

    if (!hasSlaveSequence ||
        sequence != lastSlaveSequence) {

        lastSlaveSequence = sequence;
        hasSlaveSequence = true;

        qrDetected(face);
    }

    // ------------------------------------------
    // 3. ACQUITTEMENT QR
    // ------------------------------------------

    Wire1.beginTransmission(SLAVE_ADDR);

    Wire1.write(ACK_SIGNATURE);
    Wire1.write(sequence);

    uint8_t status = Wire1.endTransmission();

    if (status != 0) {

        Serial.printf(
            "[I2C] Erreur ACK : %u\n",
            status
        );
    }
}

// ==================================================
// SETUP
// ==================================================

void setup() {

    Serial.begin(115200);
    delay(250);

    Serial.println();
    Serial.println("==============================");
    Serial.println("      PMI BPOST - MAITRE");
    Serial.println("==============================");

    // Relais OFF au demarrage
    for (uint8_t i = 0; i < 4; i++) {

        digitalWrite(RELAY_PINS[i], RELAY_OFF);

        pinMode(RELAY_PINS[i], OUTPUT);

        digitalWrite(RELAY_PINS[i], RELAY_OFF);
    }

    // QR1
    uartQR1.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR1_RX,
        QR1_TX
    );

    // QR2
    uartQR2.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR2_RX,
        QR2_TX
    );

    // OLED
    oled.begin();
    oled.setBusClock(400000);

    // I2C inter ESP32
    if (!Wire1.begin(
        LINK_SDA,
        LINK_SCL,
        100000
    )) {

        Serial.println(
            "[I2C] Erreur initialisation"
        );
    }

    Wire1.setTimeOut(30);

    drawDisplay();

    Serial.println("QR1 et QR2 initialises");
    Serial.println("Relais R1 a R4 initialises");
    Serial.println("Attente de l'esclave...");
}

// ==================================================
// LOOP
// ==================================================

void loop() {

    // Lecteurs locaux
    pollQR(uartQR1, stateQR1, GAUCHE);
    pollQR(uartQR2, stateQR2, DROITE);

    // QR3 / QR4 + boutons de l'esclave
    pollSlave();

    // Affichage
    if (displayDirty) {
        drawDisplay();
    }

    delay(1);
}
