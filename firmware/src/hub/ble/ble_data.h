// BLE 데이터 수신 + 리포트 큐 (센서 + CMD_ACK)
#pragma once
#include <NimBLEDevice.h>
#include "../dto/sensor_report.h"
#include "../dto/cmd_ack_report.h"

// NimBLE notify 콜백 — ble_connection.cpp에서 subscribe 시 전달
void onDataNotify(NimBLERemoteCharacteristic* c,
                  uint8_t* data, size_t len, bool isNotify);

// 센서 리포트 큐에서 하나 꺼냄. 비었으면 false.
bool bleGetPendingReport(SensorReport* out);

// CMD_ACK 큐에서 하나 꺼냄. 비었으면 false.
bool bleGetPendingAck(CmdAckReport* out);
