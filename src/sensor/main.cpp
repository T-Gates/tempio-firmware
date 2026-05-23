#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>

// LDR 분압회로 — GPIO2 (ADC)
static constexpr uint8_t LDR_PIN = 2;

SensirionI2cSht4x sht4x;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    sht4x.begin(Wire);
    Serial.println("sensor node ready");
}

void loop() {
    delay(5000);

    float temp, humidity;
    sht4x.measureHighPrecision(temp, humidity);

    int ldr = analogRead(LDR_PIN);

    Serial.printf("Temp: %.1f C  Hum: %.1f %%  LDR: %d\n", temp, humidity, ldr);
}
