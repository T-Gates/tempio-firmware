// ble_central.cpp — 허브 BLE Central (다중 연결)
//
// 흐름: 스캔 → pending 큐 → loop에서 연결 → 서비스 탐색 → notify 구독 → HUB_READY
// 노드 식별: BLE MAC 주소 (공장 고유)

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include "protocol.h"
#include "ble_central.h"
#include "../config.h"

// C3=GPIO8(active-low), ESP32=GPIO2(active-high)
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr uint8_t LED_PIN = 8;
static constexpr bool LED_ON = LOW;
#else
static constexpr uint8_t LED_PIN = 2;
static constexpr bool LED_ON = HIGH;
#endif

// ──────────── 다중 연결 관리 ────────────

struct ConnectedNode {
    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* configChar = nullptr;
    NimBLEAddress addr;
    NodeType nodeType = NodeType::SENSOR;
    bool     used     = false;
};

static ConnectedNode nodes[MAX_NODES];

// ──────────── 연결 대기 큐 ────────────
static NimBLEAddress pendingAddrs[PENDING_MAX];
static int pendingCount = 0;

// ──────────── 센서 데이터 링 버퍼 ────────────
// onDataNotify(NimBLE 태스크)와 ble_get_pending_report(Arduino loop 태스크)가 동시 접근
static SensorReport reportQueue[REPORT_QUEUE_MAX];
static volatile int reportHead = 0;
static volatile int reportTail = 0;
static volatile int reportCount = 0;
static portMUX_TYPE reportMux = portMUX_INITIALIZER_UNLOCKED;

// ──────────── 플래그 ────────────
static volatile bool doScan = false;
static unsigned long lastPrint = 0;

// ══════════════════════════════════════════════════════════════════════
// 헬퍼 — nodes[] 배열 검색/관리
// ══════════════════════════════════════════════════════════════════════

static bool isAlreadyConnected(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return true;
    }
    return false;
}

static bool isAlreadyPending(const NimBLEAddress& addr) {
    for (int i = 0; i < pendingCount; i++) {
        if (pendingAddrs[i] == addr) return true;
    }
    return false;
}

static int findEmptySlot() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used) return i;
    }
    return -1;
}

static int findSlotByClient(const NimBLEClient* c) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].client == c) return i;
    }
    return -1;
}

static int findSlotByAddr(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return i;
    }
    return -1;
}

static int activeCount() {
    int count = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used) count++;
    }
    return count;
}

static void cleanupDisconnected() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            nodes[i].configChar = nullptr;
        }
    }
}

static NimBLEClient* findReusableClient() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            NimBLEClient* c = nodes[i].client;
            nodes[i].client = nullptr;
            return c;
        }
    }
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════
// notify 수신 — 센서노드가 데이터를 push하면 여기로 온다
// ══════════════════════════════════════════════════════════════════════

// notify를 보낸 노드의 슬롯 번호를 역추적
static int resolveSourceSlot(NimBLERemoteCharacteristic* c) {
    auto* svc = c->getRemoteService();
    return svc ? findSlotByClient(svc->getClient()) : -1;
}

static void handleNodeInfo(int slot, const uint8_t* data, size_t len, const char* srcAddr) {
    if (len < sizeof(NodeInfo)) return;
    NodeInfo ni;
    memcpy(&ni, data, sizeof(ni));
    if (slot >= 0) nodes[slot].nodeType = ni.node_type;
    const char* typeName = (ni.node_type == NodeType::SENSOR) ? "sensor" : "ir";
    Serial.printf(">> [%s] NodeInfo: type=%s bat=%umV fw=%u.%u\n",
                  srcAddr, typeName, ni.battery_mv, ni.fw_major, ni.fw_minor);
}

