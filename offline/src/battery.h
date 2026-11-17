#pragma once
#include <Arduino.h>
#include "board_config.h"
// Voltage estimate for ONE lithium cell, not a coulomb counter or charging detector.
struct Battery {
  int percent = -1;
  uint32_t millivolts = 0;
  uint32_t last = 0;
  void begin() {
#if LUCY_BATTERY_PIN >= 0
    pinMode(LUCY_BATTERY_PIN, INPUT);
    analogSetPinAttenuation(LUCY_BATTERY_PIN, ADC_11db);
    sample();
#endif
  }
  void sample() {
#if LUCY_BATTERY_PIN >= 0
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(LUCY_BATTERY_PIN);
    millivolts = sum / 8 * LUCY_BATTERY_DIVIDER_MILLI / 1000;
    if (millivolts < 2500 || millivolts > 4400) { percent = -1; return; }
    static const uint16_t mv[] = {3200, 3500, 3600, 3700, 3750, 3800, 3900, 4000, 4100, 4200};
    static const uint8_t pc[] = {0, 5, 10, 20, 35, 50, 65, 80, 90, 100};
    percent = 0;
    for (int i = 1; i < 10; i++) {
      if (millivolts >= mv[i]) percent = pc[i];
      else {
        if (millivolts > mv[i-1]) percent = pc[i-1] + (millivolts - mv[i-1]) * (pc[i] - pc[i-1]) / (mv[i] - mv[i-1]);
        break;
      }
    }
#endif
  }
  void tick(uint32_t now) { if (uint32_t(now - last) >= 10000) { last = now; sample(); } }
};
