// 노드별 명령 펜딩 큐 + BLE 패킷 변환·전달
//
// 서버에서 명령이 오면:
//   1. 대상 노드가 연결 중이면 → 즉시 BLE 전송
//   2. 연결 안 되어 있으면 → 해당 노드의 펜딩 큐에 보관 (FIFO)
//   3. 나중에 노드가 연결되면 → flushNodePending()으로 밀어냄
#pragma once
#include "../dto/mqtt_command.h"

// MQTT로 수신된 JSON 데이터를 파싱해서 명령별로 디스패치.
// esp_mqtt 태스크에서 호출됨 — Arduino loop가 아닌 다른 태스크!
void parseAndDispatchCommands(const char* data, int len);

// 명령 수신 시 호출. 연결된 노드면 즉시 전송, 아니면 펜딩 큐에 보관.
// esp_mqtt 태스크에서 호출될 수 있으므로 내부적으로 portMUX 보호.
void dispatchCommand(const MqttCommand& cmd);

// 특정 노드의 펜딩 큐를 비우며 전송. BLE 연결 직후 호출.
// Arduino loop 태스크에서만 호출할 것.
void flushNodePending(const char* nodeAddr);

// 연결된 모든 노드의 펜딩 큐를 비우며 전송. loop()에서 주기적으로 호출.
void flushAllPending();

// 펜딩 상태 조회 — 디버그/모니터링용
int pendingActiveSlots();   // 펜딩 명령이 있는 노드 수
int pendingTotalCommands(); // 전체 펜딩 명령 수
