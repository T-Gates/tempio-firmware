// BLE Central 오케스트레이터 — 초기화 + 루프 + 공유 상태 관리
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ble_internal.h"
#include "ble_central.h"
#include "ble_connection.h"
#include "ble_data.h"

// ──────────── 공유 상태 정의 (extern은 ble_internal.h) ────────────
ConnectedNode nodes[MAX_NODES];
volatile bool doScan = false;
static unsigned long lastPrint = 0;

// ──────────── 슬롯 헬퍼 — 다른 ble_*.cpp에서 호출 ────────────

bool isAlreadyConnected(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return true;
    }
    return false;
}

int findEmptySlot() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used) return i;
    }
    return -1;
}

int findSlotByClient(const NimBLEClient* c) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].client == c) return i;
    }
    return -1;
}

int findSlotByAddr(const NimBLEAddress& addr) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].addr == addr) return i;
    }
    return -1;
}

int activeCount() {
    int count = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].used) count++;
    }
    return count;
}

void cleanupDisconnected() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            nodes[i].configChar = nullptr;
        }
    }
}

NimBLEClient* findReusableClient() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used && nodes[i].client) {
            NimBLEClient* c = nodes[i].client;
            nodes[i].client = nullptr;
            return c;
        }
    }
    return nullptr;
}

// ──────────── 공개 API ────────────

void ble_central_init() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !LED_ON);

    NimBLEDevice::init("tempio-hub");
    NimBLEDevice::setMTU(512);

    auto* pScan = NimBLEDevice::getScan();
    ble_connection_init(pScan);
    pScan->start(0, false);
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
    return nodes[slot].configChar->writeValue(
        reinterpret_cast<const uint8_t*>(data), len);
}
