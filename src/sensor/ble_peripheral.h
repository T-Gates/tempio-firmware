#pragma once
#include <stdint.h>

// BLE Peripheral 초기화.
// NimBLE 시작 + Server/Service/Characteristic 생성 + 광고 시작.
// NVS에서 node_id, sleep_interval 복원.
void ble_peripheral_init();

// 허브 연결을 기다린다. timeout_ms 안에 연결되면 true.
bool ble_wait_connect(uint32_t timeout_ms);

// 허브가 CONFIG에 write할 때까지 기다린다.
// 허브는 subscribe 완료 후 ASSIGN_ID를 쓰므로, 이게 오면 데이터 전송 안전.
bool ble_wait_config(uint32_t timeout_ms);

// NodeInfo(자기 소개) 전송.
void ble_send_node_info();

// 센서 데이터 전송.
void ble_send_sensor_data();

// 명시적 BLE 연결 해제 + NVS 닫기 + BLE 스택 종료.
void ble_disconnect();

// 현재 연결 여부.
bool ble_is_connected();

// 딥슬립 주기 (초). 10~3600 범위. 허브가 SET_INTERVAL로 변경 가능.
uint16_t ble_get_sleep_interval();
