#pragma once
#include <BatteryMonitor.h>
#include <X3Detect.h>

#define BAT_GPIO0 0  // Battery voltage (X4 ADC pin)

// Wraps BatteryMonitor for X4 and BQ27220 fuel gauge for X3.
struct BatteryReader {
  uint16_t readPercentage() const {
    if (X3Detect::isX3()) return X3Detect::readBatteryPercent();
    static BatteryMonitor adcMon(BAT_GPIO0);
    return adcMon.readPercentage();
  }

  uint16_t readMillivolts() const {
    if (X3Detect::isX3()) return X3Detect::readBatteryMillivolts();
    static BatteryMonitor adcMon(BAT_GPIO0);
    return adcMon.readMillivolts();
  }

  static uint16_t percentageFromMillivolts(uint16_t mv) {
    return BatteryMonitor::percentageFromMillivolts(mv);
  }
};

inline BatteryReader& getBatteryMonitor() {
  static BatteryReader instance;
  return instance;
}

#define batteryMonitor getBatteryMonitor()
