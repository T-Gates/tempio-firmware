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

// ──────────── 슬롯 검색 공통 헬퍼 ────────────
// 파이썬의 next((i for i, n in enumerate(nodes) if pred(n)), -1) 과 같은 패턴
// 인덱스가 필요한 함수들이 공유하는 기반 함수

// 조건에 맞는 슬롯의 인덱스를 반환. 없으면 -1.
template <typename Pred>
static int findSlotIndex(Pred pred) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (pred(nodes[i])) return i;
    }
    return -1;
}

// ──────────── 슬롯 헬퍼 — 다른 ble_*.cpp에서 호출 ────────────

bool isAlreadyConnected(const NimBLEAddress& addr) {
    return findSlotIndex([&](const ConnectedNode& n) {
        return n.used && n.addr == addr;
    }) >= 0;
}

int findEmptySlot() {
    return findSlotIndex([](const ConnectedNode& n) { return !n.used; });
}

int findSlotByClient(const NimBLEClient* c) {
    return findSlotIndex([c](const ConnectedNode& n) {
        return n.used && n.client == c;
    });
}

int findSlotByAddr(const NimBLEAddress& addr) {
    return findSlotIndex([&](const ConnectedNode& n) {
        return n.used && n.addr == addr;
    });
}

// 인덱스 불필요 — range-based for 사용
int activeCount() {
    int count = 0;
    for (const auto& n : nodes) {
        if (n.used) count++;
    }
    return count;
}

// 끊긴 슬롯의 configChar 정리 — range-based for
void cleanupDisconnected() {
    for (auto& n : nodes) {
        if (!n.used && n.client) {
            n.configChar = nullptr;
        }
    }
}

// 끊긴 클라이언트 객체 재활용 — 부작용(nullptr 대입)이 있으므로 별도 구현
NimBLEClient* findReusableClient() {
    int idx = findSlotIndex([](const ConnectedNode& n) {
        return !n.used && n.client;
    });
    if (idx < 0) return nullptr;
    NimBLEClient* c = nodes[idx].client;
    nodes[idx].client = nullptr;
    return c;
}

// ──────────── 공개 API ────────────

// NimBLE 스택 초기화 + 스캔 시작
void bleCentralInit() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !LED_ON);

    NimBLEDevice::init("tempio-hub");
    NimBLEDevice::setMTU(BLE_MTU);  // IR 타이밍 등 큰 패킷을 위해 MTU 확대

    auto* pScan = NimBLEDevice::getScan();
    bleConnectionInit(pScan);
    pScan->start(0, false);     // 0 = 무한 스캔, false = 중복 결과 필터링
}

// 5초마다 연결 상태 시리얼 출력
static void printStatus() {
    if (millis() - lastPrint <= 5000) return;
    lastPrint = millis();
    int count = activeCount();
    if (count == 0) Serial.println("scanning...");
    else Serial.printf("nodes: %d connected\n", count);
}

// 매 loop() 틱마다 호출 — 끊긴 연결 정리 → 재스캔 → 대기 노드 연결
void bleCentralLoop() {
    cleanupDisconnected();
    if (doScan) {
        doScan = false;
        NimBLEDevice::getScan()->start(0, false);
    }
    // 한번에 하나씩. 여러 개 처리하면 루프가 길어져서 MQTT 등 다른 작업이 밀림.
    processNextPending();
    printStatus();
}

int bleConnectedCount() {
    return activeCount();
}

bool bleIsNodeConnected(const char* addrStr) {
    NimBLEAddress addr(std::string(addrStr), 0);
    return isAlreadyConnected(addr);
}

// MAC 주소 문자열로 노드를 찾아 CONFIG 특성에 바이너리 write
bool bleSendToNode(const char* addrStr, const void* data, size_t len) {
    NimBLEAddress addr(std::string(addrStr), 0);
    int slot = findSlotByAddr(addr);
    if (slot < 0 || !nodes[slot].configChar) return false;
    return nodes[slot].configChar->writeValue(
        reinterpret_cast<const uint8_t*>(data), len);
}
