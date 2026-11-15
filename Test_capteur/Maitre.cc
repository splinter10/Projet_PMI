
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ==========================================
// ESP32-S3 MAITRE
// 4 GM67 + OLED SH1107 128x128
// ==========================================

// ==========================================
// OLED : BUS I2C N°0
// ==========================================

#define OLED_SDA 8
#define OLED_SCL 9

U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE
);

// ==========================================
// ESP32 ESCLAVE : BUS I2C N°1
// ==========================================

TwoWire I2C_Esclave(1);

#define ESP_SDA 10
#define ESP_SCL 11
#define ESP_ADRESSE 0x42

// ==========================================
// SCANNERS LOCAUX
// ==========================================

#define QR_BAUD 9600

// QR GAUCHE
#define QR_GAUCHE_RX 15
#define QR_GAUCHE_TX 16

// QR DROITE
#define QR_DROITE_RX 17
#define QR_DROITE_TX 18

// ==========================================
// IDENTIFIANTS DES FACES
// ==========================================

enum FaceQR : uint8_t {
    AUCUNE = 0,
    GAUCHE = 1,
    DROITE = 2,
    AVANT = 3,
    ARRIERE = 4
};

// ==========================================
// VARIABLES
// ==========================================

// Derniere face ayant detecte un QR
FaceQR faceDetectee = AUCUNE;

// Etat de la liaison I2C
bool esclaveConnecte = false;

// Numero du dernier evenement distant
uint8_t dernierNumeroEsclave = 0;

// Actualisation OLED
bool miseAJourEcran = true;

// Temporisations
uint32_t dernierI2C = 0;
uint32_t dernierOLED = 0;

const uint32_t INTERVALLE_I2C = 20;
const uint32_t INTERVALLE_OLED = 100;
const uint32_t FIN_LECTURE_MS = 80;

// ==========================================
// STRUCTURE DE RECEPTION QR
// ==========================================

struct LecteurQR {
    uint16_t nbCaracteres = 0;
    uint32_t dernierCaractere = 0;
};

LecteurQR qrGauche;
LecteurQR qrDroite;

// ==========================================
// NOM D'UNE FACE
// ==========================================

const char* nomFace(FaceQR face) {

    switch (face) {

        case GAUCHE:
            return "GAUCHE";

        case DROITE:
            return "DROITE";

        case AVANT:
            return "AVANT";

        case ARRIERE:
            return "ARRIERE";

        default:
            return "AUCUNE";
    }
}

// ==========================================
// ENREGISTRER UNE DETECTION
// ==========================================

void nouvelleDetection(FaceQR face) {

    faceDetectee = face;

    // Demander une actualisation OLED
    miseAJourEcran = true;

    // Diagnostic PC
    Serial.print("[QR DETECTE] ");
    Serial.println(nomFace(face));
}

// ==========================================
// FIN D'UNE LECTURE UART
// ==========================================

void terminerLecture(
    LecteurQR &lecteur,
    FaceQR face
) {

    if (lecteur.nbCaracteres == 0) {
        return;
    }

    nouvelleDetection(face);

    lecteur.nbCaracteres = 0;
}

// ==========================================
// LECTURE D'UN GM67
// ==========================================

void lireScanner(
    HardwareSerial &port,
    LecteurQR &lecteur,
    FaceQR face
) {

    while (port.available() > 0) {

        uint8_t c = port.read();

        lecteur.dernierCaractere = millis();

        // Dans notre test, le GM67
        // termine les trames par 0x0D
        if (c == '\r' || c == '\n') {
            terminerLecture(lecteur, face);
            continue;
        }

        // Compter uniquement les
        // caracteres imprimables
        if (c >= 32 && c <= 126) {

            if (lecteur.nbCaracteres < 65535) {
                lecteur.nbCaracteres++;
            }
        }
    }

    // Fin alternative sans CR/LF
    if (lecteur.nbCaracteres > 0 &&
        millis() - lecteur.dernierCaractere
        >= FIN_LECTURE_MS) {

        terminerLecture(lecteur, face);
    }
}

