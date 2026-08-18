#include <Arduino.h>

#include "twilight_ble.h"

constexpr uint8_t kLightSensorPin = 4;
constexpr uint8_t kRelayPin = 6;

constexpr TwilightBle::Thresholds kDefaultThresholds = {1500, 3500};

constexpr uint32_t kSampleIntervalMs = 100;

bool relayIsOn = false;

static_assert(kDefaultThresholds.dark < kDefaultThresholds.light);

void setRelay(bool turnOn) {
    if (relayIsOn == turnOn) {
        return;
    }

    relayIsOn = turnOn;
    digitalWrite(kRelayPin, turnOn ? HIGH : LOW);
    TwilightBle::updateRelayState(turnOn);
    Serial.printf("Relay %s\n", turnOn ? "ON" : "OFF");
}

void updateRelay(uint16_t adcValue) {
    switch (TwilightBle::relayMode()) {
        case TwilightBle::RelayMode::on:
            setRelay(true);
            return;
        case TwilightBle::RelayMode::off:
            setRelay(false);
            return;
        case TwilightBle::RelayMode::automatic:
            break;
    }

    const TwilightBle::Thresholds thresholds = TwilightBle::thresholds();

    if (adcValue < thresholds.dark) {
        setRelay(true);
    } else if (adcValue > thresholds.light) {
        setRelay(false);
    }
}

void setup() {
    Serial.begin(115200);

    analogReadResolution(12);

    digitalWrite(kRelayPin, LOW);
    pinMode(kRelayPin, OUTPUT);

    TwilightBle::begin(kDefaultThresholds);
}

void loop() {
    const uint16_t adcValue = analogRead(kLightSensorPin);

    Serial.printf("ADC: %u\n", adcValue);
    updateRelay(adcValue);

    delay(kSampleIntervalMs);
}
