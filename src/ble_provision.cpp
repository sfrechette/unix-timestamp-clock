#include "ble_provision.h"
#include "config.h"
#include "wifi_manager.h"
#include "secrets.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// BLE state (private to this translation unit)
// ---------------------------------------------------------------------------
static String  pending_ssid;
static String  pending_pass;
static bool    ble_authenticated = false;   // set by correct PIN, cleared on disconnect

// ---------------------------------------------------------------------------
// BLE characteristic callbacks
// ---------------------------------------------------------------------------

// PIN write — must match BLE_PIN from secrets.h before any other write works
class PinCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (c->getValue() == BLE_PIN) {
            ble_authenticated = true;
            Serial.println("[BLE] PIN accepted — provisioning unlocked");
        } else {
            ble_authenticated = false;
            Serial.println("[BLE] PIN rejected");
        }
    }
};

// SSID write — stages the new SSID (requires PIN first)
class SsidCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!ble_authenticated) { Serial.println("[BLE] SSID write blocked — not authenticated"); return; }
        pending_ssid = c->getValue().c_str();
        Serial.printf("[BLE] SSID staged: '%s'\n", pending_ssid.c_str());
    }
};

// Password write — stages the new password (requires PIN first)
class PassCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!ble_authenticated) { Serial.println("[BLE] Password write blocked — not authenticated"); return; }
        pending_pass = c->getValue().c_str();
        Serial.println("[BLE] Password staged");
    }
};

// Server callbacks — restart advertising and clear auth on disconnect
class ServerCallbacks : public BLEServerCallbacks {
    void onDisconnect(BLEServer * /*server*/) override {
        ble_authenticated = false;
        Serial.println("[BLE] Client disconnected — auth cleared, restarting advertising");
        BLEDevice::startAdvertising();
    }
};

// Apply write — write "1" to save staged credentials and reconnect (requires PIN first)
class ApplyCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!ble_authenticated) { Serial.println("[BLE] Apply blocked — not authenticated"); return; }
        if (c->getValue() != "1") return;
        if (pending_ssid.isEmpty()) {
            Serial.println("[BLE] Apply ignored — SSID is empty");
            return;
        }

        // Persist to NVS
        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.putString("ssid", pending_ssid);
        prefs.putString("pass", pending_pass);
        prefs.end();

        // Update runtime credentials and reconnect
        wifi_ssid = pending_ssid;
        wifi_pass = pending_pass;

        Serial.printf("[BLE] Applying SSID: '%s' — reconnecting\n", wifi_ssid.c_str());
        WiFi.disconnect();
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    }
};

// ---------------------------------------------------------------------------
// BLE init — advertises as BLE_DEVICE_NAME
// ---------------------------------------------------------------------------
void ble_init()
{
    pending_ssid = wifi_ssid;   // pre-fill with current SSID (readable in nRF Connect)
    pending_pass = "";           // password is never pre-filled

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEServer  *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());   // restart advertising on disconnect
    BLEService *svc    = server->createService(BLE_SVC_UUID);

    // PIN — write-only, must be correct before other writes are accepted
    BLECharacteristic *pin_char = svc->createCharacteristic(
        BLE_PIN_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pin_char->setCallbacks(new PinCallback());

    // SSID — readable (shows active SSID) + writable
    BLECharacteristic *ssid_char = svc->createCharacteristic(
        BLE_SSID_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    ssid_char->setValue(wifi_ssid.c_str());
    ssid_char->setCallbacks(new SsidCallback());

    // Password — write-only (never readable)
    BLECharacteristic *pass_char = svc->createCharacteristic(
        BLE_PASS_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pass_char->setCallbacks(new PassCallback());

    // Apply — write "1" to commit and reconnect
    BLECharacteristic *apply_char = svc->createCharacteristic(
        BLE_APPLY_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    apply_char->setCallbacks(new ApplyCallback());

    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE] Advertising as '%s'\n", BLE_DEVICE_NAME);
}
