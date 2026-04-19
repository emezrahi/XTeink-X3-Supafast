#pragma once
#include <Arduino.h>
#include <driver/gpio.h>

// Disable internal pull-ups/pull-downs on all GPIOs to minimize leakage
// current during deep sleep. Skips POWER_BUTTON_PIN (wakeup source).
inline void disableGpioPullsForSleep() {
  static constexpr gpio_num_t pins[] = {
      GPIO_NUM_0,  GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_4,  GPIO_NUM_5,
      GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_8,  GPIO_NUM_9,  GPIO_NUM_10,
      GPIO_NUM_13, GPIO_NUM_20, GPIO_NUM_21,
  };
  for (auto pin : pins) {
    gpio_pullup_dis(pin);
    gpio_pulldown_dis(pin);
  }
}
