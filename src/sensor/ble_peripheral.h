// #pragma once = 이 헤더 파일이 여러 번 include 되어도 한 번만 처리하라는 지시.
// 없으면 같은 선언이 중복돼서 컴파일 에러남
#pragma once

// BLE Peripheral — 센서 데이터 광고·전송, 허브 명령 수신
//
// [완료] TEMPIO UUID로 광고 → 허브 연결 수락
// [완료] 연결 시 NodeInfo 전송 + 주기적 SensorData notify
// [완료] CONFIG 특성으로 ASSIGN_ID/SET_INTERVAL/RESET 수신
// [TODO] 실제 센서 연결 — DHT22 → SHT40 (Phase 2)
// [TODO] 딥슬립 (Phase 7)

// BLE Peripheral 초기화. 서비스/특성 생성, 광고 시작. setup()에서 한 번만 호출
void ble_peripheral_init();
// BLE Peripheral 매 프레임 처리. 센서값 전송, 상태 출력. loop()에서 반복 호출
void ble_peripheral_loop();
// 현재 허브와 BLE 연결되어 있는지 여부를 반환. true = 연결됨
bool ble_is_connected();
