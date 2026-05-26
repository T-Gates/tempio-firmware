// 허브 설정값 — 환경에 따라 여기만 수정
#pragma once

// MQTT 브로커 URI (ws:// 또는 wss://)
// 로컬: "ws://192.168.0.7:9001/mqtt"
// 프로덕션: "wss://mqtt.yuumi.wiki/mqtt"
#define MQTT_BROKER_URI  "wss://mqtt.yuumi.wiki/mqtt"

// 메인 루프 주기 (ms)
#define LOOP_DELAY_MS    100
