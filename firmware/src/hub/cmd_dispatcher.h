// MQTT 명령 → BLE 패킷 변환·전달
// 서버가 보낸 JSON 명령을 노드가 이해하는 바이너리로 변환해서 BLE write.
// 파이썬으로 치면: json.loads() → struct.pack() → ble.write()
#pragma once
#include "net/mqtt_handler.h"

// 명령 타입(cmd.type)에 따라 적절한 핸들러로 라우팅.
// SET_INTERVAL, RESET_NODE, IR_TIMING 지원.
void dispatch_command(const MqttCommand& cmd);
