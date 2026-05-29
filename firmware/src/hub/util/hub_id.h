// 허브 고유 식별자 — WiFi MAC에서 콜론 제거 + 소문자
// "aabbccddeeff" 형태. MQTT 토픽, 클라이언트 ID 등에 사용.
#pragma once

// WiFi MAC 기반으로 hubId를 생성. WiFi 초기화 후 한 번만 호출.
void hubIdInit();

// 생성된 hubId 문자열 반환. hubIdInit() 호출 전이면 빈 문자열.
const char* getHubId();
