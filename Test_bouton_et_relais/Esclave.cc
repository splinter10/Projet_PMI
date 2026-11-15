
#include <Arduino.h>
#include <Wire.h>

// ========================================
// CONFIGURATION I2C
// ========================================

#define I2C_ADRESSE 0x42
#define I2C_SDA 10
#define I2C_SCL 11

// ========================================
// CONFIGURATION BOUTONS
// ========================================

const uint8_t BOUTONS[4] = {4, 5, 6, 7};

const unsigned long ANTIREBOND_MS = 15;

// États des boutons
bool etatStable[4] = {false, false, false, false};
bool derniereLecture[4] = {false, false, false, false};

unsigned long dernierChangement[4] = {0, 0, 0, 0};

// État transmis au maître
volatile uint8_t etatBoutons = 0;

// ========================================
// ENVOI DES BOUTONS AU MAITRE
// ========================================

void envoyerBoutons() {

    Wire.write((uint8_t)etatBoutons);
}

// ========================================
// LECTURE DES BOUTONS
// ========================================

void lireBoutons() {

    uint8_t nouvelEtat = 0;

    for (int i = 0; i < 4; i++) {

        bool appuye = digitalRead(BOUTONS[i]) == LOW;

        // Détection d'un changement
        if (appuye != derniereLecture[i]) {

            derniereLecture[i] = appuye;
            dernierChangement[i] = millis();
        }

        // Antirebond
        if (millis() - dernierChangement[i] >= ANTIREBOND_MS) {

            etatStable[i] = appuye;
        }

        // Construction de l'octet
        if (etatStable[i]) {

            nouvelEtat |= (1 << i);
        }
    }

    etatBoutons = nouvelEtat;
}

// ========================================
// SETUP
// ========================================

void setup() {

    Serial.begin(115200);

    // Configuration des boutons
    for (int i = 0; i < 4; i++) {

        pinMode(BOUTONS[i], INPUT_PULLUP);
    }

    // Callback de réponse I2C
    Wire.onRequest(envoyerBoutons);

    // Initialisation I2C en esclave
    if (!Wire.begin(
        (uint8_t)I2C_ADRESSE,
        I2C_SDA,
        I2C_SCL,
        100000
    )) {

        Serial.println("ERREUR I2C");

        while (true) {
            delay(1000);
        }
    }

    Serial.println("ESP32 ESCLAVE PRET");
}

// ========================================
// LOOP
// ========================================

void loop() {

    lireBoutons();

    delay(2);
}
