// BLE Central 공개 API — 외부 모듈(main.cpp 등)에서 사용
#pragma once
#include <stdint.h>
#include "../dto/sensor_report.h"

// BLE 스택 초기화 + 스캔 시작. setup()에서 한 번만 호출.
void ble_central_init();

// 매 loop()마다 호출. 끊긴 연결 정리 → 재스캔 → 대기 노드 연결 시도.
void ble_central_loop();

// 현재 BLE로 연결된 노드 수.
int ble_connected_count();

// 특정 노드에 바이너리 데이터 전송. addrStr은 "aa:bb:cc:dd:ee:ff" 형태.
// 내부적으로 CONFIG 특성에 write. 성공 true, 미연결/실패 false.
bool ble_send_to_node(const char* addrStr, const void* data, size_t len);

// 링 버퍼에서 센서 리포트 하나를 꺼낸다. 새 데이터 없으면 false.
// 파이썬의 queue.get_nowait()과 비슷 — 없으면 바로 리턴.
bool ble_get_pending_report(SensorReport* out);
