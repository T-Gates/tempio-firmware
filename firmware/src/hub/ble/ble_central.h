#pragma once
#include <stdint.h>

// BLE Central 초기화.
// NimBLE 스택 시작 + 스캔 콜백 등록 + 스캔 시작.
// setup()에서 한 번만 호출.
void ble_central_init();

// BLE Central 매 프레임 처리.
// 끊긴 클라이언트 정리, 재스캔, 대기 중인 노드 연결을 순서대로 처리.
// loop()에서 반복 호출.
void ble_central_loop();

// 현재 연결된 노드 수 반환.
int ble_connected_count();

// 특정 노드에 명령 전송.
// MAC 주소 문자열("AA:BB:CC:DD:EE:FF")로 대상 지정, CONFIG 특성에 write.
// 성공 true, 실패(미연결/노드 없음) false.
bool ble_send_to_node(const char* addrStr, const void* data, size_t len);

// ──────────── 센서 데이터 공유 버퍼 ────────────

struct SensorReport {
    char node_id[18];       // MAC 주소 문자열 "aa:bb:cc:dd:ee:ff"
    char node_type_str[8];  // "sensor" or "ir"
    float temperature;
    float humidity;
    uint16_t ldr;
    uint16_t battery_mv;
    int8_t ble_rssi;
};

// 가장 최근 센서 리포트를 꺼내간다. 새 데이터가 없으면 false.
bool ble_get_pending_report(SensorReport* out);
