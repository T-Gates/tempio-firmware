// MQTT 명령 → BLE 패킷 변환·전달
//
// 역할: JSON payload를 바이너리 구조체로 변환해서 bleSendToNode() 호출.
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
    bool ok = bleSendToNode(cmd.target, &pkt, sizeof(pkt));
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
    bool ok = bleSendToNode(cmd.target, &pkt, sizeof(pkt));
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
    if (pktLen > BLE_MAX_WRITE_SIZE) {
        Serial.printf("<< IR_TIMING → %s : too large (%u bytes)\n",
                      cmd.target, pktLen);
        return false;
    }
    // 스택 배열 사용 — 위에서 BLE_MAX_WRITE_SIZE 초과 체크를 했으므로 안전
    // heap 할당(new/delete)을 피해 메모리 단편화 방지
    uint8_t pkt[BLE_MAX_WRITE_SIZE];

    pkt[0] = static_cast<uint8_t>(MsgType::IR_TIMING);
    memcpy(pkt + 1, &cmd.cmd_id, 2);
    memcpy(pkt + 3, &count, 2);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t val = timings[i];
        memcpy(pkt + 5 + i * 2, &val, 2);
    }
    bool ok = bleSendToNode(cmd.target, pkt, pktLen);
    Serial.printf("<< IR_TIMING → %s : %u pulses (%s)\n",
                  cmd.target, count, ok ? "ok" : "fail");
    return ok;
}

// TEST → TestCmd 구조체로 변환 후 BLE 전송
static bool handleTest(const MqttCommand& cmd) {
    JsonDocument doc;
    deserializeJson(doc, cmd.payload);

    TestCmd pkt;
    pkt.cmd_id = cmd.cmd_id;
    pkt.led = doc["led"] | 0;
    bool ok = bleSendToNode(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< TEST → %s : led=%u (%s)\n",
                  cmd.target, pkt.led, ok ? "ok" : "fail");
    return ok;
}

// 명령 타입별 핸들러로 라우팅. 알 수 없는 타입이면 false.
static bool trySend(const MqttCommand& cmd) {
    if (strcmp(cmd.type, "SET_INTERVAL") == 0) return handleSetInterval(cmd);
    if (strcmp(cmd.type, "RESET_NODE") == 0)   return handleResetNode(cmd);
    if (strcmp(cmd.type, "IR_TIMING") == 0)     return handleIrTiming(cmd);
    if (strcmp(cmd.type, "TEST") == 0)          return handleTest(cmd);
    Serial.printf("<< unknown cmd: %s\n", cmd.type);
    return false;
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

// 명령 진입점: target 비어있으면 허브 자체 명령, 아니면 노드로 전송 시도 → 실패 시 펜딩
void dispatchCommand(const MqttCommand& cmd) {
    if (cmd.target[0] == '\0') { handleHubCommand(cmd); return; }

    if (trySend(cmd)) return;
    pool.push(cmd.target, cmd);
    Serial.printf(">> pending: %s → %s\n", cmd.type, cmd.target);
}

// 특정 노드의 펜딩 큐에서 명령을 꺼내 전송. 실패하면 다시 넣고 중단.
void flushNodePending(const char* nodeAddr) {
    MqttCommand cmd;
    while (pool.pop(nodeAddr, &cmd)) {
        if (!trySend(cmd)) {
            pool.push(nodeAddr, cmd);
            break;
        }
    }
}

// 모든 노드의 펜딩 큐 순회하며 전송 시도. loop()에서 매 틱 호출.
void flushAllPending() {
    int idx = 0;
    char nodeId[18];
    while (pool.nextNode(&idx, nodeId, sizeof(nodeId))) {
        flushNodePending(nodeId);
    }
}

int pendingActiveSlots() { return pool.activeSlots(); }
int pendingTotalCommands() { return pool.totalPending(); }
