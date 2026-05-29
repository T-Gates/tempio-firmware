#include <Arduino.h>
#include <WiFi.h>
#include "hub_id.h"

static char hubId[16];

void hubIdInit() {
    String mac = WiFi.macAddress();
    int j = 0;
    for (int i = 0; i < static_cast<int>(mac.length()) && j < 12; i++) {
        if (mac[i] != ':') {
            hubId[j++] = static_cast<char>(tolower(static_cast<unsigned char>(mac[i])));
        }
    }
    hubId[j] = '\0';
}

const char* getHubId() {
    return hubId;
}
