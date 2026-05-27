// 허브 자체 명령 처리 — target이 비어있는 명령
//
// BLE 노드에 보내는 게 아니라 허브가 직접 실행하는 명령들.
// 예: HUB_STATUS (상태 조회), 향후 HUB_REBOOT 등
#pragma once
#include "../net/mqtt_handler.h"

void handle_hub_command(const MqttCommand& cmd);
