#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "wifi_manager.h"

static Preferences prefs;
static String ssid;
static String password;

static void load_credentials() {
    prefs.begin("wifi", true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pw", "");
    prefs.end();
}

static void save_credentials(const String& newSsid, const String& newPw) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", newSsid);
    prefs.putString("pw", newPw);
    prefs.end();
    ssid = newSsid;
    password = newPw;
}

static void connect_wifi() {
    Serial.printf("WiFi connecting to: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 20; // 20 * 500ms = 10s
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
