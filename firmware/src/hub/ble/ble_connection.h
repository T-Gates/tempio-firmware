// BLE 스캔 + 연결 관리
#pragma once
#include <NimBLEDevice.h>

// 스캔 결과 콜백 등록 + 스캔 파라미터 설정. ble_central_init()에서 호출.
void ble_connection_init(NimBLEScan* pScan);

// 대기열에서 노드 하나 꺼내 연결 시도. loop()에서 매 틱 호출.
void processNextPending();