// ==========================================
// LECTURE DES DETECTIONS ESCLAVES
// ==========================================

void lireEsclave() {

    uint8_t message[3] = {0};

    int nb = I2C_Esclave.requestFrom(
        (uint8_t)ESP_ADRESSE,
        (uint8_t)3
    );

    if (nb != 3) {

        esclaveConnecte = false;

        // Vider les donnees eventuelles
        while (I2C_Esclave.available()) {
            I2C_Esclave.read();
        }

        return;
    }

    for (int i = 0; i < 3; i++) {
        message[i] = I2C_Esclave.read();
    }

    uint8_t numero = message[0];
    uint8_t face = message[1];
    uint8_t signature = message[2];

    // Verifier la trame
    if (signature != 0xA5 ||
        (face != AUCUNE &&
         face != AVANT &&
         face != ARRIERE)) {

        esclaveConnecte = false;
        return;
    }

    esclaveConnecte = true;

    // Pas de QR en attente
    if (numero == 0 || face == AUCUNE) {
        return;
    }

    // Nouvelle detection
    if (numero != dernierNumeroEsclave) {

        dernierNumeroEsclave = numero;

        nouvelleDetection((FaceQR)face);
    }

    // Envoyer un acquittement a l'esclave
    I2C_Esclave.beginTransmission(
        (uint8_t)ESP_ADRESSE
    );

    I2C_Esclave.write((uint8_t)0x5A);
    I2C_Esclave.write(numero);

    uint8_t erreur =
        I2C_Esclave.endTransmission();

    if (erreur != 0) {
        esclaveConnecte = false;
        Serial.println(
            "[I2C] Echec acquittement"
        );
    }
}

// ==========================================
// DESSINER UNE FACE SUR L'OLED
// ==========================================

void dessinerFace(
    int x,
    int y,
    int largeur,
    int hauteur,
    const char* texte,
    FaceQR face
) {

    bool active = (faceDetectee == face);

    // Face selectionnee : fond blanc
    if (active) {
        oled.setDrawColor(1);
        oled.drawBox(
            x, y, largeur, hauteur
        );

        // Ecriture noire
        oled.setDrawColor(0);

    } else {

        // Face non selectionnee :
        // cadre blanc et fond noir
        oled.setDrawColor(1);
        oled.drawFrame(
            x, y, largeur, hauteur
        );
    }

    oled.setFont(u8g2_font_5x8_tr);

    int largeurTexte =
        oled.getStrWidth(texte);

    int texteX =
        x + (largeur - largeurTexte) / 2;

    int texteY =
        y + (hauteur + 7) / 2;

    oled.drawStr(
        texteX,
        texteY,
        texte
    );

    // Retablir la couleur blanche
    oled.setDrawColor(1);
}

// ==========================================
// AFFICHAGE OLED
// ==========================================

void afficherOLED() {

    oled.clearBuffer();

    // --------------------------------------
    // TITRE
    // --------------------------------------

    oled.setFont(u8g2_font_6x12_tr);

    const char* titre = "POSITION QR";

    int titreX =
        (128 - oled.getStrWidth(titre)) / 2;

    oled.drawStr(titreX, 13, titre);

    oled.drawLine(
        4, 19,
        123, 19
    );

    // --------------------------------------
    // COLIS CENTRAL
    // --------------------------------------

    oled.drawFrame(
        40, 49,
        48, 38
    );

    oled.setFont(u8g2_font_6x12_tr);

    oled.drawStr(
        49, 70,
        "COLIS"
    );

    // --------------------------------------
    // LIGNES DE LIAISON
    // --------------------------------------

    // Haut : 5 pixels d'ecart
    oled.drawLine(
        64, 44,
        64, 48
    );

    // Bas : 5 pixels d'ecart
    // Modification demandee
    oled.drawLine(
        64, 87,
        64, 91
    );

    // Cote gauche
    oled.drawLine(
        37, 67,
        39, 67
    );

    // Cote droit
    oled.drawLine(
        88, 67,
        90, 67
    );

    // --------------------------------------
    // FACE AVANT
    // --------------------------------------

    dessinerFace(
        44, 29,
        40, 15,
        "AVANT",
        AVANT
    );

    // --------------------------------------
    // FACE ARRIERE
    // --------------------------------------
    // Ancienne position : Y = 103
    // Nouvelle position : Y = 92

    dessinerFace(
        34, 92,
        60, 15,
        "ARRIERE",
        ARRIERE
    );

    // --------------------------------------
    // FACE GAUCHE
    // --------------------------------------

    dessinerFace(
        2, 60,
        35, 15,
        "GCH",
        GAUCHE
    );

    // --------------------------------------
    // FACE DROITE
    // --------------------------------------

    dessinerFace(
        91, 60,
        35, 15,
        "DRT",
        DROITE
    );

    // --------------------------------------
    // ETAT DE LA LIAISON I2C
    // --------------------------------------

    oled.setFont(u8g2_font_4x6_tr);

    if (esclaveConnecte) {

        oled.drawStr(
            48, 126,
            "I2C OK"
        );

    } else {

        oled.drawStr(
            38, 126,
            "I2C PERDUE"
        );
    }

    oled.sendBuffer();
}

