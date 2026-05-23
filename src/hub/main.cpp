// Arduino 프레임워크의 핵심 헤더. digitalWrite, Serial, delay 등 모든 기본 함수 포함
#include <Arduino.h>
// BLE Central(허브) 기능을 모아둔 우리 파일. init/loop/is_connected 함수 선언
#include "ble_central.h"

// BLE 초기화(NimBLEDevice::init)가 실행되면 USB CDC(시리얼 통신)가 잠깐 끊김.
// 그래서 setup() 안에서 Serial.println() 해도 PC 시리얼 모니터에 안 보임.
void setup() {
    // 3초 대기. 보드에 전원이 들어온 뒤 USB 연결이 안정될 시간을 줌
    delay(3000);
    // 시리얼 통신 시작. 115200 = 초당 115200비트 속도 (baud rate)
    // PC의 시리얼 모니터도 같은 속도로 맞춰야 글자가 깨지지 않음
    Serial.begin(115200);
    // Serial이 준비될 때까지 기다림. USB CDC는 PC가 포트를 열어야 준비 완료됨.
    // 준비 안 됐으면 10ms씩 쉬면서 계속 확인
    while (!Serial) { delay(10); }

    // BLE Central 모드 초기화 — 스캔 시작, 콜백 등록 등
    ble_central_init();
}

// loop()는 Arduino가 무한 반복 호출하는 함수. while(true)와 같음
void loop() {
    // BLE Central의 매 프레임 처리 — 연결 시도, 상태 출력 등
    ble_central_loop();
    // 100ms 쉬기. CPU를 100% 쓰지 않도록 약간의 여유를 줌
    delay(100);
}
