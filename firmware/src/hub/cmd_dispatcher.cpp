// MQTT 명령 → BLE 패킷 변환·전달
//
// 역할: JSON payload를 바이너리 구조체로 변환해서 ble_send_to_node() 호출.
// 즉시 전송 실패 시 PendingPool에 보관, 나중에 flush로 재시도.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <protocol.h>
#include "cmd_dispatcher.h"
#include "ble/ble_central.h"
#include "config.h"
#include "util/pending_pool.h"

static PendingPool pool;

// ──────────── BLE 패킷 변환 + 전송 ────────────

static bool handleSetInterval(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    SetInterval pkt;
    pkt.interval_sec = doc["interval_sec"] | 60;
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< SET_INTERVAL → %s : %us (%s)\n",
                  cmd.target, pkt.interval_sec, ok ? "ok" : "fail");
    return ok;
}

static bool handleResetNode(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    ResetNode pkt;
    pkt.level = doc["level"] | 0;
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< RESET_NODE → %s : level=%u (%s)\n",
                  cmd.target, pkt.level, ok ? "ok" : "fail");
    return ok;
}

static bool handleIrTiming(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    JsonArray timings = doc["timings"].as<JsonArray>();
    if (timings.isNull() || timings.size() == 0) return false;

    uint16_t count = timings.size();
    size_t pktLen = 1 + 2 + count * 2;
    if (pktLen > 500) {
        Serial.printf("<< IR_TIMING → %s : too large (%u bytes)\n",
                      cmd.target, pktLen);
        return false;
    }
    uint8_t* pkt = (uint8_t*)malloc(pktLen);
    if (!pkt) return false;

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
    return ok;
}

static bool trySend(const MqttCommand& cmd) {
    if (strcmp(cmd.type, "SET_INTERVAL") == 0) return handleSetInterval(cmd);
    if (strcmp(cmd.type, "RESET_NODE") == 0)   return handleResetNode(cmd);
    if (strcmp(cmd.type, "IR_TIMING") == 0)     return handleIrTiming(cmd);
    Serial.printf("<< unknown cmd: %s\n", cmd.type);
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void dispatch_command(const MqttCommand& cmd) {
    if (trySend(cmd)) return;
    pool.push(cmd.target, cmd);
    Serial.printf(">> pending: %s → %s\n", cmd.type, cmd.target);
}

void flush_node_pending(const char* nodeAddr) {
    MqttCommand cmd;
    while (pool.pop(nodeAddr, &cmd)) {
        if (!trySend(cmd)) {
            pool.push(nodeAddr, cmd);
            break;
        }
    }
}

void flush_all_pending() {
    int idx = 0;
    char nodeId[18];
    while (pool.nextNode(&idx, nodeId, sizeof(nodeId))) {
        flush_node_pending(nodeId);
    }
}

int pending_active_slots() { return pool.activeSlots(); }
int pending_total_commands() { return pool.totalPending(); }
