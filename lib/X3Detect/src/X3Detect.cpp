#include <X3Detect.h>
#include <Preferences.h>
#include <Wire.h>

bool X3Detect::_x3 = false;

// ---------- I2C helpers ----------

bool X3Detect::readReg8(uint8_t addr, uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1, (uint8_t)true) < 1) return false;
  *out = Wire.read();
  return true;
}

bool X3Detect::readReg16LE(uint8_t addr, uint8_t reg, uint16_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)2, (uint8_t)true) < 2) {
    while (Wire.available()) Wire.read();
    return false;
  }
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  *out = ((uint16_t)hi << 8) | lo;
  return true;
}

// ---------- Per-chip probes ----------

bool X3Detect::probeBQ27220() {
  uint16_t soc = 0, voltageMv = 0;
  if (!readReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) return false;
  if (soc > 100) return false;
  if (!readReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) return false;
  return voltageMv >= 2500 && voltageMv <= 5000;
}

bool X3Detect::probeDS3231() {
  uint8_t sec = 0;
  if (!readReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) return false;
  // BCD sanity: tens digit 0-5, ones digit 0-9
  return ((sec >> 4) & 0x07) <= 5 && (sec & 0x0F) <= 9;
}

bool X3Detect::probeQMI8658() {
  uint8_t id = 0;
  if (readReg8(I2C_ADDR_QMI8658,     QMI8658_WHO_AM_I_REG, &id) && id == QMI8658_WHO_AM_I_VAL) return true;
  if (readReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &id) && id == QMI8658_WHO_AM_I_VAL) return true;
  return false;
}

uint8_t X3Detect::runProbePass() {
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  uint8_t score = (uint8_t)probeBQ27220()
                + (uint8_t)probeDS3231()
                + (uint8_t)probeQMI8658();
  Wire.end();
  // Return pins to safe input state
  pinMode(X3_I2C_SDA, INPUT);
  pinMode(X3_I2C_SCL, INPUT);
  return score;
}

// ---------- Battery / charging (X3 runtime) ----------

uint8_t X3Detect::readBatteryPercent() {
  if (!_x3) return 0;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  uint16_t soc = 0;
  readReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc);
  Wire.end();
  return (uint8_t)min((uint16_t)100, soc);
}

uint16_t X3Detect::readBatteryMillivolts() {
  if (!_x3) return 0;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  uint16_t mv = 0;
  readReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &mv);
  Wire.end();
  return mv;
}

bool X3Detect::readIsCharging() {
  if (!_x3) return false;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  uint16_t raw = 0;
  bool ok = readReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw);
  Wire.end();
  if (!ok) return false;
  return (int16_t)raw > 0;
}

// ---------- Main detect ----------

void X3Detect::detect() {
  Preferences prefs;

  // Check for manual override first
  if (prefs.begin(X3_NVS_NAMESPACE, true)) {
    uint8_t ovr = prefs.getUChar(X3_NVS_KEY_OVERRIDE, 0);
    prefs.end();
    if (ovr == 1) { _x3 = false; return; }
    if (ovr == 2) { _x3 = true;  return; }
  }

  // Check cached result
  if (prefs.begin(X3_NVS_NAMESPACE, true)) {
    uint8_t cached = prefs.getUChar(X3_NVS_KEY_CACHED, 0);
    prefs.end();
    if (cached == 1) { _x3 = false; return; }
    if (cached == 2) { _x3 = true;  return; }
  }

  // No cache -- run two probe passes (require 2+ of 3 chips on both passes)
  uint8_t s1 = runProbePass();
  delay(2);
  uint8_t s2 = runProbePass();

  bool x3Confirmed = (s1 >= 2) && (s2 >= 2);
  bool x4Confirmed = (s1 == 0) && (s2 == 0);

  if (x3Confirmed || x4Confirmed) {
    _x3 = x3Confirmed;
    if (prefs.begin(X3_NVS_NAMESPACE, false)) {
      prefs.putUChar(X3_NVS_KEY_CACHED, _x3 ? 2 : 1);
      prefs.end();
    }
  }
  // inconclusive: default stays false (X4)
}
