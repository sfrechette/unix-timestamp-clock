// Unix Timestamp Clock — LILYGO T-Display-S3 AMOLED
// WiFi NTP sync · BLE WiFi provisioning · 5 swipeable views · circular navigation

#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <Preferences.h>

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

    // Restore saved orientation (BOOT button toggles it at runtime).
    // Rotation 0 and 2 are both 536x240, so LVGL setup is unaffected.
    Preferences prefs;
    prefs.begin("display", true);
    bool flipped = prefs.getBool("flipped", false);
    prefs.end();
    amoled.setRotation(flipped ? ROTATION_FLIPPED : ROTATION_NORMAL);
    pinMode(FLIP_BUTTON_PIN, INPUT_PULLUP);

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
