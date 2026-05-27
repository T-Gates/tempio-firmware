// BLE 스캔 + 연결 — 노드 발견 → pending 큐 → 연결 수립
#include <Arduino.h>
#include "ble_internal.h"
#include "ble_connection.h"
#include "ble_data.h"
#include "../cmd/cmd_dispatcher.h"

// ──────────── 연결 대기 큐 ────────────
static NimBLEAddress pendingAddrs[PENDING_MAX];
static int pendingCount = 0;

static bool isAlreadyPending(const NimBLEAddress& addr) {
    for (int i = 0; i < pendingCount; i++) {
        if (pendingAddrs[i] == addr) return true;
    }
    return false;
}

// ──────────── NimBLE 콜백 ────────────

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

// ──────────── 연결 헬퍼 ────────────

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

// 서비스 탐색 + DATA 특성 구독
static NimBLERemoteService* discoverAndSubscribe(NimBLEClient* client) {
    auto* svc = client->getService(TEMPIO_SERVICE_UUID);
    if (!svc) return nullptr;
    auto* dataChar = svc->getCharacteristic(TEMPIO_CHAR_DATA_UUID);
    if (!dataChar || !dataChar->canNotify()) return nullptr;
    dataChar->subscribe(true, onDataNotify);
    return svc;
}

// 슬롯 등록 + HUB_READY 전송
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

    // 이 노드에 대기 중인 서버 명령이 있으면 지금 전송
    flush_node_pending(addr.toString().c_str());
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

// ──────────── 공개 API ────────────

void ble_connection_init(NimBLEScan* pScan) {
    pScan->setScanCallbacks(&scanCb);
    pScan->setActiveScan(true);
    pScan->setInterval(BLE_SCAN_INTERVAL);
    pScan->setWindow(BLE_SCAN_WINDOW);
}

// pending 큐에서 주소 하나를 꺼내 연결
void processNextPending() {
    if (pendingCount == 0) return;
    NimBLEAddress addr = pendingAddrs[0];
    for (int i = 1; i < pendingCount; i++)
        pendingAddrs[i - 1] = pendingAddrs[i];
    pendingCount--;

    NimBLEDevice::getScan()->stop();
    connectToNode(addr);
    NimBLEDevice::getScan()->start(0, false);
}
