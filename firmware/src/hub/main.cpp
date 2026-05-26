#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ble/ble_central.h"
#include "net/wifi_manager.h"
#include "net/mqtt_client.h"
#include <protocol.h>

void setup() {
    delay(3000);
    Serial.begin(115200);
    while (!Serial && millis() < 6000) { delay(10); }

    wifi_init();
    ble_central_init();
    mqtt_init(MQTT_BROKER_URI);
}

void loop() {
    wifi_process_serial();
    ble_central_loop();
    mqtt_loop();

    SensorReport report;
    if (ble_get_pending_report(&report)) {
        char json[512];
        report.toJson(json, sizeof(json));
        mqtt_publish_report(json);
        Serial.printf(">> MQTT published: %s\n", report.node_id);
    }

    MqttCommand cmd;
    while (mqtt_get_command(&cmd)) {
        if (strcmp(cmd.type, "SET_INTERVAL") == 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, cmd.payload)) {
                SetInterval pkt;
                pkt.interval_sec = doc["interval_sec"] | 60;
                bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
                Serial.printf("<< SET_INTERVAL → %s : %us (%s)\n",
                              cmd.target, pkt.interval_sec, ok ? "ok" : "fail");
            }
        } else if (strcmp(cmd.type, "RESET_NODE") == 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, cmd.payload)) {
                ResetNode pkt;
                pkt.level = doc["level"] | 0;
                bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
                Serial.printf("<< RESET_NODE → %s : level=%u (%s)\n",
                              cmd.target, pkt.level, ok ? "ok" : "fail");
            }
        } else if (strcmp(cmd.type, "IR_TIMING") == 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, cmd.payload)) {
                JsonArray timings = doc["timings"].as<JsonArray>();
                if (!timings.isNull() && timings.size() > 0) {
                    uint16_t count = timings.size();
                    size_t pktLen = 1 + 2 + count * 2;
                    uint8_t* pkt = (uint8_t*)malloc(pktLen);
                    if (pkt) {
                        pkt[0] = static_cast<uint8_t>(MsgType::IR_TIMING);
                        memcpy(pkt + 1, &count, 2);
                        for (uint16_t i = 0; i < count; i++) {
                            uint16_t val = timings[i];
                            memcpy(pkt + 3 + i * 2, &val, 2);
                        }
                        bool ok = ble_send_to_node(cmd.target, pkt, pktLen);
                        Serial.printf("<< IR_TIMING → %s : %u pulses (%s)\n",
                                      cmd.target, count, ok ? "ok" : "fail");
                        free(pkt);
                    }
                }
            }
        } else {
            Serial.printf("<< unknown cmd: %s\n", cmd.type);
        }
    }

    delay(LOOP_DELAY_MS);
}
