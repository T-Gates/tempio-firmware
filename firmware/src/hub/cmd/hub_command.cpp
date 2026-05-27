#include <Arduino.h>
#include <ArduinoJson.h>
#include "hub_command.h"
#include "cmd_dispatcher.h"
#include "../ble/ble_central.h"

// 허브 상태를 JSON으로 직렬화. 버퍼에 쓰고 길이 리턴.
static int buildHubStatus(char* buf, size_t bufSize) {
    JsonDocument doc;
    doc["type"] = "hub_status";
    doc["ble_connected"] = ble_connected_count();
    doc["pending_slots"] = pending_active_slots();
    doc["pending_commands"] = pending_total_commands();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime_ms"] = millis();
    return serializeJson(doc, buf, bufSize);
}

// 허브 자체 명령 라우터 — 결과 JSON을 buf에 쓰고 길이 리턴. 알 수 없는 명령이면 0.
int handle_hub_command(const MqttCommand& cmd, char* buf, size_t bufSize) {
    if (strcmp(cmd.type, "HUB_STATUS") == 0) return buildHubStatus(buf, bufSize);
    Serial.printf("<< unknown hub cmd: %s\n", cmd.type);
    return 0;
}
