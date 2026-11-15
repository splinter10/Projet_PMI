
#include <Arduino.h>
#include <Wire.h>

// ==================================================
// PMI BPOST - ESP32-S3 ESCLAVE
//
// QR3 : AVANT
// QR4 : ARRIERE
//
// Boutons B1 a B4
// Transmission des QR et des boutons par I2C
// ==================================================

// ---------- LECTEURS QR ----------

#define QR3_RX 15
#define QR3_TX 16

#define QR4_RX 17
#define QR4_TX 18

#define QR_BAUD 9600

HardwareSerial uartQR3(1);
HardwareSerial uartQR4(2);

// ---------- I2C ----------

#define LINK_SDA 10
#define LINK_SCL 11

#define SLAVE_ADDR 0x42
#define EVENT_SIGNATURE 0xA5
#define ACK_SIGNATURE 0x5A

// ---------- BOUTONS ----------

const uint8_t BUTTON_PINS[4] = {
    4, 5, 6, 7
};

const uint32_t DEBOUNCE_MS = 35;

struct ButtonState {
    bool rawPressed = false;
    bool stablePressed = false;
    bool armed = true;
    uint32_t lastChange = 0;
};

ButtonState buttons[4];

// 4 bits de commande
volatile uint8_t buttonMask = 0;

// ---------- FACES ----------

#define AVANT 3
#define ARRIERE 4

// ---------- ETATS QR ----------

struct QRState {
    bool receiving = false;
    bool detectedOnce = false;
    uint32_t lastByteMs = 0;
    uint32_t lastDetectionMs = 0;
};

QRState stateQR3;
QRState stateQR4;

const uint32_t QR_GAP_MS = 80;
const uint32_t QR_LOCKOUT_MS = 1000;

// ---------- FILE D'EVENEMENTS QR ----------

struct Event {
    uint8_t sequence;
    uint8_t face;
};

const uint8_t QUEUE_SIZE = 16;

Event queueEvents[QUEUE_SIZE];

uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

uint8_t nextSequence = 0;

portMUX_TYPE dataMux =
    portMUX_INITIALIZER_UNLOCKED;

// ==================================================
// LECTURE DES QUATRE BOUTONS
// ==================================================

void readButtons() {

    uint32_t now = millis();
    uint8_t newMask = 0;

    for (uint8_t i = 0; i < 4; i++) {

        ButtonState &b = buttons[i];

        bool pressed =
            digitalRead(BUTTON_PINS[i]) == LOW;

        // Detection d'un changement
        if (pressed != b.rawPressed) {

            b.rawPressed = pressed;
            b.lastChange = now;
        }

        // Anti-rebond
        if (now - b.lastChange >= DEBOUNCE_MS) {

            if (b.stablePressed != b.rawPressed) {

                b.stablePressed = b.rawPressed;

                if (!b.stablePressed) {
                    b.armed = true;
                }

                Serial.printf(
                    "[BOUTON B%u] %s\n",
                    i + 1,
                    b.stablePressed
                        ? "APPUYE"
                        : "RELACHE"
                );
            }
        }

        // Protection au demarrage :
        // si bouton maintenu pendant le boot,
        // il faut le relacher avant utilisation.

        if (b.stablePressed && b.armed) {

            newMask |= (1 << i);
        }
    }

    // Actualisation de l'etat transmis
    portENTER_CRITICAL(&dataMux);

    buttonMask = newMask;

    portEXIT_CRITICAL(&dataMux);
}

// ==================================================
// AJOUT D'UNE DETECTION QR
// ==================================================

bool addEvent(uint8_t face) {

    bool accepted = false;

    portENTER_CRITICAL(&dataMux);

    if (queueCount < QUEUE_SIZE) {

        nextSequence++;

        if (nextSequence == 0) {
            nextSequence++;
        }

        queueEvents[queueTail] = {
            nextSequence,
            face
        };

        queueTail =
            (queueTail + 1) % QUEUE_SIZE;

        queueCount++;
        accepted = true;
    }

    portEXIT_CRITICAL(&dataMux);

    return accepted;
}

