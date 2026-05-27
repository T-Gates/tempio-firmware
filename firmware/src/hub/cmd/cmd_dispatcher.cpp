// MQTT 명령 → BLE 패킷 변환·전달
//
// 역할: JSON payload를 바이너리 구조체로 변환해서 ble_send_to_node() 호출.
// 즉시 전송 실패 시 PendingPool에 보관, 나중에 flush로 재시도.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <protocol.h>
#include "cmd_dispatcher.h"
#include "../ble/ble_central.h"
#include "../config.h"
#include "hub_command.h"
#include "../util/pending_pool.h"

static PendingPool pool;

// ──────────── BLE 패킷 변환 + 전송 ────────────

// SET_INTERVAL → SetInterval 구조체로 변환 후 BLE 전송
static bool handleSetInterval(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    SetInterval pkt;
    pkt.cmd_id = cmd.cmd_id;
    pkt.interval_sec = doc["interval_sec"] | 60;
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< SET_INTERVAL → %s : %us (%s)\n",
                  cmd.target, pkt.interval_sec, ok ? "ok" : "fail");
    return ok;
}

// RESET_NODE → ResetNode 구조체로 변환 후 BLE 전송
static bool handleResetNode(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    ResetNode pkt;
    pkt.cmd_id = cmd.cmd_id;
    pkt.level = doc["level"] | 0;
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< RESET_NODE → %s : level=%u (%s)\n",
                  cmd.target, pkt.level, ok ? "ok" : "fail");
    return ok;
}

// IR_TIMING → 가변 길이 바이너리 패킷(헤더+타이밍 배열)으로 변환 후 BLE 전송
static bool handleIrTiming(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return false;

    JsonArray timings = doc["timings"].as<JsonArray>();
    if (timings.isNull() || timings.size() == 0) return false;

    uint16_t count = timings.size();
    size_t pktLen = 1 + 2 + 2 + count * 2;  // type(1) + cmd_id(2) + count(2) + timings
    if (pktLen > 500) {
        Serial.printf("<< IR_TIMING → %s : too large (%u bytes)\n",
                      cmd.target, pktLen);
        return false;
    }
    auto* pkt = new uint8_t[pktLen];
    if (!pkt) return false;

    pkt[0] = static_cast<uint8_t>(MsgType::IR_TIMING);
    memcpy(pkt + 1, &cmd.cmd_id, 2);
    memcpy(pkt + 3, &count, 2);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t val = timings[i];
        memcpy(pkt + 5 + i * 2, &val, 2);
    }
    bool ok = ble_send_to_node(cmd.target, pkt, pktLen);
    Serial.printf("<< IR_TIMING → %s : %u pulses (%s)\n",
                  cmd.target, count, ok ? "ok" : "fail");
    delete[] pkt;
    return ok;
}

// 명령 타입별 핸들러로 라우팅. 알 수 없는 타입이면 false.
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

// 명령 진입점: target 비어있으면 허브 자체 명령, 아니면 노드로 전송 시도 → 실패 시 펜딩
void dispatch_command(const MqttCommand& cmd) {
    if (cmd.target[0] == '\0') { handle_hub_command(cmd); return; }

    if (trySend(cmd)) return;
    pool.push(cmd.target, cmd);
    Serial.printf(">> pending: %s → %s\n", cmd.type, cmd.target);
}

// 특정 노드의 펜딩 큐에서 명령을 꺼내 전송. 실패하면 다시 넣고 중단.
void flush_node_pending(const char* nodeAddr) {
    MqttCommand cmd;
    while (pool.pop(nodeAddr, &cmd)) {
        if (!trySend(cmd)) {
            pool.push(nodeAddr, cmd);
            break;
        }
    }
}

// 모든 노드의 펜딩 큐 순회하며 전송 시도. loop()에서 매 틱 호출.
void flush_all_pending() {
    int idx = 0;
    char nodeId[18];
    while (pool.nextNode(&idx, nodeId, sizeof(nodeId))) {
        flush_node_pending(nodeId);
    }
}

int pending_active_slots() { return pool.activeSlots(); }
int pending_total_commands() { return pool.totalPending(); }
