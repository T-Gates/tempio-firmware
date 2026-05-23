#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// IR LED — 2N2222 베이스를 GPIO3으로 구동
static constexpr uint8_t IR_PIN = 3;

IRsend irsend(IR_PIN);

void setup() {
    Serial.begin(115200);
    irsend.begin();
    Serial.println("ir node ready");
}

void loop() {
    // TODO: BLE로 명령 수신 후 IR 송신
}
