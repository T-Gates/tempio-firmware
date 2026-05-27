// 서버 → 허브 MQTT 명령을 BLE 바이너리 패킷으로 변환해서 노드에 전달
// 파이썬으로 치면: json dict를 struct.pack()으로 바이트 변환 후 BLE 전송
#include <Arduino.h>
#include <ArduinoJson.h>
#include <protocol.h>
#include "cmd_dispatcher.h"
#include "ble/ble_central.h"

// 센서노드 측정 주기 변경. payload 예: {"interval_sec": 30}
static void handleSetInterval(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return;

    SetInterval pkt;
    pkt.interval_sec = doc["interval_sec"] | 60;  // 파이썬 dict.get("key", 60)과 같은 패턴
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< SET_INTERVAL → %s : %us (%s)\n",
                  cmd.target, pkt.interval_sec, ok ? "ok" : "fail");
}

// 노드 리셋. level 0=소프트, 1=팩토리
static void handleResetNode(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return;

    ResetNode pkt;
    pkt.level = doc["level"] | 0;
    bool ok = ble_send_to_node(cmd.target, &pkt, sizeof(pkt));
    Serial.printf("<< RESET_NODE → %s : level=%u (%s)\n",
                  cmd.target, pkt.level, ok ? "ok" : "fail");
}

// IR 신호 전송. payload 예: {"timings": [9000, 4500, 560, ...]}
// timings 배열 = IR LED on/off 마이크로초 시퀀스 (에어컨 리모컨 신호)
static void handleIrTiming(const MqttCommand& cmd) {
    JsonDocument doc;
    if (deserializeJson(doc, cmd.payload)) return;

    JsonArray timings = doc["timings"].as<JsonArray>();
    if (timings.isNull() || timings.size() == 0) return;

    uint16_t count = timings.size();
    // 패킷 구조: [MsgType 1B][count 2B][timing값들 count*2B]
    size_t pktLen = 1 + 2 + count * 2;
    // BLE 한 번에 보낼 수 있는 최대 크기(MTU) 제한
    if (pktLen > 500) {
        Serial.printf("<< IR_TIMING → %s : too large (%u bytes)\n",
                      cmd.target, pktLen);
        return;
    }
    // 가변 길이라 스택 대신 힙 할당 — 파이썬의 bytearray()와 비슷
    uint8_t* pkt = (uint8_t*)malloc(pktLen);
    if (!pkt) return;

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

// cmd.type 문자열로 분기 — C++에선 문자열 switch가 안 돼서 strcmp 사용
void dispatch_command(const MqttCommand& cmd) {
    if (strcmp(cmd.type, "SET_INTERVAL") == 0) {
        handleSetInterval(cmd);
    } else if (strcmp(cmd.type, "RESET_NODE") == 0) {
        handleResetNode(cmd);
    } else if (strcmp(cmd.type, "IR_TIMING") == 0) {
        handleIrTiming(cmd);
    } else {
        Serial.printf("<< unknown cmd: %s\n", cmd.type);
    }
}
