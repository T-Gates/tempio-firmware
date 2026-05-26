#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ble/ble_central.h"
#include "net/wifi_manager.h"
#include "net/mqtt_client.h"

void setup() {
    delay(3000);
    Serial.begin(115200);
    while (!Serial && millis() < 6000) { delay(10); }

    wifi_init();
    ble_central_init();
    mqtt_init(MQTT_BROKER_IP, MQTT_BROKER_PORT);
}

void loop() {
    wifi_process_serial();
    ble_central_loop();
    mqtt_loop();

    // BLE에서 새 센서 데이터가 있으면 MQTT로 publish
    SensorReport report;
    if (ble_get_pending_report(&report)) {
        // JSON 직렬화
        JsonDocument doc;
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["uptime_ms"] = millis();

        JsonArray devices = doc["connected_devices"].to<JsonArray>();
        JsonObject dev = devices.add<JsonObject>();
        dev["node_id"] = report.node_id;
        dev["node_type"] = report.node_type_str;
        dev["battery_voltage"] = report.battery_mv / 1000.0f;
        dev["rssi"] = report.ble_rssi;

        JsonArray readings = doc["sensor_readings"].to<JsonArray>();
        JsonObject reading = readings.add<JsonObject>();
        reading["node_id"] = report.node_id;
        reading["temperature"] = serialized(String(report.temperature, 1));
        reading["humidity"] = serialized(String(report.humidity, 1));
        if (report.ldr > 0) {
            reading["lux"] = report.ldr;
        }

        char json[512];
        serializeJson(doc, json, sizeof(json));
        mqtt_publish_report(json);

        Serial.printf(">> MQTT published: %s\n", report.node_id);
    }

    delay(LOOP_DELAY_MS);
}