// SensorData → SensorReport 변환
static SensorReport buildReport(int slot, const SensorData& sd, const char* srcAddr) {
    SensorReport rpt;
    strncpy(rpt.node_id, srcAddr, sizeof(rpt.node_id) - 1);
    rpt.node_id[sizeof(rpt.node_id) - 1] = '\0';
    const char* typeStr = (slot >= 0 && nodes[slot].nodeType == NodeType::IR)
        ? "ir" : "sensor";
    strncpy(rpt.node_type_str, typeStr, sizeof(rpt.node_type_str) - 1);
    rpt.node_type_str[sizeof(rpt.node_type_str) - 1] = '\0';
    rpt.temperature = sd.temp;
    rpt.humidity    = sd.humidity;
    rpt.ldr         = sd.ldr;
    rpt.battery_mv  = sd.battery_mv;
    rpt.ble_rssi    = (slot >= 0) ? nodes[slot].client->getRssi() : 0;
    return rpt;
}

static void enqueueReport(const SensorReport& rpt) {
    portENTER_CRITICAL(&reportMux);
    if (reportCount < REPORT_QUEUE_MAX) {
        reportQueue[reportTail] = rpt;
        reportTail = (reportTail + 1) % REPORT_QUEUE_MAX;
        reportCount++;
    }
    portEXIT_CRITICAL(&reportMux);
}

static void handleSensorData(int slot, const uint8_t* data, size_t len, const char* srcAddr) {
    if (len < sizeof(SensorData)) return;
    SensorData sd;
    memcpy(&sd, data, sizeof(sd));
    Serial.printf(">> [%s] sensor: %.1f C  %.1f %%  ldr=%u  bat=%umV\n",
                  srcAddr, sd.temp, sd.humidity, sd.ldr, sd.battery_mv);
    enqueueReport(buildReport(slot, sd, srcAddr));
}

static void onDataNotify(NimBLERemoteCharacteristic* c,
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
        default:
            Serial.printf(">> [%s] unknown: 0x%02x (%u bytes)\n",
                          srcAddr, data[0], len);
    }
}

// ══════════════════════════════════════════════════════════════════════
// BLE 콜백 — NimBLE가 이벤트 발생 시 자동 호출
// ══════════════════════════════════════════════════════════════════════

class ScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (!dev->isAdvertisingService(NimBLEUUID(TEMPIO_SERVICE_UUID))) return;
        auto addr = dev->getAddress();
        if (isAlreadyConnected(addr)) return;
        if (isAlreadyPending(addr))   return;
        if (findEmptySlot() < 0)      return;
        if (pendingCount >= PENDING_MAX) return;

        Serial.printf("found: %s  rssi=%d\n",
                      addr.toString().c_str(), dev->getRSSI());
        pendingAddrs[pendingCount++] = addr;
    }
};

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override {
        Serial.printf("connected: %s\n", c->getPeerAddress().toString().c_str());
    }

    void onDisconnect(NimBLEClient* c, int reason) override {
        int slot = findSlotByClient(c);
        if (slot >= 0) {
            Serial.printf("%s disconnected (reason=%d)\n",
                          nodes[slot].addr.toString().c_str(), reason);
            nodes[slot].used = false;
            nodes[slot].configChar = nullptr;
        }
        digitalWrite(LED_PIN, activeCount() > 0 ? LED_ON : !LED_ON);
        doScan = true;
    }
};

static ScanCB  scanCb;
static ClientCB clientCb;

// ══════════════════════════════════════════════════════════════════════
// 연결 — pending 큐에서 꺼낸 주소로 BLE 연결 수립
// ══════════════════════════════════════════════════════════════════════

// NimBLE 2.5.0에서 deleteClient()가 heap 크래시 → 삭제 대신 슬롯에 보관
static void stashClient(NimBLEClient* c) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && !nodes[i].client) {
            nodes[i].client = c;
            return;
        }
    }
}

static void disconnectAndStash(NimBLEClient* c) {
    c->disconnect();
    stashClient(c);
}

static NimBLEClient* acquireClient() {
    auto* c = findReusableClient();
    if (!c) c = NimBLEDevice::createClient();
    if (c) c->setClientCallbacks(&clientCb);
    return c;
}

