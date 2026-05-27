// BLE 모듈 내부 공유 상태 — ble/ 폴더 안에서만 include
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "protocol.h"
#include "../config.h"

// C3=GPIO8(active-low), ESP32=GPIO2(active-high)
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr uint8_t LED_PIN = 8;
static constexpr bool LED_ON = LOW;
#else
static constexpr uint8_t LED_PIN = 2;
static constexpr bool LED_ON = HIGH;
#endif

// BLE 연결 슬롯 — 노드 하나당 하나씩
struct ConnectedNode {
    NimBLEClient* client = nullptr;                  // NimBLE 클라이언트 핸들
    NimBLERemoteCharacteristic* configChar = nullptr; // 설정 전송용 특성 (write)
    NimBLEAddress addr;                               // 노드 MAC 주소
    NodeType nodeType = NodeType::SENSOR;             // 센서 or IR
    bool     used     = false;                        // 슬롯 사용 중 여부
};

// ble_central.cpp에서 정의, 다른 ble_*.cpp에서 extern 접근
extern ConnectedNode nodes[MAX_NODES];
extern volatile bool doScan;

// 슬롯 검색 헬퍼 (ble_central.cpp에서 구현)
bool isAlreadyConnected(const NimBLEAddress& addr); // 이미 연결된 주소인지
int findEmptySlot();                                 // 빈 슬롯 인덱스, 없으면 -1
int findSlotByClient(const NimBLEClient* c);         // 클라이언트로 슬롯 찾기
int findSlotByAddr(const NimBLEAddress& addr);       // MAC 주소로 슬롯 찾기
int activeCount();                                   // 사용 중인 슬롯 수
void cleanupDisconnected();                          // 끊긴 연결 슬롯 정리
NimBLEClient* findReusableClient();                  // 끊긴 클라이언트 재활용
