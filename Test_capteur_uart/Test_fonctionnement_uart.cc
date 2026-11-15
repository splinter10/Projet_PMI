
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("ESP32-S3 DEMARRE !");
}

void loop() {
    Serial.println("ESP32-S3 fonctionne");
    delay(1000);
}