// ==================================================
// DETECTION QR
// ==================================================

void qrDetected(uint8_t face) {

    Serial.printf(
        "[QR%u] DETECTE - FACE %s\n",
        face,
        face == AVANT ? "AVANT" : "ARRIERE"
    );

    if (!addEvent(face)) {

        Serial.println(
            "[I2C] File pleine - detection perdue"
        );
    }
}

// ==================================================
// FIN DE LECTURE QR
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
// LECTURE UART
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
// DEMANDE DU MAITRE
// ==================================================
//
// Paquet de 4 octets :
//
// [0] Sequence QR
// [1] Face QR
// [2] Etat des boutons
// [3] Signature
//
// ==================================================

void onMasterRequest() {

    uint8_t packet[4] = {
        0,
        0,
        0,
        EVENT_SIGNATURE
    };

    portENTER_CRITICAL(&dataMux);

    // Recuperation des boutons
    packet[2] = buttonMask;

    // Recuperation de la detection QR
    if (queueCount > 0) {

        packet[0] =
            queueEvents[queueHead].sequence;

        packet[1] =
            queueEvents[queueHead].face;
    }

    portEXIT_CRITICAL(&dataMux);

    Wire1.write(packet, sizeof(packet));
}

// ==================================================
// ACQUITTEMENT QR
// ==================================================

void onMasterReceive(int count) {

    if (count < 2) {

        while (Wire1.available()) {
            Wire1.read();
        }

        return;
    }

    int marker = Wire1.read();
    int sequence = Wire1.read();

    while (Wire1.available()) {
        Wire1.read();
    }

    if (marker != ACK_SIGNATURE ||
        sequence < 1) {
        return;
    }

    portENTER_CRITICAL(&dataMux);

    if (queueCount > 0 &&
        queueEvents[queueHead].sequence ==
        sequence) {

        queueHead =
            (queueHead + 1) % QUEUE_SIZE;

        queueCount--;
    }

    portEXIT_CRITICAL(&dataMux);
}

// ==================================================
// SETUP
// ==================================================

void setup() {

    Serial.begin(115200);
    delay(250);

    Serial.println();
    Serial.println("==============================");
    Serial.println("     PMI BPOST - ESCLAVE");
    Serial.println("==============================");

    // Initialisation des boutons
    for (uint8_t i = 0; i < 4; i++) {

        pinMode(BUTTON_PINS[i], INPUT_PULLUP);

        bool pressed =
            digitalRead(BUTTON_PINS[i]) == LOW;

        buttons[i].rawPressed = pressed;
        buttons[i].stablePressed = pressed;
        buttons[i].armed = !pressed;
        buttons[i].lastChange = millis();
    }

    // QR3
    uartQR3.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR3_RX,
        QR3_TX
    );

    // QR4
    uartQR4.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR4_RX,
        QR4_TX
    );

    // I2C
    Wire1.onRequest(onMasterRequest);
    Wire1.onReceive(onMasterReceive);

    if (!Wire1.begin(
        SLAVE_ADDR,
        LINK_SDA,
        LINK_SCL,
        100000
    )) {

        Serial.println(
            "[I2C] Erreur initialisation"
        );

    } else {

        Serial.println(
            "[I2C] Esclave pret - adresse 0x42"
        );
    }

    Serial.println("QR3 et QR4 initialises");
    Serial.println("Boutons B1 a B4 initialises");
    Serial.println("Systeme pret !");
}

// ==================================================
// LOOP
// ==================================================

void loop() {

    // Lecture des boutons
    readButtons();

    // Detection QR3 / QR4
    pollQR(uartQR3, stateQR3, AVANT);
    pollQR(uartQR4, stateQR4, ARRIERE);

    delay(1);
}
