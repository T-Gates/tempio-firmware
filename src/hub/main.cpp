#include <Arduino.h>

// 최소 테스트: LED 깜빡임 + 시리얼 출력
// BLE 문제인지 시리얼 문제인지 분리

static constexpr uint8_t LED_PIN = 8;
static int count = 0;

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("=== HELLO FROM C3 ===");

    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_PIN, LOW);  // LED ON
    Serial.printf("blink %d\n", count++);
    delay(500);

    digitalWrite(LED_PIN, HIGH); // LED OFF
    delay(500);
}
