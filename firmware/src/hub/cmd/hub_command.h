// 허브 자체 명령 처리 — target이 비어있는 명령
//
// BLE 노드에 보내는 게 아니라 허브가 직접 실행하는 명령들.
// 결과 JSON을 버퍼에 쓰고 길이를 리턴. 전송은 호출자(cmd_dispatcher)가 담당.
// 알 수 없는 명령이면 0 리턴.
#pragma once
#include "../net/mqtt_handler.h"

int handle_hub_command(const MqttCommand& cmd, char* buf, size_t bufSize);
