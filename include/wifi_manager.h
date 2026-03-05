#pragma once

#include <Arduino.h>

// NVS-backed WiFi credentials — defined in wifi_manager.cpp, shared with ble_provision
extern String wifi_ssid;
extern String wifi_pass;

void load_wifi_credentials();
void wifi_init();
void wifi_reconnect_check();
