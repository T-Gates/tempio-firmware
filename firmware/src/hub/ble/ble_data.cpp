// BLE 데이터 수신 — notify 콜백으로 센서 데이터를 받아서 리포트 큐에 적재
#include <Arduino.h>
#include <cstring>
#include "ble_internal.h"
#include "ble_data.h"
#include "../util/thread_safe_queue.h"

static ThreadSafeQueue<SensorReport, REPORT_QUEUE_MAX> reportQueue;
static ThreadSafeQueue<CmdAckReport, CMD_PENDING_PER_NODE> ackQueue;

// notify를 보낸 특성 → 클라이언트 → 슬롯 인덱스 역추적
static int resolveSourceSlot(NimBLERemoteCharacteristic* c) {
    auto* svc = c->getRemoteService();
    return svc ? findSlotByClient(svc->getClient()) : -1;
}

// NODE_INFO 메시지 처리 — 노드 타입(sensor/ir) 등록 + 시리얼 출력
static void handleNodeInfo(int slot, const uint8_t* data, size_t len, const char* srcAddr) {
    if (len < sizeof(NodeInfo)) return;
    NodeInfo ni;
    memcpy(&ni, data, sizeof(ni));
    if (slot >= 0) nodes[slot].nodeType = ni.node_type;
    const char* typeName = (ni.node_type == NodeType::SENSOR) ? "sensor" : "ir";
    Serial.printf(">> [%s] NodeInfo: type=%s bat=%umV fw=%u.%u\n",
                  srcAddr, typeName, ni.battery_mv, ni.fw_major, ni.fw_minor);
}

// BLE 바이너리 → SensorReport DTO 변환 (허브 상태는 main.cpp에서 주입)
static SensorReport buildReport(int slot, const SensorData& sd, const char* srcAddr) {
    SensorReport rpt;
    // strlcpy는 항상 널 종단을 보장 — strncpy + 수동 '\0'보다 안전
    strlcpy(rpt.node_id, srcAddr, sizeof(rpt.node_id));
    const char* typeStr = (slot >= 0 && nodes[slot].nodeType == NodeType::IR)
        ? "ir" : "sensor";
    strlcpy(rpt.node_type_str, typeStr, sizeof(rpt.node_type_str));
    rpt.temperature = sd.temp;
    rpt.humidity    = sd.humidity;
    rpt.ldr         = sd.ldr;
    rpt.battery_mv  = sd.battery_mv;
    rpt.ble_rssi    = (slot >= 0) ? nodes[slot].client->getRssi() : 0;
    return rpt;
}

static void enqueueReport(const SensorReport& rpt) {
    reportQueue.push(rpt);
}

static void handleCmdAck(const uint8_t* data, size_t len, const char* srcAddr) {
    if (len < sizeof(CmdAck)) return;
    CmdAck ack;
    memcpy(&ack, data, sizeof(ack));
    Serial.printf(">> [%s] CMD_ACK: id=%u %s\n",
                  srcAddr, ack.cmd_id, ack.success ? "ok" : "fail");

    CmdAckReport rpt;
    strlcpy(rpt.node_id, srcAddr, sizeof(rpt.node_id));
    rpt.cmd_id = ack.cmd_id;
    rpt.success = ack.success;
    ackQueue.push(rpt);
}

// SENSOR_DATA 메시지 처리 — 파싱 → 리포트 생성 → 큐에 적재
static void handleSensorData(int slot, const uint8_t* data, size_t len, const char* srcAddr) {
    if (len < sizeof(SensorData)) return;
    SensorData sd;
    memcpy(&sd, data, sizeof(sd));
    Serial.printf(">> [%s] sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                  srcAddr, sd.temp, sd.humidity, sd.ldr, sd.battery_mv);
    enqueueReport(buildReport(slot, sd, srcAddr));
}

// NimBLE notify 콜백 — 센서노드가 데이터를 push하면 자동 호출
void onDataNotify(NimBLERemoteCharacteristic* c,
                  uint8_t* data, size_t len, bool isNotify) {
    if (len < 1) return;
    int slot = resolveSourceSlot(c);
    // toString()은 임시 std::string — c_str()만 저장하면 댕글링 포인터
    std::string addrStr = (slot >= 0) ? nodes[slot].addr.toString() : "??";
    const char* srcAddr = addrStr.c_str();

    switch (static_cast<MsgType>(data[0])) {
        case MsgType::NODE_INFO:
            handleNodeInfo(slot, data, len, srcAddr);
            break;
        case MsgType::SENSOR_DATA:
            handleSensorData(slot, data, len, srcAddr);
            break;
        case MsgType::CMD_ACK:
            handleCmdAck(data, len, srcAddr);
            break;
        default:
            Serial.printf(">> [%s] unknown: 0x%02x (%u bytes)\n",
                          srcAddr, data[0], len);
    }
}

bool bleGetPendingReport(SensorReport* out) {
    return reportQueue.pop(out);
}

bool bleGetPendingAck(CmdAckReport* out) {
    return ackQueue.pop(out);
}
