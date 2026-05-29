// 허브 메인 — setup/loop 오케스트레이터
//
// 실행 순서: WiFi → BLE → MQTT 초기화 후,
// loop에서 시리얼·BLE·MQTT 폴링 + 리포트 발행 + 펜딩 명령 처리
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "ble/ble_central.h"
#include "net/wifi_manager.h"
#include "net/mqtt_handler.h"
#include "cmd/cmd_dispatcher.h"

void setup() {
    delay(BOOT_DELAY_MS);
    Serial.begin(115200);
    while (!Serial && millis() < SERIAL_TIMEOUT_MS) { delay(10); }

    wifiInit();
    bleCentralInit();
    mqttInit(MQTT_BROKER_URI);
}

void loop() {
    wifiProcessSerial();    // 시리얼로 WiFi 설정 변경 감시
    bleCentralLoop();       // BLE 스캔·연결·정리
    mqttLoop();             // WiFi 상태에 따라 MQTT 시작/중지

    // BLE로 들어온 센서 리포트를 MQTT로 서버에 전달
    SensorReport report;
    while (bleGetPendingReport(&report)) {
        // 허브 자체 상태를 리포트에 주입 (노드는 WiFi/힙 정보를 모름)
        report.wifi_rssi  = WiFi.RSSI();
        report.free_heap  = ESP.getFreeHeap();
        report.uptime_ms  = millis();

        char json[REPORT_JSON_BUF_SIZE];
        report.toJson(json, sizeof(json));
        mqttPublishReport(json);
        Serial.printf(">> MQTT published: %s\n", report.node_id);
    }

    // BLE로 들어온 CMD_ACK를 MQTT로 서버에 전달
    CmdAckReport ack;
    while (bleGetPendingAck(&ack)) {
        char json[128];
        ack.toJson(json, sizeof(json));
        mqttPublishReport(json);
        Serial.printf(">> MQTT ack: node=%s cmd=%u\n", ack.node_id, ack.cmd_id);
    }

    flushAllPending();

    delay(LOOP_DELAY_MS);
}
