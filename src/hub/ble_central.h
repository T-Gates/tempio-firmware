// #pragma once = 이 헤더 파일이 여러 번 include 되어도 한 번만 처리하라는 지시.
// 없으면 같은 선언이 중복돼서 컴파일 에러남
#pragma once

// BLE Central — 센서노드/IR노드 스캔·연결·데이터 수신
//
// [완료] TEMPIO UUID로 센서노드 스캔 → 연결 → DATA notify 수신
// [완료] CONFIG 특성으로 ASSIGN_ID 전송
// [TODO] 다중 노드 동시 연결 (현재 1대만)
// [TODO] WiFi + 서버 연동 (Phase 3)
// [TODO] SCD40 CO2 센서 (Phase 4)

// BLE Central 초기화. 스캔 시작, 콜백 등록 등. setup()에서 한 번만 호출
void ble_central_init();
// BLE Central 매 프레임 처리. 연결 시도, 상태 출력 등. loop()에서 반복 호출
void ble_central_loop();
// 현재 센서노드와 BLE 연결되어 있는지 여부를 반환. true = 연결됨
bool ble_is_connected();
