#pragma once
#include <stdint.h>

// 24 recognizable behaviours, each with five motion performances = 120 clips.
// Only parameters live in flash; no sprite sheets, heap allocations or PSRAM.
namespace cat {
enum Behaviour : uint8_t {
  KNEAD, BUNT, LOAF, SLOW_BLINK, ZOOM, SCRATCH, FLEHMEN, PURR,
  MEOW, CHIRP, CHATTER, HISS, YOWL, TAIL_UP, PUFF, TUCK, TWITCH,
  AIRPLANE, BELLY, SLEEP, GROOM, STRETCH, POUNCE, WAVE, BEHAVIOURS
};
constexpr uint8_t VARIANTS = 5;
constexpr uint8_t CLIPS = BEHAVIOURS * VARIANTS;
static_assert(CLIPS >= 100, "Lucy needs 100+ animation performances");
static const char *const NAMES[] = {
  "Making biscuits", "Head bunt", "Loaf", "Slow blink", "Zoomies",
  "Scratching", "Flehmen", "Purring", "Meowing", "Chirping",
  "Chattering", "Hissing", "Yowling", "Happy tail", "Puffed up",
  "Tucked tail", "Tail twitch", "Airplane ears", "Belly roll",
  "Sleeping", "Grooming", "Stretching", "Pouncing", "Hello paw"
};
static const char *const VARIANT_NAMES[] = {
  "Gentle", "Bouncy", "Curious", "Dreamy", "Spirited"
};
inline const char *name(uint8_t id) { return NAMES[(id % CLIPS) / VARIANTS]; }
inline uint32_t duration(uint8_t id) {
  const uint8_t b = (id % CLIPS) / VARIANTS;
  return (b == SLEEP || b == LOAF ? 7000U : 4000U) + (id % VARIANTS) * 420U;
}
inline int wave(uint32_t time, uint16_t period, int amplitude) {
  const int p = int(time % period) * 4 * amplitude / period;
  return p < amplitude ? p : (p < 3 * amplitude ? 2 * amplitude - p : p - 4 * amplitude);
}
struct Player {
  uint8_t clip = 0;
  uint8_t offset = 0;
  uint8_t sequence = 0;
  uint32_t started = 0;
  bool automatic = true;
  void begin(uint32_t seed, uint32_t now) {
    offset = seed % CLIPS;
    sequence = 0;
    clip = offset;
    started = now;
  }
  void select(uint8_t id, uint32_t now) {
    clip = id % CLIPS;
    started = now;
    automatic = false;
  }
  bool tick(uint32_t now) {
    if (!automatic || uint32_t(now - started) < duration(clip)) return false;
    // 37 and 120 are coprime: visit every clip before repeating any.
    sequence = (sequence + 1) % CLIPS;
    clip = (offset + sequence * 37U) % CLIPS;
    started = now;
    return true;
  }
};

template<class D> void heart(D &d, int x, int y) {
  d.drawLine(x, y + 2, x + 3, y - 1);
  d.drawLine(x + 3, y - 1, x + 6, y + 2);
  d.drawLine(x + 6, y + 2, x + 3, y + 6);
  d.drawLine(x + 3, y + 6, x, y + 2);
}
template<class D> void tiny(D &d, int x, int y, bool paw) {
  d.drawRFrame(x - 10, y - 5, 21, 16, 5);
  d.drawLine(x - 10, y, x - 9, y - 11);
  d.drawLine(x - 9, y - 11, x - 3, y - 5);
  d.drawLine(x + 10, y, x + 9, y - 11);
  d.drawLine(x + 9, y - 11, x + 3, y - 5);
  d.drawDisc(x - 4, y + 1, 1);
  d.drawDisc(x + 4, y + 1, 1);
  d.drawLine(x - 2, y + 6, x, y + 7);
  d.drawLine(x, y + 7, x + 2, y + 6);
  if (paw) d.drawCircle(x + 14, y, 3);
}

template<class D> void draw(D &d, uint8_t id, uint32_t elapsed) {
  id %= CLIPS;
  const auto b = static_cast<Behaviour>(id / VARIANTS);
  const uint8_t v = id % VARIANTS;
  const uint16_t period = 1800 - v * 210;
  const int motion = wave(elapsed + v * 137U, period, 2 + v);
  int cx = 64, cy = 39, eyeH = 11;
  int gaze = (v == 2 ? motion : (int(v) - 2));
  int lean = 0, ear = 0, leftPaw = 0, rightPaw = 0, mouth = 0;
  bool closed = elapsed % (3900 + v * 170) < 130;
  bool body = false;
  switch (b) {
    case KNEAD: leftPaw = motion; rightPaw = -motion; closed = true; cy += motion / 3; break;
    case BUNT: cx += motion; lean = motion / 2; closed = true; break;
    case LOAF: cy += 3 + motion / 3; eyeH = 4; body = true; break;
    case SLOW_BLINK: {
      const uint32_t p = elapsed % (2400 + v * 180);
      eyeH = p < 1200 ? 11 - int(p * 10 / 1200) : 1 + int((p - 1200) * 10 / (1200 + v * 180));
      closed = eyeH <= 2; break;
    }
    case ZOOM: cx += wave(elapsed, 600 + v * 90, 13); cy += motion / 2; gaze = motion; break;
    case SCRATCH: leftPaw = -6 - motion; rightPaw = -6 + motion; gaze = -2; break;
    case FLEHMEN: mouth = 2; eyeH = 7; cy -= motion / 2; break;
    case PURR: cy += wave(elapsed, 220 + v * 35, 1 + v / 2); closed = true; break;
    case MEOW: mouth = (elapsed % period < period / 2) ? 1 : 0; cy += motion / 2; break;
    case CHIRP: mouth = (elapsed % 480 < 190) ? 1 : 0; lean = motion / 2; eyeH = 9; break;
    case CHATTER: mouth = 3; gaze = 3; cy += wave(elapsed, 160 + v * 20, 1); break;
    case HISS: mouth = 4; ear = 7 + v; eyeH = 5; break;
    case YOWL: mouth = 5; closed = true; cy -= motion / 2; break;
    case TAIL_UP: lean = motion / 3; break;
    case PUFF: ear = -2; eyeH = 13; cy -= 2; break;
    case TUCK: cy += 3; ear = 4; eyeH = 6; gaze = -motion / 2; body = true; break;
    case TWITCH: gaze = motion / 2; eyeH = 7; break;
    case AIRPLANE: ear = 9 + v; eyeH = 6; lean = motion / 2; break;
    case BELLY: cy -= 7; lean = motion / 2; leftPaw = -7 + motion; rightPaw = -7 - motion; body = true; break;
    case SLEEP: cy += 2 + motion / 2; eyeH = 1; closed = true; body = true; break;
    case GROOM: leftPaw = -10 + motion; mouth = 6; closed = true; lean = -2; break;
    case STRETCH: cy -= motion / 2; leftPaw = -2 - motion; rightPaw = -2 - motion; closed = true; break;
    case POUNCE: cy += motion; eyeH = 13; gaze = 0; leftPaw = rightPaw = -motion; break;
    case WAVE: rightPaw = -10 + motion; lean = motion / 2; break;
    default: break;
  }
  d.setDrawColor(1);
  // Tail is behind the face. Its silhouette changes with the behaviour.
  int tx = cx + 40;
  int ty = 43 + (b == TWITCH ? wave(elapsed, 340 + v * 45, 7) : motion / 2);
  if (b == TUCK) { d.drawEllipse(cx, 56, 27, 5); }
  else {
    d.drawLine(cx + 29, 54, tx + 2, 48);
    d.drawLine(tx + 2, 48, tx + 5, ty);
    d.drawLine(tx + 5, ty, tx + 3, ty - 10);
    d.drawLine(tx + 3, ty - 10, tx - 2, ty - 12);
    if (b == PUFF) for (int j = 0; j < 5; j++) d.drawLine(tx + 1, 33 + j * 3, tx + 8, 31 + j * 3);
  }
  if (body) {
    d.drawEllipse(cx, 53, 27, 10);
    if (b == BELLY) d.drawEllipse(cx, 53, 13 + v, 7);
  }
  // Cat outline and triangular ears; no giant opaque eye sprites.
  d.setDrawColor(0);
  d.drawRBox(cx - 31, cy - 14, 63, 32, 12); // hide body/tail behind the face
  d.setDrawColor(1);
  d.drawRFrame(cx - 31, cy - 14, 63, 32, 12);
  d.drawLine(cx - 29, cy - 4, cx - 27 - ear / 2, cy - 25 + ear);
  d.drawLine(cx - 27 - ear / 2, cy - 25 + ear, cx - 13, cy - 13);
  d.drawLine(cx + 29, cy - 4, cx + 27 + ear / 2, cy - 25 + ear);
  d.drawLine(cx + 27 + ear / 2, cy - 25 + ear, cx + 13, cy - 13);
  d.drawLine(cx - 24, cy - 10, cx - 24, cy - 16 + ear / 2);
  d.drawLine(cx + 24, cy - 10, cx + 24, cy - 16 + ear / 2);
  for (int side = -1; side <= 1; side += 2) {
    const int ex = cx + side * 13, ey = cy - 2 + side * lean;
    if (closed) {
      d.drawLine(ex - 6, ey, ex, ey + 2 + v % 2);
      d.drawLine(ex, ey + 2 + v % 2, ex + 6, ey);
    } else {
      d.drawRBox(ex - 6, ey - eyeH / 2, 13, eyeH, eyeH >= 6 ? 3 : eyeH / 2);
      d.setDrawColor(0);
      d.drawBox(ex - 1 + gaze / 2, ey - eyeH / 2 + 1, 3, eyeH > 4 ? eyeH - 2 : 1);
      d.setDrawColor(1);
      d.drawPixel(ex - 3, ey - 2);
    }
    d.drawLine(cx + side * 24, cy + 5, cx + side * 35, cy + 3);
    d.drawLine(cx + side * 24, cy + 8, cx + side * 35, cy + 10);
  }
  d.drawTriangle(cx - 2, cy + 4, cx + 2, cy + 4, cx, cy + 6);
  if (mouth == 1 || mouth == 5) {
    d.drawEllipse(cx, cy + 11, 3 + v % 2, mouth == 5 ? 5 : 3);
  } else if (mouth == 2 || mouth == 3 || mouth == 4) {
    const int mh = mouth == 3 ? 2 + (elapsed / 90) % 3 : 5;
    d.drawRFrame(cx - 6, cy + 8, 13, mh + 1, 1);
    if (mouth == 4) {
      d.drawLine(cx - 4, cy + 8, cx - 3, cy + 11);
      d.drawLine(cx + 4, cy + 8, cx + 3, cy + 11);
    }
  } else {
    d.drawLine(cx, cy + 6, cx, cy + 9);
    d.drawLine(cx, cy + 9, cx - 4, cy + 11);
    d.drawLine(cx, cy + 9, cx + 4, cy + 11);
    if (mouth == 6) d.drawLine(cx - 3, cy + 11, cx - 5, cy + 15);
  }
  if (b != LOAF && b != SLEEP && b != TUCK) {
    const int py1 = 59 + leftPaw / 2 > 60 ? 60 : 59 + leftPaw / 2;
    const int py2 = 59 + rightPaw / 2 > 60 ? 60 : 59 + rightPaw / 2;
    d.drawRFrame(cx - 24, py1 - 3, 16, 7, 3);
    d.drawRFrame(cx + 8, py2 - 3, 16, 7, 3);
    for (int i = 0; i < 3; i++) {
      d.drawVLine(cx - 21 + i * 4, py1, 2);
      d.drawVLine(cx + 11 + i * 4, py2, 2);
    }
  }
  if (b == BUNT || b == SLOW_BLINK || b == PURR) heart(d, 10, 23 + motion / 2);
  if (b == SLEEP) {
    d.drawLine(100, 19, 106, 19); d.drawLine(106, 19, 100, 25); d.drawLine(100, 25, 106, 25);
    d.drawLine(111, 13, 116, 13); d.drawLine(116, 13, 111, 18); d.drawLine(111, 18, 116, 18);
  }
  if (b == ZOOM) for (int i = 0; i < 3; i++) d.drawHLine(cx - 47, 32 + i * 8, 7 + v);
  if (b == SCRATCH) for (int i = 0; i < 3; i++) d.drawLine(12 + i * 4, 46, 9 + i * 4, 55 + motion / 2);
  if (b == PUFF) for (int i = -2; i <= 2; i++) d.drawLine(cx + i * 5, cy - 15, cx + i * 5 + 2, cy - 19);
}

template<class D> void battery(D &d, int percent) {
  d.drawRFrame(107, 0, 19, 10, 4);
  d.drawVLine(127, 3, 4);
  if (percent < 0) {
    d.drawHLine(114, 3, 4); d.drawPixel(118, 4); d.drawPixel(116, 5); d.drawPixel(116, 7);
  } else {
    const int w = (percent * 13 + 99) / 100;
    if (w) d.drawRBox(110, 3, w, 4, w < 3 ? 0 : 1);
  }
}
template<class D> void network(D &d, bool connected, int rssi, bool ap) {
  if (connected) {
    int bars = rssi > -55 ? 4 : (rssi > -67 ? 3 : (rssi > -78 ? 2 : 1));
    for (int i = 0; i < 4; i++) {
      if (i < bars) d.drawBox(2 + i * 3, 9 - i * 2, 2, 1 + i * 2);
      else d.drawPixel(2 + i * 3, 9);
    }
  } else if (ap) {
    d.drawCircle(7, 5, 4); d.drawDisc(7, 5, 1);
  } else {
    d.drawLine(3, 2, 11, 9); d.drawLine(11, 2, 3, 9);
  }
}
} // namespace cat
