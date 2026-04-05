#include "battery.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using esphome::arc_bridge::battery_percent_from_3s_li_ion;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

bool approx(float actual, float expected, float tolerance = 0.05f) {
  return std::fabs(actual - expected) <= tolerance;
}

void test_curve_clamps() {
  require(approx(battery_percent_from_3s_li_ion(8.5f), 0.0f),
          "voltages below the curve should clamp to 0%");
  require(approx(battery_percent_from_3s_li_ion(12.8f), 100.0f),
          "voltages above the curve should clamp to 100%");
}

void test_curve_anchors() {
  require(approx(battery_percent_from_3s_li_ion(9.0f), 0.0f), "9.0V should be empty");
  require(approx(battery_percent_from_3s_li_ion(11.1f), 35.0f), "11.1V should match the curve");
  require(approx(battery_percent_from_3s_li_ion(12.6f), 100.0f), "12.6V should be full");
}

void test_curve_interpolation() {
  require(approx(battery_percent_from_3s_li_ion(11.55f), 63.5f),
          "intermediate voltages should interpolate between curve anchors");
}

}  // namespace

int main() {
  test_curve_clamps();
  test_curve_anchors();
  test_curve_interpolation();
  std::cout << "battery curve tests passed" << std::endl;
  return 0;
}
