#pragma once

#include <cstdint>

namespace TwilightBle {

struct Thresholds {
    uint16_t dark;
    uint16_t light;
};

enum class RelayMode : uint8_t {
    automatic,
    on,
    off,
};

void begin(Thresholds defaultThresholds);
Thresholds thresholds();
RelayMode relayMode();
void updateRelayState(bool isOn);

}  // namespace TwilightBle
