#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// BLE device name and GATT UUIDs
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME  "ClockConfig"
#define BLE_SVC_UUID     "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define BLE_PIN_UUID     "AA000001-C5A8-4C7D-AEA1-B2D3F50B0B55"   // 1 — enter PIN first
#define BLE_SSID_UUID    "AA000002-C5A8-4C7D-AEA1-B2D3F50B0B55"   // 2 — enter SSID
#define BLE_PASS_UUID    "AA000003-C5A8-4C7D-AEA1-B2D3F50B0B55"   // 3 — enter password
#define BLE_APPLY_UUID   "AA000004-C5A8-4C7D-AEA1-B2D3F50B0B55"   // 4 — apply / commit

// ---------------------------------------------------------------------------
// Brightness levels — screen tap cycles through these (index 0-3)
// ---------------------------------------------------------------------------
#define BRIGHTNESS_INIT_IDX  2          // start at index 2 = 192/255

static const uint8_t BRIGHTNESS_LEVELS[] = { 64, 128, 192, 255 };
static const uint8_t BRIGHTNESS_COUNT    = 4;
