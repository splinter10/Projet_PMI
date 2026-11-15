
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ========================================
// CONFIGURATION I2C
// ========================================

// OLED : bus I2C n°0
#define OLED_SDA 8
#define OLED_SCL 9

// ESP32 esclave : bus I2C n°1
#define ESP_SDA 10
#define ESP_SCL 11
#define ESP_ADRESSE 0x42

TwoWire I2C_Esclave = TwoWire(1);

// Même écran que dans ton ancien projet
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE
);

// ========================================
// CONFIGURATION RELAIS
// ========================================

const uint8_t RELAIS_PINS[4] = {4, 5, 6, 7};

// Relais actif à LOW
const uint8_t RELAIS_ON  = LOW;
const uint8_t RELAIS_OFF = HIGH;

// ========================================
// VARIABLES
// ========================================

uint8_t etatBoutons = 0;
bool communicationOK = false;

unsigned long derniereInterrogation = 0;
unsigned long dernierAffichage = 0;

const unsigned long INTERVALLE_I2C = 20;
const unsigned long INTERVALLE_OLED = 100;

// ========================================
// COMMANDE DES RELAIS
// ========================================

void arreterRelais() {
    etatBoutons = 0;

    for (int i = 0; i < 4; i++) {
        digitalWrite(RELAIS_PINS[i], RELAIS_OFF);
    }
}

void mettreAJourRelais(uint8_t etats) {
    etatBoutons = etats & 0x0F;

    for (int i = 0; i < 4; i++) {
        bool appuye = etatBoutons & (1 << i);

        digitalWrite(
            RELAIS_PINS[i],
            appuye ? RELAIS_ON : RELAIS_OFF
        );
    }
}

// ========================================
// COMMUNICATION AVEC L'ESCLAVE
// ========================================

void lireBoutonsEsclave() {

    int nb = I2C_Esclave.requestFrom(
        (uint8_t)ESP_ADRESSE,
        (uint8_t)1
    );

    if (nb == 1 && I2C_Esclave.available()) {

        uint8_t donnees = I2C_Esclave.read();

        // Vérification : seuls 4 bits sont autorisés
        if ((donnees & 0xF0) == 0) {
            communicationOK = true;
            mettreAJourRelais(donnees);
            return;
        }
    }

    // Erreur I2C : désactivation des relais
    communicationOK = false;
    arreterRelais();

    while (I2C_Esclave.available()) {
        I2C_Esclave.read();
    }
}

// ========================================
// AFFICHAGE OLED
// ========================================

void afficherTexteCentre(const char* texte, int y) {
    int largeur = oled.getStrWidth(texte);
    int x = (128 - largeur) / 2;

    if (x < 0) x = 0;

    oled.drawStr(x, y, texte);
}

void afficherOLED() {

    oled.clearBuffer();

    // Titre
    oled.setFont(u8g2_font_6x12_tr);
    afficherTexteCentre("COMMANDE VERINS", 13);

    oled.drawLine(0, 18, 127, 18);

    // Affichage des 4 boutons et relais
    for (int i = 0; i < 4; i++) {

        int y = 38 + i * 20;

        bool actif = etatBoutons & (1 << (3 - i));

        char texte[20];

        snprintf(
            texte,
            sizeof(texte),
            "B%d / R%d",
            i + 1,
            i + 1
        );

        oled.setFont(u8g2_font_6x12_tr);
        oled.drawStr(4, y, texte);

        // Indicateur d'état
        if (actif) {
            oled.drawBox(91, y - 11, 33, 14);

            oled.setDrawColor(0);
            oled.drawStr(99, y, "ON");
            oled.setDrawColor(1);
        }
        else {
            oled.drawFrame(91, y - 11, 33, 14);
            oled.drawStr(98, y, "OFF");
        }
    }

    // Statut de communication
    oled.drawLine(0, 111, 127, 111);

    oled.setFont(u8g2_font_5x8_tr);

    if (communicationOK) {
        afficherTexteCentre("I2C : CONNECTE", 124);
    }
    else {
        afficherTexteCentre("I2C : PERDUE", 124);
    }

    oled.sendBuffer();
}

// ========================================
// SETUP
// ========================================

void setup() {

    Serial.begin(115200);

    // Désactivation des relais au démarrage
    for (int i = 0; i < 4; i++) {
        digitalWrite(RELAIS_PINS[i], RELAIS_OFF);
        pinMode(RELAIS_PINS[i], OUTPUT);
    }

    // OLED sur I2C n°0
    Wire.begin(OLED_SDA, OLED_SCL, 100000);

    oled.begin();
    oled.clearBuffer();

    // Communication avec ESP32 esclave
    if (!I2C_Esclave.begin(
        ESP_SDA,
        ESP_SCL,
        100000
    )) {
        Serial.println("Erreur I2C esclave");
        afficherOLED();

        while (true) {
            arreterRelais();
            delay(100);
        }
    }

    I2C_Esclave.setTimeOut(20);

    communicationOK = false;
    arreterRelais();

    afficherOLED();

    Serial.println("ESP32 MAITRE PRET");
}

// ========================================
// LOOP
// ========================================

void loop() {

    unsigned long maintenant = millis();

    // Lecture des boutons
    if (maintenant - derniereInterrogation >= INTERVALLE_I2C) {

        derniereInterrogation = maintenant;

        lireBoutonsEsclave();
    }

    // Actualisation écran
    if (maintenant - dernierAffichage >= INTERVALLE_OLED) {

        dernierAffichage = maintenant;

        afficherOLED();
    }
}