// 서비스 탐색 + DATA 특성 구독. 성공 시 서비스 포인터 반환.
static NimBLERemoteService* discoverAndSubscribe(NimBLEClient* client) {
    auto* svc = client->getService(TEMPIO_SERVICE_UUID);
    if (!svc) return nullptr;
    auto* dataChar = svc->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    if (!dataChar || !dataChar->canNotify()) return nullptr;
    dataChar->subscribe(true, onDataNotify);
    return svc;
}

// 슬롯에 등록 + HUB_READY 전송 ("나 준비됐어, 데이터 보내도 돼")
static void registerNode(int slot, NimBLEClient* client,
                         const NimBLEAddress& addr, NimBLERemoteService* svc) {
    nodes[slot].client     = client;
    nodes[slot].configChar = svc->getCharacteristic(TEMPIO_CHAR_CONFIG_UUID);
    nodes[slot].addr       = addr;
    nodes[slot].used       = true;

    delay(200);
    HubReady ready;
    if (nodes[slot].configChar) {
        nodes[slot].configChar->writeValue(
            reinterpret_cast<const uint8_t*>(&ready), sizeof(ready));
    }
    Serial.printf("<< HUB_READY → %s\n", addr.toString().c_str());
    digitalWrite(LED_PIN, LED_ON);
}

static bool connectToNode(const NimBLEAddress& addr) {
    int slot = findEmptySlot();
    if (slot < 0) return false;

    auto* client = acquireClient();
    if (!client) return false;

    if (!client->connect(addr)) {
        disconnectAndStash(client);
        return false;
    }
    auto* svc = discoverAndSubscribe(client);
    if (!svc) {
        disconnectAndStash(client);
        return false;
    }
    registerNode(slot, client, addr, svc);
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// 공개 API
// ══════════════════════════════════════════════════════════════════════

void ble_central_init() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !LED_ON);

    NimBLEDevice::init("tempio-hub");
    NimBLEDevice::setMTU(512);

    auto* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCb);
    pScan->setActiveScan(true);
    pScan->setInterval(BLE_SCAN_INTERVAL);
    pScan->setWindow(BLE_SCAN_WINDOW);
    pScan->start(0, false);
}

// pending 큐에서 주소 하나를 꺼내 연결. connect()가 blocking이라 한 번에 하나만.
static void processNextPending() {
    if (pendingCount == 0) return;
    NimBLEAddress addr = pendingAddrs[0];
    for (int i = 1; i < pendingCount; i++)
        pendingAddrs[i - 1] = pendingAddrs[i];
    pendingCount--;

    NimBLEDevice::getScan()->stop();
    connectToNode(addr);
    NimBLEDevice::getScan()->start(0, false);
}

static void printStatus() {
    if (millis() - lastPrint <= 5000) return;
    lastPrint = millis();
    int count = activeCount();
    if (count == 0) Serial.println("scanning...");
    else Serial.printf("nodes: %d connected\n", count);
}

void ble_central_loop() {
    cleanupDisconnected();
    if (doScan) {
        doScan = false;
        NimBLEDevice::getScan()->start(0, false);
    }
    processNextPending();
    printStatus();
}

int ble_connected_count() {
    return activeCount();
}

bool ble_send_to_node(const char* addrStr, const void* data, size_t len) {
    NimBLEAddress addr(std::string(addrStr), 0);
    int slot = findSlotByAddr(addr);
    if (slot < 0 || !nodes[slot].configChar) return false;

    nodes[slot].configChar->writeValue(
        reinterpret_cast<const uint8_t*>(data), len);
    return true;
}

bool ble_get_pending_report(SensorReport* out) {
    portENTER_CRITICAL(&reportMux);
    if (reportCount <= 0) {
        portEXIT_CRITICAL(&reportMux);
        return false;
    }
    *out = reportQueue[reportHead];
    reportHead = (reportHead + 1) % REPORT_QUEUE_MAX;
    reportCount--;
    portEXIT_CRITICAL(&reportMux);
    return true;
}
