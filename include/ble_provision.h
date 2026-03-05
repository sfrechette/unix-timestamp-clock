#pragma once

// Initialises the BLE GATT server and starts advertising as BLE_DEVICE_NAME.
// Call after load_wifi_credentials() and before wifi_init().
void ble_init();
