// Unix Timestamp Clock — LILYGO T-Display-S3 AMOLED
// WiFi NTP sync · BLE WiFi provisioning · 5 swipeable views · circular navigation

#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>

#include "config.h"
#include "display.h"
#include "wifi_manager.h"
#include "ble_provision.h"

// ---------------------------------------------------------------------------
// Hardware object — shared with display.cpp (extern'd in display.h)
// ---------------------------------------------------------------------------
LilyGo_Class amoled;

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("[Boot] Unix Timestamp Clock starting");

    if (!amoled.begin()) {
        Serial.println("[ERROR] amoled.begin() failed — halting");
        while (true) { delay(1000); }
    }

    amoled.setBrightness(BRIGHTNESS_LEVELS[BRIGHTNESS_INIT_IDX]);  // start at 192

    // Use DMA buffers (internal SRAM) instead of a PSRAM framebuffer.
    // This avoids any PSRAM init dependency and is more reliable on first boot.
    beginLvglHelperDMA(amoled);

    build_ui();

    // Force the initial render before WiFi/BLE start competing for CPU
    for (int i = 0; i < 10; i++) {
        lv_task_handler();
        delay(10);
    }

    load_wifi_credentials();
    ble_init();     // BLE must start before WiFi — shares the same radio,
    wifi_init();    // coexistence layer needs BLE initialized first

    Serial.println("[Boot] Setup complete");
}

void loop()
{
    lv_task_handler();
    wifi_reconnect_check();
    delay(5);
}
