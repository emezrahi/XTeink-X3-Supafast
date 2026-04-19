#pragma once
#include <Arduino.h>

// I2C pins shared between X3 peripheral probing and runtime use
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

// TI BQ27220 fuel gauge
#define I2C_ADDR_BQ27220  0x55
#define BQ27220_SOC_REG   0x2C  // StateOfCharge (%)
#define BQ27220_CUR_REG   0x0C  // Current (signed mA)
#define BQ27220_VOLT_REG  0x08  // Voltage (mV)

// DS3231 RTC
#define I2C_ADDR_DS3231   0x68
#define DS3231_SEC_REG    0x00

// QMI8658 IMU
#define I2C_ADDR_QMI8658      0x6B
#define I2C_ADDR_QMI8658_ALT  0x6A
#define QMI8658_WHO_AM_I_REG  0x00
#define QMI8658_WHO_AM_I_VAL  0x05

// NVS namespace/keys for caching detection result across reboots
#define X3_NVS_NAMESPACE  "x3det"
#define X3_NVS_KEY_CACHED "det"   // 0=unknown, 1=x4, 2=x3
#define X3_NVS_KEY_OVERRIDE "ovr" // 0=auto, 1=force x4, 2=force x3

class X3Detect {
public:
  // Run device detection (reads NVS cache, probes I2C if needed, writes cache).
  // Call once at boot before display init.
  static void detect();

  // Result accessors
  static bool isX3() { return _x3; }
  static bool isX4() { return !_x3; }

  // Read battery % from BQ27220 fuel gauge (X3 only)
  static uint8_t readBatteryPercent();

  // Read battery voltage in millivolts from BQ27220 (X3 only)
  static uint16_t readBatteryMillivolts();

  // Read charging current from BQ27220; positive = charging (X3 only)
  static bool readIsCharging();

private:
  static bool _x3;

  static bool readReg8(uint8_t addr, uint8_t reg, uint8_t* out);
  static bool readReg16LE(uint8_t addr, uint8_t reg, uint16_t* out);
  static bool probeBQ27220();
  static bool probeDS3231();
  static bool probeQMI8658();
  static uint8_t runProbePass(); // returns score 0-3
};
