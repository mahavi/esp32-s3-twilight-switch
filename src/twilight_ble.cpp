#include "twilight_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <cstdlib>

namespace {

constexpr char kDeviceName[] = "Twilight Switch";
constexpr char kServiceUuid[] = "5f7d1001-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr char kThresholdsCharacteristicUuid[] = "5f7d1002-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr char kRelayModeCharacteristicUuid[] = "5f7d1003-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr char kRelayStateCharacteristicUuid[] = "5f7d1004-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr uint16_t kMaximumAdcValue = 4095;

portMUX_TYPE thresholdsMutex = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE relayModeMutex = portMUX_INITIALIZER_UNLOCKED;
TwilightBle::Thresholds currentThresholds = {0, 0};
TwilightBle::RelayMode currentRelayMode = TwilightBle::RelayMode::automatic;
Preferences preferences;
bool preferencesAreAvailable = false;
BLECharacteristic* relayStateCharacteristic = nullptr;
volatile bool clientIsConnected = false;

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

TwilightBle::RelayMode relayModeSnapshot() {
    portENTER_CRITICAL(&relayModeMutex);
    const TwilightBle::RelayMode snapshot = currentRelayMode;
    portEXIT_CRITICAL(&relayModeMutex);
    return snapshot;
}

void updateRelayMode(TwilightBle::RelayMode mode) {
    portENTER_CRITICAL(&relayModeMutex);
    currentRelayMode = mode;
    portEXIT_CRITICAL(&relayModeMutex);
}

const char* relayModeValue(TwilightBle::RelayMode mode) {
    switch (mode) {
        case TwilightBle::RelayMode::automatic:
            return "automatic";
        case TwilightBle::RelayMode::on:
            return "on";
        case TwilightBle::RelayMode::off:
            return "off";
    }
    return "automatic";
}

bool parseRelayMode(String value, TwilightBle::RelayMode* result) {
    value.trim();
    value.toLowerCase();
    if (value == "automatic") {
        *result = TwilightBle::RelayMode::automatic;
    } else if (value == "on") {
        *result = TwilightBle::RelayMode::on;
    } else if (value == "off") {
        *result = TwilightBle::RelayMode::off;
    } else {
        return false;
    }
    return true;
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

class RelayModeCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        const String value = characteristic->getValue().c_str();
        TwilightBle::RelayMode mode = TwilightBle::RelayMode::automatic;

        if (!parseRelayMode(value, &mode)) {
            Serial.printf("Rejected BLE relay mode: %s\n", value.c_str());
            characteristic->setValue(relayModeValue(relayModeSnapshot()));
            return;
        }

        updateRelayMode(mode);
        characteristic->setValue(relayModeValue(mode));
        Serial.printf("BLE relay mode updated: %s\n", relayModeValue(mode));
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        clientIsConnected = true;
        Serial.println("BLE client connected");
    }

    void onDisconnect(BLEServer*) override {
        clientIsConnected = false;
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

    BLECharacteristic* relayModeCharacteristic = service->createCharacteristic(
        kRelayModeCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    relayModeCharacteristic->setValue(relayModeValue(RelayMode::automatic));
    relayModeCharacteristic->setCallbacks(new RelayModeCharacteristicCallbacks());

    relayStateCharacteristic = service->createCharacteristic(
        kRelayStateCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    relayStateCharacteristic->setValue("off");
    relayStateCharacteristic->addDescriptor(new BLE2902());

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

RelayMode relayMode() {
    return relayModeSnapshot();
}

void updateRelayState(bool isOn) {
    if (relayStateCharacteristic == nullptr) {
        return;
    }

    relayStateCharacteristic->setValue(isOn ? "on" : "off");
    if (clientIsConnected) {
        relayStateCharacteristic->notify();
    }
}

}  // namespace TwilightBle
