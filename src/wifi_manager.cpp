#include "wifi_manager.h"
#include "secrets.h"

#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Preferences.h>

String wifi_ssid;
String wifi_pass;

// ---------------------------------------------------------------------------
// WiFi event handler
// ---------------------------------------------------------------------------
static void on_wifi_event(WiFiEvent_t event)
{
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("[WiFi] Connected to AP");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        // Re-sync every 15 min (SDK default is 3 h) — keeps crystal drift
        // to ~10-20 ms so the millisecond display stays meaningful
        sntp_set_sync_interval(15 * 60 * 1000);
        configTzTime(TIMEZONE_STR,
                     "pool.ntp.org", "time.nist.gov", "time.google.com");
        Serial.println("[NTP] Sync started — waiting for valid time");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("[WiFi] Disconnected");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// NVS credential load — seeds from secrets.h on first boot
// ---------------------------------------------------------------------------
void load_wifi_credentials()
{
    Preferences prefs;
    prefs.begin("wifi", false);   // open read-write so we can seed on first boot

    if (!prefs.isKey("ssid")) {
        prefs.putString("ssid", WIFI_SSID);
        prefs.putString("pass", WIFI_PASSWORD);
        Serial.println("[NVS] Seeding credentials from secrets.h");
    }

    wifi_ssid = prefs.getString("ssid");
    wifi_pass = prefs.getString("pass");
    prefs.end();

    Serial.printf("[NVS] Loaded SSID: '%s'\n", wifi_ssid.c_str());
}

// ---------------------------------------------------------------------------
// WiFi init — uses NVS credentials
// ---------------------------------------------------------------------------
void wifi_init()
{
    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    Serial.printf("[WiFi] Connecting to '%s' ...\n", wifi_ssid.c_str());
}

// ---------------------------------------------------------------------------
// Periodic reconnect fallback (every 30s) — uses NVS credentials
// ---------------------------------------------------------------------------
void wifi_reconnect_check()
{
    static uint32_t last_ms = 0;
    if (millis() - last_ms < 30000) return;
    last_ms = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Not connected — retrying");
        WiFi.disconnect();
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    }
}
