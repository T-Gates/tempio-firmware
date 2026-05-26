// WiFi 매니저 — NVS 크레덴셜 관리 + 시리얼 명령 인터페이스
#pragma once

void wifi_init();
bool wifi_is_connected();
// 시리얼에서 "wifi set SSID PW", "wifi status" 명령 처리
void wifi_process_serial();
