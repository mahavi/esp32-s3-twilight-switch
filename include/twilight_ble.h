#pragma once

#include <cstdint>

namespace TwilightBle {

struct Thresholds {
    uint16_t dark;
    uint16_t light;
};

void begin(Thresholds defaultThresholds);
Thresholds thresholds();

}  // namespace TwilightBle
