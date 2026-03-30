#include "battery.h"

#include <array>
#include <cstddef>

namespace esphome {
namespace arc_bridge {

namespace {

struct BatteryCurvePoint {
  float volts;
  float percent;
};

// Coarse 3S Li-ion open-circuit estimate points.
static constexpr std::array<BatteryCurvePoint, 9> BATTERY_CURVE_3S_LI_ION = {{
    {9.0f, 0.0f},
    {9.9f, 5.0f},
    {10.5f, 15.0f},
    {11.1f, 35.0f},
    {11.4f, 55.0f},
    {11.7f, 72.0f},
    {12.0f, 85.0f},
    {12.3f, 95.0f},
    {12.6f, 100.0f},
}};

}  // namespace

float battery_percent_from_3s_li_ion(float volts) {
  if (volts <= BATTERY_CURVE_3S_LI_ION.front().volts) {
    return BATTERY_CURVE_3S_LI_ION.front().percent;
  }

  if (volts >= BATTERY_CURVE_3S_LI_ION.back().volts) {
    return BATTERY_CURVE_3S_LI_ION.back().percent;
  }

  for (std::size_t i = 1; i < BATTERY_CURVE_3S_LI_ION.size(); i++) {
    const auto &low = BATTERY_CURVE_3S_LI_ION[i - 1];
    const auto &high = BATTERY_CURVE_3S_LI_ION[i];
    if (volts > high.volts) {
      continue;
    }

    const float span = high.volts - low.volts;
    if (span <= 0.0f) {
      return high.percent;
    }

    const float ratio = (volts - low.volts) / span;
    return low.percent + ratio * (high.percent - low.percent);
  }

  return BATTERY_CURVE_3S_LI_ION.back().percent;
}

}  // namespace arc_bridge
}  // namespace esphome
