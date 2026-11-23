// --------------------------------------------------
// Finale surprises: one random animation at the end of every morning.
//
// Replaces the fixed smiling sun. Common ones come up most mornings, rare ones about once every
// two weeks, the jackpot about once every two months. The same surprise
// never plays two mornings in a row.
//
// Every surprise runs for SURPRISE_MS and is drawn from the time `t` (ms)
// since it started, so motion stays smooth even if a frame is late.
// Not part of the Claude Design set — drawn in the same style.
// No Arduino dependencies (compiles on a desktop for previews).
// --------------------------------------------------

#pragma once

#include "oled_gfx.h"

namespace oled {

enum Surprise : uint8_t {
  // Common
  SUR_ROCKET, SUR_FIREWORKS, SUR_DANCE, SUR_RAINBOW,
  SUR_BALLOONS, SUR_FISH, SUR_SPACE, SUR_RACECAR, SUR_TRAIN,
  // Rare
  SUR_UFO, SUR_VOLCANO, SUR_SKATE,
  // Super rare
  SUR_JACKPOT,
  NUM_SURPRISES
};

constexpr uint32_t SURPRISE_MS = 10000;
// Story surprises (rocket, race car, train, UFO, skate, rainbow) are drawn
// on a 6 s timeline, played slower so the story fills SURPRISE_MS.
inline uint32_t storyT(uint32_t t) { return (uint64_t)t * 6000 / SURPRISE_MS; }
constexpr uint32_t SURPRISE_FRAME_MS = 100;  // one redraw every 100 ms

// Relative chance of each surprise. With these weights: each common one
// ~9.5 %, each rare one ~3.8 %, the jackpot ~2.9 %.
static const uint8_t SURPRISE_WEIGHT[NUM_SURPRISES] = {
  10, 10, 10, 10, 10, 10, 10, 10, 10,
  4, 4, 4,
  3,
};

inline const char* surpriseName(int s) {
  static const char* const NAMES[NUM_SURPRISES] = {
    "rocket", "fireworks", "dino dance", "rainbow",
    "balloons", "fish", "space", "race car", "train",
    "UFO (rare)", "volcano (rare)", "skate dino (rare)",
    "JACKPOT (super rare)",
  };
  return s >= 0 && s < NUM_SURPRISES ? NAMES[s] : "?";
}

// Weighted random pick from a random number `r`, never `last`.
inline int pickSurprise(uint32_t r, int last) {
  uint32_t total = 0;
  for (int i = 0; i < NUM_SURPRISES; i++)
    if (i != last) total += SURPRISE_WEIGHT[i];
  r %= total;
  for (int i = 0; i < NUM_SURPRISES; i++) {
    if (i == last) continue;
    if (r < SURPRISE_WEIGHT[i]) return i;
    r -= SURPRISE_WEIGHT[i];
  }
  return SUR_ROCKET;
}

// --------------------------------------------------
// Helpers
// --------------------------------------------------

inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline double lerp(double a, double b, double p) { return a + (b - a) * p; }
inline double easeOut(double p) { return 1 - (1 - p) * (1 - p); }

// Draws the lit pixels of `src` at (x, y), optionally mirrored and scaled.
// clearEdge: first clears a 1 px border around them, so the sprite stays
// readable on top of other drawing.
inline void blitEx(Bm& b, const Bm& src, double x, double y, int scale = 1,
                   bool flip = false, bool clearEdge = false) {
  x = jsRound(x);
  y = jsRound(y);
  for (int pass = clearEdge ? 0 : 1; pass < 2; pass++)
    for (int j = 0; j < src.h; j++)
      for (int i = 0; i < src.w; i++) {
        if (!src.get(i, j)) continue;
        int sx = flip ? src.w - 1 - i : i;
        if (pass == 0) b.rect(x + sx * scale - 1, y + j * scale - 1, scale + 2, scale + 2, 0);
        else b.rect(x + sx * scale, y + j * scale, scale, scale, 1);
      }
}

// Today's dino (as on the dino screen), feet at (cx, bottom).
inline void dinoEx(Bm& b, int mornings, double cx, double bottom, int scale,
                   bool flip = false, bool clearEdge = false) {
  const DinoSprite& s = DINO_SPRITES[dinoStage(mornings)];
  Bm spr((int)strlen(s.rows[0]), s.n);
  spr.sprite(s.rows, s.n, 0, 0);
  blitEx(b, spr, cx - floor(spr.w * scale / 2.0), bottom - spr.h * scale, scale, flip, clearEdge);
}

inline int dinoHeight(int mornings, int scale) {
  return DINO_SPRITES[dinoStage(mornings)].n * scale;
}

static const uint8_t STARFIELD[12][2] = {
  { 6, 22 }, { 18, 8 }, { 30, 32 }, { 44, 14 }, { 58, 4 }, { 76, 20 },
  { 88, 7 }, { 102, 24 }, { 116, 11 }, { 122, 36 }, { 10, 46 }, { 112, 50 },
};

inline void starfield(Bm& b, uint32_t t) {
  for (int i = 0; i < 12; i++) {
    if ((i + t / 400) % 5 == 0) sparkle(b, STARFIELD[i][0], STARFIELD[i][1], 1);
    else b.set(STARFIELD[i][0], STARFIELD[i][1]);
  }
}

// Star with rotation `rot` (radians).
inline void starRot(Bm& b, double cx, double cy, double R, double r, double rot, int v = 1) {
  double pts[10][2];
  for (int i = 0; i < 10; i++) {
    double a = -PI_D / 2 + rot + i * PI_D / 5, rr = i % 2 ? r : R;
    pts[i][0] = cx + cos(a) * rr;
    pts[i][1] = cy + sin(a) * rr;
  }
  b.poly(pts, 10, v);
}

// Wheel with a black rim gap and one turning spoke.
inline void wheel(Bm& b, double cx, double cy, double r, double angle) {
  b.disc(cx, cy, r + 1, 0);
  b.disc(cx, cy, r);
  b.disc(cx, cy, r * 0.35, 0);
  b.line(cx + cos(angle) * (r - 1), cy + sin(angle) * (r - 1),
         cx - cos(angle) * (r - 1), cy - sin(angle) * (r - 1), 0);
}

// --------------------------------------------------
// 2. Rocket: countdown, lift-off, lands on the moon and plants a flag.
// --------------------------------------------------

static const char* const SPR_ROCKET[16] = {
  ".....#.....",
  "....###....",
  "...#####...",
  "...#####...",
  "..###.###..",
  "..##...##..",
  "..###.###..",
  "..#######..",
  "..#######..",
  "..#######..",
  "..#######..",
  "..#######..",
  ".#########.",
  "##.#####.##",
  "#..#####..#",
  "#...###...#",
};
static const char* const SPR_FLAME[2][5] = {
  { "...#####...", "....###....", "...#.#.#...", ".....#.....", "..........." },
  { "...#####...", "...#.#.#...", "....###....", "....#.#....", ".....#....." },
};

inline void surpriseRocket(Bm& b, uint32_t t, const Opts&) {
  t = storyT(t);
  starfield(b, t);
  double x = 58, y;
  bool flame = false;

  if (t < 3000) {
    b.rect(0, 60, 128, 4);  // launch ground
    if (t < 1200) {
      y = 44;
      if (t > 600) x += (t / 50) % 2;  // shaking
      flame = t > 800;
    } else {
      double dt = (t - 1200) / 1000.0;
      y = 44 - 22 * dt * dt;
      flame = true;
    }
    // Smoke clouds on the pad, growing then gone.
    if (t > 500 && t < 2400) {
      double r = fmin(5, 1.5 + (t - 500) / 200.0), spread = r * 1.2;
      static const double SIDES[2][2] = { { 50, -1 }, { 78, 1 } };
      for (auto& sd : SIDES) {
        double sx = sd[0] + sd[1] * (r - 1.5);
        b.disc(sx - spread, 57, r * 0.8);
        b.disc(sx, 57 - r * 0.7, r);
        b.disc(sx + spread, 57, r * 0.8);
        b.disc(sx, 57 - r * 0.7, r * 0.35, 0);
      }
    }
  } else {
    // Moon surface, rocket lands slowly with its flame on.
    b.disc(64, 120, 62);
    b.disc(30, 64, 3, 0);
    b.disc(98, 63, 2.5, 0);
    b.disc(46, 60, 1.5, 0);
    double p = clamp01((t - 3000) / 1600.0);
    y = lerp(-20, 42, easeOut(p));
    flame = p < 1;
    if (t > 4800) {
      double h = fmin(18, (t - 4800) / 20.0);
      b.rect(86, 60 - h, 1, h);
      if (h >= 18) {
        int wave = (t / 200) % 2;
        b.rect(87, 42 + wave, 9, 6);
        b.rect(90, 44 + wave, 3, 2, 0);
        sparkle(b, 104, 30, (t / 300) % 2 + 1);
        sparkle(b, 24, 34, 2 - (t / 300) % 2);
      }
    }
  }

  if (flame) b.sprite(SPR_FLAME[(t / 80) % 2], 5, x, y + 16);
  b.sprite(SPR_ROCKET, 16, x, y);
}

// --------------------------------------------------
// 3. Fireworks over a little city.
// --------------------------------------------------

static const int16_t FIREWORKS[9][3] = {  // x, y, start ms
  { 30, 24, 0 }, { 94, 20, 600 }, { 62, 30, 1300 }, { 40, 34, 2200 }, { 92, 30, 2800 },
  { 64, 20, 3500 }, { 22, 28, 4100 }, { 104, 26, 4500 }, { 64, 28, 5000 },
};
static const uint8_t SKYLINE[9][3] = {  // x, width, height
  { 0, 14, 10 }, { 15, 10, 16 }, { 26, 16, 8 }, { 43, 8, 20 }, { 52, 18, 12 },
  { 71, 10, 18 }, { 82, 16, 9 }, { 99, 12, 14 }, { 112, 16, 11 },
};

inline void surpriseFireworks(Bm& b, uint32_t t, const Opts&) {
  for (int n = 0; n < 18; n++) {  // the 6 s show, repeated
    int i = n % 9;
    int32_t dt = (int32_t)t - FIREWORKS[i][2] - (n / 9) * 6000;
    if (dt < 0) continue;
    double x = FIREWORKS[i][0], y = FIREWORKS[i][1];
    if (dt < 400) {
      double yy = lerp(54, y, dt / 400.0);
      b.set(x, yy);
      b.set(x, yy + 1);
      b.set(x, yy + 3);
    } else if (dt < 1600) {
      double p = (dt - 400) / 1200.0;
      double r = 3 + 17 * sqrt(p), drop = 8 * p * p;
      if (p < 0.12) sparkle(b, x, y, 3);
      for (int k = 0; k < 12; k++) {
        if (p > 0.6 && (k + t / 100) % 2) continue;  // fading out
        double a = k * PI_D / 6 + i * 0.3;
        b.rect(x + cos(a) * r, y + sin(a) * r + drop, 2, 1);
        if (p < 0.5) b.set(x + cos(a) * (r - 3), y + sin(a) * (r - 3) + drop);
        if (k % 2 == 0) b.set(x + cos(a + 0.26) * r * 0.55, y + sin(a + 0.26) * r * 0.55 + drop);
      }
    }
  }
  // City in front, windows twinkle.
  for (int i = 0; i < 9; i++) {
    int x = SKYLINE[i][0], w = SKYLINE[i][1], h = SKYLINE[i][2];
    b.rect(x, 64 - h, w, h);
    for (int wy = 64 - h + 2; wy < 61; wy += 4)
      for (int wx = x + 2; wx < x + w - 2; wx += 4)
        if ((wx * 7 + wy * 3 + i + t / 700) % 3) b.rect(wx, wy, 2, 2, 0);
  }
}

// --------------------------------------------------
// 4. Dino dance under a disco ball.
// --------------------------------------------------

static const char* const SPR_NOTE[6] = { "..###", "..#.#", "..#..", "..#..", "###..", "###.." };

inline void surpriseDance(Bm& b, uint32_t t, const Opts& o) {
  int beat = t / 300;
  // Disco ball with turning light rays.
  b.line(64, 0, 64, 3);
  b.disc(64, 8, 5);
  b.rect(59, 8, 11, 1, 0);
  b.rect(62 + beat % 3, 3, 1, 11, 0);
  for (int k = 0; k < 7; k++) {
    if ((k + beat) % 2) continue;
    double a = PI_D / 8 + k * PI_D / 8;
    b.line(64 + cos(a) * 8, 8 + sin(a) * 8, 64 + cos(a) * 14, 8 + sin(a) * 14);
  }
  // Dancing dino: hops on every beat, turns every two.
  bool flip = (beat / 2) % 2;
  dinoEx(b, o.mornings, 64, beat % 2 ? 61 : 63, 2, flip);
  // Music notes floating up on both sides.
  for (int k = 0; k < 4; k++) {
    uint32_t ph = (t + k * 400) % 1600;
    double nx = (k % 2 ? 104 : 16) + (k / 2) * 6 + 3 * sin(ph / 200.0);
    double ny = 52 - ph * 36 / 1600.0;
    b.sprite(SPR_NOTE, 6, nx, ny);
  }
}

// --------------------------------------------------
// 5. Rainbow draws itself from cloud to cloud.
// --------------------------------------------------

inline void cloud(Bm& b, double cx, double cy) {
  static const double PUFFS[4][3] = { { -9, 1, 4 }, { -3, -3, 6 }, { 5, -2, 5 }, { 10, 2, 3.5 } };
  for (auto& p : PUFFS) b.disc(cx + p[0], cy + p[1], p[2] + 1, 0);
  b.rect(cx - 13, cy + 1, 27, 6, 0);
  for (auto& p : PUFFS) b.disc(cx + p[0], cy + p[1], p[2]);
  b.rrect(cx - 12, cy + 1, 25, 5, 2);
}

inline void surpriseRainbow(Bm& b, uint32_t t, const Opts&) {
  t = storyT(t);
  const double cx = 64, cy = 60;
  const double sweep = PI_D * clamp01(t / 1800.0);
  static const double BANDS[3][2] = { { 41, 47 }, { 32, 38 }, { 23, 29 } };
  b.shape([&](double x, double y) {
    if (y > cy) return false;
    double dx = x - cx, dy = cy - y, d = sqrt(dx * dx + dy * dy);
    int band = -1;
    for (int i = 0; i < 3; i++)
      if (d >= BANDS[i][0] && d < BANDS[i][1]) band = i;
    if (band < 0) return false;
    if (band == 1 && ((int)x + (int)y) % 2) return false;  // middle band dithered
    return atan2(dy, dx) >= PI_D - sweep - 1e-9;
  }, cx - 48, cy - 48, cx + 48, cy);

  cloud(b, 20, 54);
  if (t >= 1800) {
    cloud(b, 108, 54);
    int f = (t / 300) % 2;
    sparkle(b, 64, 6, f ? 1 : 2);
    sparkle(b, 22, 22, f ? 2 : 1);
    sparkle(b, 106, 20, f ? 1 : 2);
  }
}

// --------------------------------------------------
// 6. Balloons float up past a waving kid.
// --------------------------------------------------

static const uint8_t BALLOONS[5][4] = {  // x, speed px/s, start offset, filled
  { 14, 20, 0, 1 }, { 38, 26, 22, 0 }, { 64, 17, 8, 1 }, { 90, 23, 30, 0 }, { 114, 19, 4, 1 },
};

inline void surpriseBalloons(Bm& b, uint32_t t, const Opts&) {
  for (int i = 0; i < 5; i++) {
    double rise = BALLOONS[i][1] * t / 1000.0 + BALLOONS[i][2];
    double y = 78 - fmod(rise, 104);
    double x = BALLOONS[i][0] + 3 * sin(t / 450.0 + i * 1.7);
    bool filled = BALLOONS[i][3];
    b.shape([&](double px, double py) {
      double ex = (px - x) / 6, ey = (py - y) / 7.5, e = ex * ex + ey * ey;
      return filled ? e <= 1 : e <= 1 && e > 0.6;
    }, x - 7, y - 9, x + 7, y + 9);
    if (filled) b.disc(x - 2, y - 3, 1.3, 0);
    const double knot[3][2] = { { x, y + 7 }, { x - 2, y + 10 }, { x + 2, y + 10 } };
    b.poly(knot, 3);
    for (int j = 0; j < 12; j++) b.set(x + sin(j * 0.8 + t / 150.0) * 1.2, y + 10 + j);
  }
  b.rect(0, 63, 128, 1);
  b.rect(59, 50, 11, 13, 0);
  b.sprite(SPR_KID[(t / 300) % 2 ? 0 : 2], 11, 61, 52);
}

// --------------------------------------------------
// 7. Fish: a big one and two small ones, bubbles and seaweed.
// --------------------------------------------------

static const char* const FISH_BODY[7] = {
  "..####....", ".######...", "#########.", "##########", "#########.", ".######...", "..####....",
};
static const char* const FISH_TAIL[2][7] = {
  { "#..", "##.", "###", ".##", "###", "##.", "#.." },
  { "...", "#..", "##.", "###", "##.", "#..", "..." },
};

// Fish facing right (flip = facing left), top-left at (x, y).
inline void fish(Bm& b, double x, double y, bool flip, int f, int scale) {
  Bm spr(13, 7);
  spr.sprite(FISH_BODY, 7, 3, 0);
  spr.sprite(FISH_TAIL[f], 7, 0, 0);
  spr.set(10, 2, 0);  // eye
  spr.set(12, 3, 0);  // mouth
  blitEx(b, spr, x, y, scale, flip, true);
}

inline void surpriseFish(Bm& b, uint32_t t, const Opts&) {
  // Seaweed swaying.
  static const uint8_t WEED[4][2] = { { 8, 22 }, { 30, 14 }, { 98, 18 }, { 120, 24 } };
  for (int w = 0; w < 4; w++)
    for (int j = 0; j < WEED[w][1]; j++)
      b.rect(WEED[w][0] + 2.2 * sin(t / 600.0 + j * 0.3 + w) * j / WEED[w][1], 63 - j, 2, 1);
  for (int x = 0; x < 128; x += 3) b.set(x, 63);
  // Bubbles.
  static const uint8_t BX[6] = { 20, 48, 72, 102, 34, 88 };
  for (int k = 0; k < 6; k++) {
    double y = 66 - fmod(t * (14 + k * 3) / 1000.0 + k * 13, 62);
    b.ring(BX[k] + 1.5 * sin(t / 250.0 + k), y, 1.6 + (k % 3) * 0.6, 1);
  }
  int f = (t / 200) % 2;
  fish(b, 130 - fmod(30 * t / 1000.0, 190), 22 + 3 * sin(t / 500.0), true, f, 2);   // big, right to left
  fish(b, fmod(t * 40 / 1000.0, 160) - 16, 46, false, 1 - f, 1);
  fish(b, fmod(t * 26 / 1000.0 + 80, 160) - 16, 14, false, f, 1);
}

// --------------------------------------------------
// 8. Space: ringed planet with a moon, shooting stars, floating astronaut.
// --------------------------------------------------

static const int16_t SHOOTING[4][3] = { { 124, 2, 300 }, { 110, 8, 2100 }, { 126, 18, 3600 }, { 92, 0, 4800 } };

inline void surpriseSpace(Bm& b, uint32_t t, const Opts&) {
  starfield(b, t);
  const double cx = 44, cy = 38, r = 11;
  auto ringBand = [&](double x, double y, double lo, double hi) {
    double ex = (x - cx) / 24, ey = (y - cy) / 6.5, e = ex * ex + ey * ey;
    return e >= lo && e <= hi;
  };
  double ma = t / 900.0, mx = cx + 27 * cos(ma), my = cy + 8 * sin(ma);
  bool moonBehind = sin(ma) < 0;

  if (moonBehind) b.disc(mx, my, 2.5);
  b.shape([&](double x, double y) { return y < cy && ringBand(x, y, 0.62, 1); }, cx - 25, cy - 8, cx + 25, cy);
  b.disc(cx, cy, r + 1, 0);
  b.disc(cx, cy, r);
  b.shape([&](double x, double y) {  // stripes
    double dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r && ((int)(y - cy + 20)) % 6 == 0;
  }, cx - r, cy - r, cx + r, cy + r, 0);
  double sa = t / 700.0;
  if (cos(sa) > 0) b.disc(cx + 7 * sin(sa), cy - 2, 2, 0);  // turning spot
  b.shape([&](double x, double y) { return y >= cy && ringBand(x, y, 0.45, 1.2); }, cx - 26, cy, cx + 26, cy + 8, 0);
  b.shape([&](double x, double y) { return y >= cy && ringBand(x, y, 0.62, 1); }, cx - 25, cy, cx + 25, cy + 8);
  if (!moonBehind) {
    b.disc(mx, my, 3.5, 0);
    b.disc(mx, my, 2.5);
  }

  for (auto& s : SHOOTING) {
    int32_t dt = (int32_t)(t % 6000) - s[2];
    if (dt < 0 || dt >= 700) continue;
    double p = dt / 700.0, hx = s[0] - 80 * p, hy = s[1] + 40 * p;
    b.line(hx, hy, hx + 10, hy - 5);
    sparkle(b, hx, hy, 1);
  }

  // Astronaut kid floating in a helmet.
  double ax = 98, ay = 30 + 3 * sin(t / 600.0);
  b.rect(ax - 2, ay - 2, 12, 16, 0);
  b.sprite(SPR_KID[0], 11, ax, ay);
  b.ring(ax + 3.5, ay + 2.5, 5, 1);
}

// --------------------------------------------------
// 9. Race car: drives in, revs, zooms off, chequered flag.
// --------------------------------------------------

inline void raceCar(Bm& b, double x, double y, uint32_t t) {
  b.rrect(x, y + 6, 32, 7, 2);
  const double cabin[4][2] = { { x + 8, y + 6 }, { x + 12, y }, { x + 22, y }, { x + 26, y + 6 } };
  b.poly(cabin, 4);
  b.rect(x + 13, y + 2, 4, 3, 0);
  b.rect(x + 19, y + 2, 4, 3, 0);
  b.rect(x, y + 2, 2, 4);
  b.rect(x - 1, y + 1, 5, 2);
  b.text("7", x + 14, y + 6, 1, 0);
  double a = t / 40.0;
  wheel(b, x + 7, y + 13, 4, a);
  wheel(b, x + 25, y + 13, 4, a);
}

inline void surpriseRaceCar(Bm& b, uint32_t t, const Opts&) {
  t = storyT(t);
  b.rect(0, 60, 128, 1);
  int off = (t / 8) % 16;
  for (int x = -off; x < 128; x += 16) b.rect(x, 63, 8, 1);

  double x, y = 42;
  if (t < 1500) {
    x = lerp(-34, 48, easeOut(t / 1500.0));
  } else if (t < 4000) {
    x = 48;
    y -= (t / 80) % 2;
  } else {
    double p = (t - 4000) / 700.0;
    x = 48 + 250 * p * p;
  }
  if (t >= 1000 && x < 128) {
    for (int k = 0; k < 3; k++) {
      int len = 6 + ((t / 100 + k) % 3) * 4;
      b.rect(x - 4 - len, y + 3 + k * 4, len, 1);
    }
  }
  if (x < 140) raceCar(b, x, y, t);

  if (t > 4700) {
    // Chequered flag waving.
    b.rect(50, 18, 2, 42);
    for (int k = 0; k < 6; k++) {
      double off2 = 2 * sin(t / 120.0 + k * 0.8);
      for (int r = 0; r < 4; r++)
        b.rect(52 + k * 4, 18 + r * 4 + off2, 4, 4, (r + k) % 2 == 0);
      b.rect(52 + k * 4, 17 + off2, 4, 1);
      b.rect(52 + k * 4, 34 + off2, 4, 1);
    }
    sparkle(b, 96, 22, (t / 250) % 2 + 1);
    sparkle(b, 30, 30, 2 - (t / 250) % 2);
  }
}

// --------------------------------------------------
// 10. Steam train crosses with puffs of smoke.
// --------------------------------------------------

constexpr double TRAIN_SPEED = 34;  // px/s
constexpr double TRAIN_X0 = 118;    // engine front at t = 0

inline void surpriseTrain(Bm& b, uint32_t t, const Opts&) {
  t = storyT(t);
  // Smoke: one puff every 220 ms from where the chimney was.
  for (int k = 0; k * 220 <= (int)t; k++) {
    double a = (t - k * 220) / 1000.0;
    if (a > 1.6) continue;
    double ex = TRAIN_X0 - TRAIN_SPEED * k * 0.22 + 9, px = ex + a * 14, py = 28 - a * 16;
    double r = 2 + a * 4;
    if (a < 0.3) b.disc(px, py, r);
    else b.ring(px, py, r, 1.2);
  }

  double x0 = TRAIN_X0 - TRAIN_SPEED * t / 1000.0, wa = -t / 60.0;
  b.rect(0, 60, 128, 1);
  for (int x = 0; x < 128; x += 6) b.rect(x, 61, 3, 2);

  // Engine (facing left).
  b.rect(x0 + 7, 32, 5, 8);
  b.rect(x0 + 6, 30, 7, 2);
  b.rrect(x0 + 4, 40, 20, 12, 3);
  b.disc(x0 + 17, 40, 3);
  b.rect(x0 + 22, 32, 12, 20);
  b.rect(x0 + 25, 35, 6, 6, 0);
  b.rect(x0 + 20, 30, 16, 2);
  const double catcher[3][2] = { { x0 + 4, 46 }, { x0 + 4, 58 }, { x0 - 3, 58 } };
  b.poly(catcher, 3);
  b.disc(x0 + 2, 43, 1.5);
  b.rect(x0 + 2, 50, 34, 3);
  wheel(b, x0 + 10, 55, 4, wa);
  wheel(b, x0 + 20, 55, 4, wa);
  wheel(b, x0 + 30, 56, 3, wa);

  // Two wagons, a kid in a window of the first.
  for (int k = 0; k < 2; k++) {
    double xw = x0 + 38 + k * 30;
    b.rect(xw - 3, 50, 3, 1);
    b.rect(xw, 38, 26, 13);
    b.rect(xw - 1, 36, 28, 2);
    for (int w = 0; w < 3; w++) b.rect(xw + 3 + w * 7, 41, 5, 5, 0);
    if (k == 0) b.sprite(SPR_KID[0], 4, xw + 3, 42, 1);
    wheel(b, xw + 5, 56, 3, wa);
    wheel(b, xw + 21, 56, 3, wa);
  }
}

// --------------------------------------------------
// 11. (rare) UFO beams the dino up, then drops it back by parachute.
// --------------------------------------------------

inline void ufo(Bm& b, double x, double y, uint32_t t) {
  b.shape([&](double px, double py) {
    double dd = sqrt((px - x) * (px - x) + (py - y) * (py - y));
    return py < y && dd <= 7;
  }, x - 8, y - 8, x + 8, y);
  b.disc(x, y - 4, 2, 0);
  b.shape([&](double px, double py) {
    double ex = (px - x) / 17, ey = (py - y - 2) / 4.5;
    return ex * ex + ey * ey <= 1;
  }, x - 18, y - 3, x + 18, y + 7);
  for (int k = -2; k <= 2; k++)
    if ((k + 2 + t / 150) % 2) b.disc(x + k * 6, y + 2, 1.2, 0);
}

inline void parachute(Bm& b, double cx, double top) {
  double cy = top - 9;
  b.shape([&](double x, double y) {
    double dx = x - cx, dy = y - cy;
    return y <= cy && dx * dx + dy * dy <= 100;
  }, cx - 11, cy - 11, cx + 11, cy);
  for (int k = -1; k <= 1; k++) b.disc(cx + k * 7, cy + 1, 3, 0);
  b.line(cx - 10, cy, cx - 3, top);
  b.line(cx + 10, cy, cx + 3, top);
}

inline void surpriseUfo(Bm& b, uint32_t t, const Opts& o) {
  t = storyT(t);
  starfield(b, t);
  b.rect(0, 62, 128, 2);
  const int h = dinoHeight(o.mornings, 1);
  double ux = 64, uy = 14 + 2 * sin(t / 200.0);

  if (t < 1500) {
    ux = lerp(-24, 64, easeOut(t / 1500.0));
    bool scared = t > 800;
    dinoEx(b, o.mornings, 64, scared && (t / 150) % 2 ? 60 : 62, 1);
  } else if (t < 3600) {
    // Beam, then the dino floats up into the saucer.
    int ph = (t / 100) % 2;
    b.shape([&](double px, double py) {
      double top = uy + 7, half = lerp(6, 18, (py - top) / (62 - top));
      return py >= top && py < 62 && fabs(px - ux) <= half && ((int)px + (int)py + ph) % 2 == 0;
    }, ux - 19, uy + 7, ux + 19, 62);
    double p = clamp01((t - 2000) / 1400.0);
    if (p < 1) dinoEx(b, o.mornings, 64, lerp(62, uy + 8 + h * 0.5, p), 1, false, true);
  } else if (t < 4300) {
    double q = (t - 3600) / 700.0;
    ux = 64 + 120 * q * q;
  } else {
    // Back again, drops the dino with a parachute, flies off.
    if (t < 4800) ux = lerp(-24, 64, easeOut((t - 4300) / 500.0));
    else {
      ux = 64 + (t - 4800) * 0.12;
      uy -= (t - 4800) * 0.02;
    }
    if (t >= 4800) {
      double p = clamp01((t - 4800) / 1100.0);
      double bottom = lerp(uy + 8 + h, 62, p);
      double dx = 64 + 4 * sin(t / 250.0) * (1 - p);
      dinoEx(b, o.mornings, dx, bottom, 1, false, true);
      if (p < 1) parachute(b, dx, bottom - h);
    }
  }
  ufo(b, ux, uy, t);
}

// --------------------------------------------------
// 12. (rare) Volcano rumbles, erupts; the dino hops at a safe distance.
// --------------------------------------------------

inline void surpriseVolcano(Bm& b, uint32_t t, const Opts& o) {
  int sh = (t > 300 && t < 1300) ? (t / 60) % 2 : 0;
  const double cone[4][2] = { { 14, 64 }, { 50.0 + sh, 28 }, { 70.0 + sh, 28 }, { 106, 64 } };
  b.poly(cone, 4);
  const double crater[4][2] = { { 53.0 + sh, 28 }, { 67.0 + sh, 28 }, { 64.0 + sh, 31 }, { 56.0 + sh, 31 } };
  b.poly(crater, 4, 0);

  if (t < 1300) {
    // Rumbling smoke.
    for (int k = 0; k < 3; k++) {
      double a = fmod(t / 900.0 + k / 3.0, 1.0);
      b.disc(60 + sh + 2 * sin(a * 6 + k), 25 - a * 18, 1 + a * 2.5);
    }
  } else {
    double p = clamp01((t - 1500) / 2000.0);
    // Lava rivers down both sides (dark on the white cone).
    if (p > 0) {
      b.line(55, 31, lerp(55, 32, p), lerp(31, 62, p), 0);
      b.line(56, 31, lerp(56, 33, p), lerp(31, 62, p), 0);
      b.line(65, 31, lerp(65, 88, p), lerp(31, 62, p), 0);
      b.line(64, 31, lerp(64, 87, p), lerp(31, 62, p), 0);
    }
    if (t < 1700) {
      b.disc(60, 20, (t - 1300) / 25.0);
      b.disc(60, 20, (t - 1300) / 25.0 - 3, 0);
    }
    // Lava blobs flying out.
    for (int k = 0; k * 160 + 1300 <= (int)t; k++) {
      double a = (t - 1300 - k * 160) / 1000.0;
      double vx = (k * 37) % 70 - 35, vy = -(70 + (k * 23) % 40);
      double x = 60 + vx * a, y = 26 + vy * a + 70 * a * a;
      if (y > 66) continue;
      b.disc(x, y, 3, 0);
      b.disc(x, y, 2);
    }
  }
  bool hop = t > 1300 && (t / 150) % 2;
  dinoEx(b, o.mornings, 116, hop ? 61 : 64, 1, true, true);
}

// --------------------------------------------------
// 13. (rare) Skateboard dino jumps the ramp and scores 100.
// --------------------------------------------------

inline void surpriseSkate(Bm& b, uint32_t t, const Opts& o) {
  t = storyT(t);
  b.rect(0, 62, 128, 2);
  const double ramp[3][2] = { { 50, 62 }, { 74, 62 }, { 74, 50 } };
  b.poly(ramp, 3);

  double x = -30 + 190 * t / 5200.0, lift = 0;
  if (x >= 50 && x < 74) lift = (x - 50) / 2;
  else if (x >= 74) {
    double u = x - 74;
    lift = fmax(0, 12 + u * 0.9 - u * u * 0.024);
  }
  double feet = 57 - lift;
  if (x < 150) {
    if (lift == 0 && t > 300) {
      for (int k = 0; k < 3; k++) b.rect(x - 22 - k * 3, feet - 6 - k * 5, 6 + k * 2, 1);
    }
    dinoEx(b, o.mornings, x, feet, 2, false, true);
    b.rrect(x - 13, feet, 26, 3, 1);
    b.disc(x - 8, feet + 4, 1.8);
    b.disc(x + 8, feet + 4, 1.8);
    if (lift > 12) {
      sparkle(b, x - 20, feet - 10, (t / 100) % 2 + 1);
      sparkle(b, x + 18, feet + 2, 2 - (t / 100) % 2);
    }
  }
  if (t > 4300) {
    int s = t < 4500 ? 3 : 2;  // pops in big, then settles
    const char* score = "100";
    b.text(score, 64 - b.tw(score, s) / 2, 10, s);
    sparkle(b, 26, 18, (t / 200) % 2 + 1);
    sparkle(b, 102, 16, 2 - (t / 200) % 2);
  }
}

// --------------------------------------------------
// 14. (super rare) JACKPOT: slot machine lands on 777, flashing, star rain.
// --------------------------------------------------

inline void surpriseJackpot(Bm& b, uint32_t t, const Opts&) {
  if (t < 3000) {
    for (int i = 0; i < 3; i++) {
      double x = 28 + i * 24;
      b.rrect(x, 18, 22, 28, 3);
      b.rrect(x + 2, 20, 18, 24, 2, 0);
      uint32_t stopAt = 900 + i * 500;
      char d[2] = { (char)(t < stopAt ? '0' + (t / 70 + i * 4) % 10 : '7'), 0 };
      b.text(d, x + 6, 25, 2);
    }
    bool pulled = t < 400;
    b.rect(104, pulled ? 30 : 22, 2, pulled ? 14 : 22);
    b.disc(105, pulled ? 30 : 21, 3);
    b.rect(100, 40, 6, 4);
    if (t >= 1900) {
      int f = (t / 150) % 2;
      sparkle(b, 14, 20, f ? 1 : 3);
      sparkle(b, 116, 50, f ? 3 : 1);
      sparkle(b, 16, 52, f ? 3 : 1);
      if (f) b.invert();
    }
  } else {
    for (int k = 0; k < 8; k++) {
      double y = fmod(t * 0.04 * (1 + (k % 3) * 0.3) + k * 23, 80) - 10;
      b.sprite(SPR_STAR7, 7, k * 16 + 4, y);
    }
    double R = 20 + 2 * sin(t / 150.0);
    starRot(b, 64, 38, R + 2, (R + 2) * 0.44, t / 600.0, 0);
    starRot(b, 64, 38, R, R * 0.44, t / 600.0);
    face(b, 64, 39, R / 12);
    b.rect(0, 0, 128, 15, 0);
    const char* s = "777";
    b.text(s, 64 - b.tw(s) / 2, 4);
    sparkle(b, 40, 7, (t / 200) % 2 + 1);
    sparkle(b, 88, 7, 2 - (t / 200) % 2);
  }
}

// --------------------------------------------------

// Clears `b` (128×64) and draws surprise `id` at `t` ms.
inline void renderSurprise(Bm& b, int id, uint32_t t, const Opts& o) {
  b.clear();
  switch (id) {
    default: surpriseRocket(b, t, o); break;
    case SUR_FIREWORKS: surpriseFireworks(b, t, o); break;
    case SUR_DANCE: surpriseDance(b, t, o); break;
    case SUR_RAINBOW: surpriseRainbow(b, t, o); break;
    case SUR_BALLOONS: surpriseBalloons(b, t, o); break;
    case SUR_FISH: surpriseFish(b, t, o); break;
    case SUR_SPACE: surpriseSpace(b, t, o); break;
    case SUR_RACECAR: surpriseRaceCar(b, t, o); break;
    case SUR_TRAIN: surpriseTrain(b, t, o); break;
    case SUR_UFO: surpriseUfo(b, t, o); break;
    case SUR_VOLCANO: surpriseVolcano(b, t, o); break;
    case SUR_SKATE: surpriseSkate(b, t, o); break;
    case SUR_JACKPOT: surpriseJackpot(b, t, o); break;
  }
}

}  // namespace oled
