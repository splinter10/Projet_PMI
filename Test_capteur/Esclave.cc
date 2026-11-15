
#include <Arduino.h>
#include <Wire.h>

// ==========================================
// ESP32-S3 ESCLAVE
// Lecteurs QR AVANT et ARRIERE
// ==========================================

// Liaison I2C
#define I2C_ADRESSE 0x42
#define I2C_SDA 10
#define I2C_SCL 11

// Configuration UART
#define QR_BAUD 9600

// Scanner AVANT
#define QR_AVANT_RX 15
#define QR_AVANT_TX 16

// Scanner ARRIERE
#define QR_ARRIERE_RX 17
#define QR_ARRIERE_TX 18

// Numeros des faces
#define FACE_AVANT 3
#define FACE_ARRIERE 4

// Delai pour considerer une lecture terminee
// si le scanner n'envoie pas CR ou LF
const uint32_t FIN_LECTURE_MS = 80;

// ==========================================
// STRUCTURE DE RECEPTION QR
// ==========================================

struct LecteurQR {
    uint16_t nbCaracteres = 0;
    uint32_t dernierCaractere = 0;
};

LecteurQR qrAvant;
LecteurQR qrArriere;

// ==========================================
// FILE D'ATTENTE DES DETECTIONS
// ==========================================

// Chaque evenement contient :
// numero = identifiant de la lecture
// face = position du scanner

struct EvenementQR {
    uint8_t numero;
    uint8_t face;
};

const uint8_t CAPACITE_FILE = 16;

EvenementQR fileQR[CAPACITE_FILE];

uint8_t teteFile = 0;
uint8_t nombreEvenements = 0;
uint8_t prochainNumero = 0;

// Protection des donnees partagees
// avec les callbacks I2C
portMUX_TYPE verrou = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// AJOUTER UNE DETECTION
// ==========================================

void ajouterDetection(uint8_t face) {
    bool ajoute = false;

    portENTER_CRITICAL(&verrou);

    if (nombreEvenements < CAPACITE_FILE) {

        prochainNumero++;

        // La valeur 0 signifie "aucun evenement"
        if (prochainNumero == 0) {
            prochainNumero = 1;
        }

        uint8_t position =
            (teteFile + nombreEvenements)
            % CAPACITE_FILE;

        fileQR[position].numero = prochainNumero;
        fileQR[position].face = face;

        nombreEvenements++;
        ajoute = true;
    }

    portEXIT_CRITICAL(&verrou);

    if (ajoute) {
        Serial.print("[QR] Detection : ");

        if (face == FACE_AVANT) {
            Serial.println("AVANT");
        } else {
            Serial.println("ARRIERE");
        }
    } else {
        Serial.println(
            "[ERREUR] File QR pleine !"
        );
    }
}

// ==========================================
// CALLBACK : DEMANDE DU MAITRE
// ==========================================

// Trame de 3 octets :
// [0] Numero de l'evenement
// [1] Face detectee
// [2] Signature 0xA5

void onI2CRequest() {
    uint8_t message[3] = {0, 0, 0xA5};

    portENTER_CRITICAL(&verrou);

    if (nombreEvenements > 0) {
        message[0] = fileQR[teteFile].numero;
        message[1] = fileQR[teteFile].face;
    }

    portEXIT_CRITICAL(&verrou);

    Wire.write(message, 3);
}

// ==========================================
// CALLBACK : ACQUITTEMENT DU MAITRE
// ==========================================

// Le maitre envoie :
// [0] Signature 0x5A
// [1] Numero de l'evenement traite

void onI2CReceive(int nbOctets) {

    if (nbOctets < 2) {
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }

    uint8_t signature = Wire.read();
    uint8_t numero = Wire.read();

    // Vider les octets supplementaires
    while (Wire.available()) {
        Wire.read();
    }

    if (signature != 0x5A) {
        return;
    }

    portENTER_CRITICAL(&verrou);

    if (nombreEvenements > 0 &&
        fileQR[teteFile].numero == numero) {

        teteFile =
            (teteFile + 1) % CAPACITE_FILE;

        nombreEvenements--;
    }

    portEXIT_CRITICAL(&verrou);
}

// ==========================================
// FIN D'UNE LECTURE QR
// ==========================================

void terminerLecture(
    LecteurQR &lecteur,
    uint8_t face
) {

    if (lecteur.nbCaracteres == 0) {
        return;
    }

    // Un QR a ete recu :
    // on enregistre uniquement sa face
    ajouterDetection(face);

    lecteur.nbCaracteres = 0;
}

// ==========================================
// LECTURE D'UN SCANNER UART
// ==========================================

void lireScanner(
    HardwareSerial &port,
    LecteurQR &lecteur,
    uint8_t face
) {

    while (port.available() > 0) {

        uint8_t c = port.read();

        lecteur.dernierCaractere = millis();

        // Fin du QR : CR ou LF
        if (c == '\r' || c == '\n') {
            terminerLecture(lecteur, face);
            continue;
        }

        // Compter les caracteres recus
        // sans stocker le contenu du QR
        if (c >= 32 && c <= 126) {
            if (lecteur.nbCaracteres < 65535) {
                lecteur.nbCaracteres++;
            }
        }
    }

    // Secours si le lecteur n'envoie
    // pas de caractere de fin de ligne
    if (lecteur.nbCaracteres > 0 &&
        millis() - lecteur.dernierCaractere
        >= FIN_LECTURE_MS) {

        terminerLecture(lecteur, face);
    }
}

// ==========================================
// SETUP
// ==========================================

void setup() {

    // Diagnostic USB vers le PC
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=======================");
    Serial.println("ESP32-S3 ESCLAVE");
    Serial.println("=======================");

    // UART1 : QR AVANT
    Serial1.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR_AVANT_RX,
        QR_AVANT_TX
    );

    // UART2 : QR ARRIERE
    Serial2.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR_ARRIERE_RX,
        QR_ARRIERE_TX
    );

    // Callbacks I2C
    Wire.onRequest(onI2CRequest);
    Wire.onReceive(onI2CReceive);

    // Demarrage I2C en mode esclave
    bool resultat = Wire.begin(
        (uint8_t)I2C_ADRESSE,
        I2C_SDA,
        I2C_SCL,
        100000
    );

    if (!resultat) {
        Serial.println(
            "[ERREUR] Demarrage I2C"
        );

        while (true) {
            delay(1000);
        }
    }

    Serial.println("[OK] UART AVANT");
    Serial.println("[OK] UART ARRIERE");
    Serial.println("[OK] I2C ADRESSE 0x42");
    Serial.println("En attente de QR...");
}

// ==========================================
// LOOP
// ==========================================

void loop() {

    lireScanner(
        Serial1,
        qrAvant,
        FACE_AVANT
    );

    lireScanner(
        Serial2,
        qrArriere,
        FACE_ARRIERE
    );

    delay(2);
}
