// BLE 데이터 수신 + 센서 리포트 큐
#pragma once
#include <NimBLEDevice.h>
#include "../dto/sensor_report.h"

// NimBLE notify 콜백 — ble_connection.cpp에서 subscribe 시 전달
void onDataNotify(NimBLERemoteCharacteristic* c,
                  uint8_t* data, size_t len, bool isNotify);

// 리포트 큐에서 하나 꺼냄. 비었으면 false.
bool ble_get_pending_report(SensorReport* out);
