#include "twilight_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <cstdlib>

namespace {

constexpr char kDeviceName[] = "Twilight Switch";
constexpr char kServiceUuid[] = "5f7d1001-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr char kThresholdsCharacteristicUuid[] = "5f7d1002-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr uint16_t kMaximumAdcValue = 4095;

portMUX_TYPE thresholdsMutex = portMUX_INITIALIZER_UNLOCKED;
TwilightBle::Thresholds currentThresholds = {0, 0};
Preferences preferences;
bool preferencesAreAvailable = false;

bool thresholdsAreValid(TwilightBle::Thresholds thresholds) {
    return thresholds.dark < thresholds.light && thresholds.light <= kMaximumAdcValue;
}

TwilightBle::Thresholds thresholdSnapshot() {
    portENTER_CRITICAL(&thresholdsMutex);
    const TwilightBle::Thresholds snapshot = currentThresholds;
    portEXIT_CRITICAL(&thresholdsMutex);
    return snapshot;
}

void updateThresholds(TwilightBle::Thresholds thresholds, bool persist) {
    portENTER_CRITICAL(&thresholdsMutex);
    currentThresholds = thresholds;
    portEXIT_CRITICAL(&thresholdsMutex);

    if (persist && preferencesAreAvailable) {
        preferences.putUShort("dark", thresholds.dark);
        preferences.putUShort("light", thresholds.light);
    }
}

void setCharacteristicValue(
    BLECharacteristic* characteristic,
    TwilightBle::Thresholds thresholds
) {
    char value[16];
    snprintf(value, sizeof(value), "%u,%u", thresholds.dark, thresholds.light);
    characteristic->setValue(value);
}

bool parseNumber(String text, uint16_t* result) {
    text.trim();
    if (text.isEmpty()) {
        return false;
    }

    char* end = nullptr;
    const unsigned long value = strtoul(text.c_str(), &end, 10);
    if (*end != '\0' || value > kMaximumAdcValue) {
        return false;
    }

    *result = static_cast<uint16_t>(value);
    return true;
}

bool parseThresholds(String value, TwilightBle::Thresholds* result) {
    value.trim();
    const int commaIndex = value.indexOf(',');
    if (commaIndex <= 0 || value.indexOf(',', commaIndex + 1) >= 0) {
        return false;
    }

    TwilightBle::Thresholds parsed = {0, 0};
    if (!parseNumber(value.substring(0, commaIndex), &parsed.dark)
        || !parseNumber(value.substring(commaIndex + 1), &parsed.light)
        || !thresholdsAreValid(parsed)) {
        return false;
    }

    *result = parsed;
    return true;
}

class ThresholdsCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        const String value = characteristic->getValue().c_str();
        TwilightBle::Thresholds parsed = {0, 0};

        if (!parseThresholds(value, &parsed)) {
            Serial.printf("Rejected BLE thresholds: %s\n", value.c_str());
            setCharacteristicValue(characteristic, thresholdSnapshot());
            return;
        }

        updateThresholds(parsed, true);
        setCharacteristicValue(characteristic, parsed);
        Serial.printf(
            "BLE thresholds updated: dark=%u, light=%u\n",
            parsed.dark,
            parsed.light
        );
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        Serial.println("BLE client connected");
    }

    void onDisconnect(BLEServer*) override {
        Serial.println("BLE client disconnected");
        BLEDevice::startAdvertising();
    }
};

}  // namespace

namespace TwilightBle {

void begin(Thresholds defaultThresholds) {
    preferencesAreAvailable = preferences.begin("twilight", false);

    Thresholds initialThresholds = defaultThresholds;
    if (preferencesAreAvailable) {
        const Thresholds savedThresholds = {
            preferences.getUShort("dark", defaultThresholds.dark),
            preferences.getUShort("light", defaultThresholds.light),
        };
        if (thresholdsAreValid(savedThresholds)) {
            initialThresholds = savedThresholds;
        }
    }
    updateThresholds(initialThresholds, false);

    BLEDevice::init(kDeviceName);

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService* service = server->createService(kServiceUuid);
    BLECharacteristic* thresholdsCharacteristic = service->createCharacteristic(
        kThresholdsCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );

    setCharacteristicValue(thresholdsCharacteristic, initialThresholds);
    thresholdsCharacteristic->setCallbacks(new ThresholdsCharacteristicCallbacks());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.printf("BLE advertising as \"%s\"\n", kDeviceName);
}

Thresholds thresholds() {
    return thresholdSnapshot();
}

}  // namespace TwilightBle