// ==========================================
// SETUP
// ==========================================

void setup() {

    // Diagnostic USB
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=======================");
    Serial.println("ESP32-S3 MAITRE");
    Serial.println("=======================");

    // --------------------------------------
    // INITIALISATION OLED
    // --------------------------------------

    Wire.begin(
        OLED_SDA,
        OLED_SCL,
        100000
    );

    oled.begin();

    Serial.println("[OK] OLED SH1107");

    // --------------------------------------
    // INITIALISATION I2C ESCLAVE
    // --------------------------------------

    bool resultat = I2C_Esclave.begin(
        ESP_SDA,
        ESP_SCL,
        100000
    );

    if (!resultat) {

        Serial.println(
            "[ERREUR] I2C MAITRE"
        );

        while (true) {
            delay(1000);
        }
    }

    I2C_Esclave.setTimeOut(20);

    Serial.println(
        "[OK] BUS I2C ESCLAVE"
    );

    // --------------------------------------
    // INITIALISATION GM67
    // --------------------------------------

    // UART1 : QR GAUCHE
    Serial1.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR_GAUCHE_RX,
        QR_GAUCHE_TX
    );

    // UART2 : QR DROITE
    Serial2.begin(
        QR_BAUD,
        SERIAL_8N1,
        QR_DROITE_RX,
        QR_DROITE_TX
    );

    Serial.println(
        "[OK] QR GAUCHE"
    );

    Serial.println(
        "[OK] QR DROITE"
    );

    // --------------------------------------
    // ETAT INITIAL
    // --------------------------------------

    faceDetectee = AUCUNE;
    esclaveConnecte = false;

    afficherOLED();

    Serial.println(
        "Systeme pret !"
    );
}

// ==========================================
// LOOP
// ==========================================

void loop() {

    uint32_t maintenant = millis();

    // --------------------------------------
    // SCANNER GAUCHE
    // --------------------------------------

    lireScanner(
        Serial1,
        qrGauche,
        GAUCHE
    );

    // --------------------------------------
    // SCANNER DROITE
    // --------------------------------------

    lireScanner(
        Serial2,
        qrDroite,
        DROITE
    );

    // --------------------------------------
    // COMMUNICATION AVEC L'ESCLAVE
    // --------------------------------------

    if (
        maintenant - dernierI2C
        >= INTERVALLE_I2C
    ) {

        dernierI2C = maintenant;

        bool ancienEtat = esclaveConnecte;

        lireEsclave();

        if (ancienEtat != esclaveConnecte) {
            miseAJourEcran = true;
        }
    }

    // --------------------------------------
    // ACTUALISATION DE L'OLED
    // --------------------------------------

    if (
        miseAJourEcran ||
        maintenant - dernierOLED
        >= INTERVALLE_OLED
    ) {

        dernierOLED = maintenant;

        afficherOLED();

        miseAJourEcran = false;
    }

    delay(2);
}
