
#include <Arduino.h>

#define QR_BAUD 9600

// Premier lecteur QR
#define QR1_RX 15
#define QR1_TX 16

// Deuxieme lecteur QR
#define QR2_RX 17
#define QR2_TX 18

void setup() {
    Serial.begin(115200);

    Serial1.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR1_RX,
        QR1_TX
    );

    Serial2.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR2_RX,
        QR2_TX
    );

    delay(1000);

    Serial.println("========================");
    Serial.println("TEST LECTEURS QR GM865");
    Serial.println("========================");
}

void loop() {

    // Lecture du GM865 n°1
    while (Serial1.available()) {

        uint8_t caractere = Serial1.read();

        Serial.printf(
            "QR1 : 0x%02X",
            caractere
        );

        if (caractere >= 32 && caractere <= 126) {
            Serial.printf(" (%c)", caractere);
        }

        Serial.println();
    }

    // Lecture du GM865 n°2
    while (Serial2.available()) {

        uint8_t caractere = Serial2.read();

        Serial.printf(
            "QR2 : 0x%02X",
            caractere
        );

        if (caractere >= 32 && caractere <= 126) {
            Serial.printf(" (%c)", caractere);
        }

        Serial.println();
    }
}
