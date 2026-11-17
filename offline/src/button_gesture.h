#pragma once
#include <stdint.h>
enum class Gesture : uint8_t { NEXT, SELECT, BACK };
inline Gesture buttonGesture(uint32_t heldMs, bool home) {
  if (home) return Gesture::SELECT;
  if (heldMs >= 1800) return Gesture::BACK;
  return heldMs >= 650 ? Gesture::SELECT : Gesture::NEXT;
}
