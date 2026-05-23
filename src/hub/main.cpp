#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>

SensirionI2cScd4x scd4x;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    scd4x.begin(Wire);
    scd4x.stopPeriodicMeasurement();
    scd4x.startPeriodicMeasurement();
    Serial.println("hub ready");
}

void loop() {
    delay(5000);

    bool isReady = false;
    scd4x.getDataReadyStatus(isReady);
    if (!isReady) return;

    float temp, humidity;
    uint16_t co2;
    scd4x.readMeasurement(co2, temp, humidity);

    Serial.printf("CO2: %u ppm  Temp: %.1f C  Hum: %.1f %%\n", co2, temp, humidity);
}
