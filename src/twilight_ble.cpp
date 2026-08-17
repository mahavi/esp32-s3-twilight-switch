#include "twilight_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace {

constexpr char kDeviceName[] = "Twilight Switch";
constexpr char kServiceUuid[] = "5f7d1001-6f5b-4f33-9d9b-2a6d49f91a10";
constexpr char kTestCharacteristicUuid[] = "5f7d1002-6f5b-4f33-9d9b-2a6d49f91a10";

class TestCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        const String value = characteristic->getValue().c_str();

        Serial.printf("BLE write: %s\n", value.c_str());
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

void begin() {
    BLEDevice::init(kDeviceName);

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService* service = server->createService(kServiceUuid);
    BLECharacteristic* testCharacteristic = service->createCharacteristic(
        kTestCharacteristicUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );

    testCharacteristic->setValue("BLE server is ready");
    testCharacteristic->setCallbacks(new TestCharacteristicCallbacks());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.printf("BLE advertising as \"%s\"\n", kDeviceName);
}

}  // namespace TwilightBle
