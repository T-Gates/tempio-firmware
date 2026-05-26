#include <Arduino.h>
#include <ArduinoJson.h>
#include <protocol.h>
#include "cmd_dispatcher.h"
#include "ble/ble_central.h"

void dispatch_command(const MqttCommand& cmd) {
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
                // BLE MTU 512 제한
                if (pktLen > 500) {
                    Serial.printf("<< IR_TIMING → %s : too large (%u bytes)\n",
                                  cmd.target, pktLen);
                    return;
                }
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
