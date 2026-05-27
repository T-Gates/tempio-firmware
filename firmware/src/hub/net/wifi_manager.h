// WiFi 매니저 — NVS에 저장된 크레덴셜로 자동 연결 + 시리얼 설정 인터페이스
#pragma once

// NVS에서 SSID/PW 읽어서 WiFi 연결 시도. setup()에서 한 번 호출.
void wifi_init();

// WiFi 연결 상태 반환.
bool wifi_is_connected();

// 시리얼 입력에서 WiFi 명령 처리. loop()에서 호출.
// "wifi set SSID PW" → NVS 저장 + 재연결, "wifi status" → 상태 출력.
void wifi_process_serial();
