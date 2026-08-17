#include <Arduino.h>

#include "twilight_ble.h"

constexpr uint8_t kLightSensorPin = 4;
constexpr uint8_t kRelayPin = 6;

constexpr uint16_t kDarkThreshold = 1500;
constexpr uint16_t kLightThreshold = 3500;

constexpr uint32_t kSampleIntervalMs = 100;

bool relayIsOn = false;

static_assert(kDarkThreshold < kLightThreshold);

void setRelay(bool turnOn) {
    if (relayIsOn == turnOn) {
        return;
    }

    relayIsOn = turnOn;
    digitalWrite(kRelayPin, turnOn ? HIGH : LOW);
    Serial.printf("Relay %s\n", turnOn ? "ON" : "OFF");
}

void updateRelay(uint16_t adcValue) {
    if (adcValue < kDarkThreshold) {
        setRelay(true);
    } else if (adcValue > kLightThreshold) {
        setRelay(false);
    }
}

void setup() {
    Serial.begin(115200);

    analogReadResolution(12);

    digitalWrite(kRelayPin, LOW);
    pinMode(kRelayPin, OUTPUT);

    TwilightBle::begin();
}

void loop() {
    const uint16_t adcValue = analogRead(kLightSensorPin);

    Serial.printf("ADC: %u\n", adcValue);
    updateRelay(adcValue);

    delay(kSampleIntervalMs);
}
