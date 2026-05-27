#include <Arduino.h>
#include <ArduinoJson.h>
#include "hub_command.h"
#include "cmd_dispatcher.h"
#include "../ble/ble_central.h"
#include "../net/mqtt_handler.h"

static void handleHubStatus() {
    char json[256];
    JsonDocument doc;
    doc["type"] = "hub_status";
    doc["ble_connected"] = ble_connected_count();
    doc["pending_slots"] = pending_active_slots();
    doc["pending_commands"] = pending_total_commands();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime_ms"] = millis();
    doc["mqtt_connected"] = mqtt_is_connected();
    serializeJson(doc, json, sizeof(json));
    mqtt_publish_report(json);
    Serial.println("<< HUB_STATUS published");
}

void handle_hub_command(const MqttCommand& cmd) {
    if (strcmp(cmd.type, "HUB_STATUS") == 0) handleHubStatus();
    else Serial.printf("<< unknown hub cmd: %s\n", cmd.type);
}
