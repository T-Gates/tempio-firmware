// WiFi 매니저 — NVS 크레덴셜 저장/로드 + 자동 재연결
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "wifi_manager.h"

static Preferences prefs;
static String ssid;
static String password;

// NVS "wifi" 네임스페이스에서 SSID/PW 읽기
static void load_credentials() {
    prefs.begin("wifi", true); // read-only
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pw", "");
    prefs.end();
}

// NVS에 새 크레덴셜 저장 후 메모리에도 반영
static void save_credentials(const String& newSsid, const String& newPw) {
    prefs.begin("wifi", false); // read-write
    prefs.putString("ssid", newSsid);
    prefs.putString("pw", newPw);
    prefs.end();
    ssid = newSsid;
    password = newPw;
}

// STA 모드로 연결 시도, 최대 10초 대기
static void connect_wifi() {
    Serial.printf("WiFi connecting to: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // 끊기면 ESP32가 자동 재연결 시도
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 20; // 20 * 500ms = 10초 타임아웃
    while (WiFi.status() != WL_CONNECTED && attempts-- > 0) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi failed, use: wifi set SSID PW");
    }
}

// 부팅 시 NVS에서 크레덴셜 로드 → 있으면 자동 연결
void wifi_init() {
    load_credentials();
    if (ssid.length() == 0) {
        Serial.println("No WiFi configured, use: wifi set SSID PW");
        return;
    }
    connect_wifi();
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

// 시리얼 명령 파서 — loop()에서 매 틱 호출
// "wifi set SSID PW" → 크레덴셜 저장 + 즉시 연결
// "wifi status" → 현재 연결 상태 출력
void wifi_process_serial() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("wifi set ")) {
        String args = line.substring(9);
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx < 0) {
            Serial.println("Usage: wifi set SSID PASSWORD");
            return;
        }
        String newSsid = args.substring(0, spaceIdx);
        String newPw = args.substring(spaceIdx + 1);
        save_credentials(newSsid, newPw);
        Serial.printf("WiFi credentials saved: %s\n", newSsid.c_str());
        connect_wifi();
    } else if (line == "wifi status") {
        if (wifi_is_connected()) {
            Serial.printf("WiFi connected, IP: %s, RSSI: %d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
            Serial.printf("WiFi disconnected (ssid: %s)\n",
                          ssid.length() > 0 ? ssid.c_str() : "none");
        }
    }
}
