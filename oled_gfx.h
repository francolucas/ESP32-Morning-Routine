// --------------------------------------------------
// 1-bit renderer for the Morning Routine OLED screens.
//
// Straight C++ port of oled.js from the Claude Design project
// "Morning Routine OLED". Every icon and screen is drawn by code
// (no bitmaps), exactly like the design, so the output matches the
// design pixel for pixel. Pixel centers are at (x + .5, y + .5).
//
// No Arduino dependencies: this file also compiles on a desktop for
// pixel-comparison tests against the original JavaScript.
// --------------------------------------------------

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace oled {

constexpr double PI_D = 3.14159265358979323846;

// JavaScript's Math.round (ties go towards +infinity).
inline double jsRound(double v) { return floor(v + 0.5); }

// JavaScript's Math.hypot for two arguments (V8 scales by the max and
// uses Kahan summation), so ring/face edges match the design exactly.
inline double jsHypot(double a, double b) {
  a = fabs(a);
  b = fabs(b);
  double m = a > b ? a : b;
  if (m == 0) return 0;
  double sum = 0, comp = 0;
  const double vals[2] = { a / m, b / m };
  for (double n : vals) {
    double summand = n * n - comp;
    double prelim = sum + summand;
    comp = (prelim - sum) - summand;
    sum = prelim;
  }
  return sqrt(sum) * m;
}

// --------------------------------------------------
// Bitmap (one byte per pixel, 0 or 1)
// --------------------------------------------------

struct Bm {
  int w, h;
  uint8_t* d;

  Bm(int w_, int h_) : w(w_), h(h_), d((uint8_t*)calloc(w_ * h_, 1)) {}
  ~Bm() { free(d); }
  Bm(const Bm&) = delete;
  Bm& operator=(const Bm&) = delete;

  void clear() { memset(d, 0, w * h); }

  // v: 0 = clear, 1 = set, 2 = toggle
  void set(double xf, double yf, int v = 1) {
    int x = (int)floor(xf), y = (int)floor(yf);
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int i = y * w + x;
    d[i] = v == 2 ? 1 - d[i] : v;
  }

  int get(int x, int y) const {
    return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : d[y * w + x];
  }

  void rect(double x, double y, double rw, double rh, int v = 1) {
    x = jsRound(x);
    y = jsRound(y);
    for (int j = 0; j < rh; j++)
      for (int i = 0; i < rw; i++) set(x + i, y + j, v);
  }

  void line(double fx0, double fy0, double fx1, double fy1, int v = 1) {
    int x0 = (int)jsRound(fx0), y0 = (int)jsRound(fy0);
    int x1 = (int)jsRound(fx1), y1 = (int)jsRound(fy1);
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
      set(x0, y0, v);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * e;
      if (e2 >= dy) { e += dy; x0 += sx; }
      if (e2 <= dx) { e += dx; y0 += sy; }
    }
  }

  template <typename F>
  void shape(F fn, double x0, double y0, double x1, double y1, int v = 1) {
    for (int y = (int)floor(y0); y <= (int)ceil(y1); y++)
      for (int x = (int)floor(x0); x <= (int)ceil(x1); x++)
        if (fn(x + .5, y + .5)) set(x, y, v);
  }

  void disc(double cx, double cy, double r, int v = 1) {
    shape([&](double x, double y) {
      double ax = x - cx, ay = y - cy;
      return ax * ax + ay * ay <= r * r;
    }, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, v);
  }

  void ring(double cx, double cy, double r, double t = 1, int v = 1) {
    shape([&](double x, double y) {
      double dd = jsHypot(x - cx, y - cy);
      return dd <= r && dd > r - t;
    }, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, v);
  }

  void rrect(double x, double y, double rw, double rh, double r, int v = 1) {
    shape([&](double px, double py) {
      if (px < x || px > x + rw || py < y || py > y + rh) return false;
      double qx = fmax(fmax(x + r - px, 0.0), px - (x + rw - r));
      double qy = fmax(fmax(y + r - py, 0.0), py - (y + rh - r));
      return qx * qx + qy * qy <= r * r;
    }, x, y, x + rw - 1, y + rh - 1, v);
  }

  // pts: n points as {x, y}
  void poly(const double (*pts)[2], int n, int v = 1) {
    double y0 = INFINITY, y1 = -INFINITY;
    for (int i = 0; i < n; i++) {
      y0 = fmin(y0, pts[i][1]);
      y1 = fmax(y1, pts[i][1]);
    }
    double xs[16];
    for (int y = (int)floor(y0); y <= (int)ceil(y1); y++) {
      double sy = y + .5;
      int nx = 0;
      for (int i = 0; i < n; i++) {
        const double* a = pts[i];
        const double* b = pts[(i + 1) % n];
        if ((a[1] <= sy && b[1] > sy) || (b[1] <= sy && a[1] > sy))
          xs[nx++] = a[0] + (sy - a[1]) / (b[1] - a[1]) * (b[0] - a[0]);
      }
      // Insertion sort (at most a handful of crossings).
      for (int i = 1; i < nx; i++) {
        double k = xs[i];
        int j = i - 1;
        while (j >= 0 && xs[j] > k) { xs[j + 1] = xs[j]; j--; }
        xs[j + 1] = k;
      }
      for (int k = 0; k + 1 < nx; k += 2)
        for (int x = (int)ceil(xs[k] - .5); x <= (int)floor(xs[k + 1] - .5); x++)
          set(x, y, v);
    }
  }

  void sprite(const char* const* rows, int nrows, double x, double y,
              int v = 1, bool flip = false) {
    int sw = (int)strlen(rows[0]);
    for (int j = 0; j < nrows; j++)
      for (int i = 0; i < sw; i++)
        if (rows[j][i] == '#') set(x + (flip ? sw - 1 - i : i), y + j, v);
  }

  int tw(const char* str, int s = 1) const;
  void text(const char* str, double x, double y, int s = 1, int v = 1);

  enum BlitMode { SOLID, DITHER, OUTLINE };

  void blit(const Bm& src, double x, double y, BlitMode mode = SOLID) {
    for (int j = 0; j < src.h; j++)
      for (int i = 0; i < src.w; i++) {
        if (!src.get(i, j)) continue;
        if (mode == DITHER && (i + j) % 2) continue;
        if (mode == OUTLINE && src.get(i - 1, j) && src.get(i + 1, j) &&
            src.get(i, j - 1) && src.get(i, j + 1))
          continue;
        set(x + i, y + j, 1);
      }
  }

  // Pixel-doubled blit: each set pixel becomes a 2×2 block.
  void blit2(const Bm& src, double x, double y) {
    for (int j = 0; j < src.h; j++)
      for (int i = 0; i < src.w; i++)
        if (src.get(i, j)) rect(x + i * 2, y + j * 2, 2, 2);
  }

  void invert() {
    for (int i = 0; i < w * h; i++) d[i] ^= 1;
  }
};

// --------------------------------------------------
// 5×7 font (digits only: wall clock and countdowns)
// --------------------------------------------------

struct Glyph {
  char c;
  const char* rows[7];
};

static const Glyph GLYPHS[] = {
  { ' ', { "...", "...", "...", "...", "...", "...", "..." } },
  { '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
  { '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
  { '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
  { '3', { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." } },
  { '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
  { '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
  { '6', { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." } },
  { '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
  { '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
  { '9', { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." } },
  { ':', { ".", ".", "#", ".", "#", ".", "." } },
};

inline const Glyph* findGlyph(char c) {
  for (const Glyph& g : GLYPHS)
    if (g.c == c) return &g;
  return nullptr;
}

inline int Bm::tw(const char* str, int s) const {
  int total = 0;
  for (const char* p = str; *p; p++) {
    const Glyph* g = findGlyph(*p);
    total += ((g ? (int)strlen(g->rows[0]) : 3) + 1) * s;
  }
  return total - s;
}

inline void Bm::text(const char* str, double x, double y, int s, int v) {
  double cx = x;
  for (const char* p = str; *p; p++) {
    const Glyph* g = findGlyph(*p);
    if (!g) g = findGlyph(' ');
    int gw = (int)strlen(g->rows[0]);
    for (int j = 0; j < 7; j++)
      for (int i = 0; i < gw; i++)
        if (g->rows[j][i] == '#') rect(cx + i * s, y + j * s, s, s, v);
    cx += (gw + 1) * s;
  }
}

// --------------------------------------------------
// Small sprites
// --------------------------------------------------

#define OLED_KID_HEAD "..###..", ".#####.", ".#.#.#.", "..###..", "...#..."
static const char* const SPR_KID[3][11] = {
  { OLED_KID_HEAD, ".#####.", "#.###.#", "..###..", "..#.#..", ".#...#.", "#.....#" },
  { OLED_KID_HEAD, "..###..", ".#####.", "..###..", "..#.#..", "..#.#..", "..#.#.." },
  { OLED_KID_HEAD, ".#####.", "#.###.#", "..###..", "..#.#..", "..#..#.", ".#...#." },
};
#undef OLED_KID_HEAD
static const char* const SPR_STAR7[7] = { "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#." };
static const char* const SPR_CHECK[7] = { "........#", ".......##", "#.....##.", "##...##..", ".##.##...", "..###....", "...#....." };
static const char* const SPR_CLOCK[7] = { "..###..", ".#...#.", "#..#..#", "#..##.#", "#.....#", ".#...#.", "..###.." };
static const char* const SPR_TRI[4] = { "...#...", "..###..", ".#####.", "#######" };
static const char* const SPR_HEART[6] = { ".##..##.", "########", "########", ".######.", "..####..", "...##..." };
static const char* const SPR_BUB[3] = { ".#.", "#.#", ".#." };

// --------------------------------------------------
// Shared drawing helpers
// --------------------------------------------------

inline void starPts(double cx, double cy, double R, double r, double out[10][2]) {
  for (int i = 0; i < 10; i++) {
    double a = -PI_D / 2 + i * PI_D / 5, rr = i % 2 ? r : R;
    out[i][0] = cx + cos(a) * rr;
    out[i][1] = cy + sin(a) * rr;
  }
}

inline void face(Bm& b, double cx, double cy, double k) {
  double ew = fmax(1, jsRound(k)), eh = fmax(2, jsRound(1.8 * k));
  double ex = jsRound(2.4 * k), ey = jsRound(cy - 2 * k);
  b.rect(cx - ex - ew, ey, ew, eh, 0);
  b.rect(cx + ex, ey, ew, eh, 0);
  double rs = 3.2 * k, th = fmax(1.1, 0.9 * k), sy = cy - 0.6 * k;
  b.shape([&](double x, double y) {
    double dd = jsHypot(x - cx, y - sy);
    return dd <= rs && dd > rs - th && y > cy + 0.9 * k;
  }, cx - rs - 1, sy, cx + rs + 1, sy + rs + 1, 0);
}

inline void sparkle(Bm& b, double x, double y, int s, int v = 1) {
  b.set(x, y, v);
  for (int k = 1; k <= s; k++) {
    b.set(x + k, y, v);
    b.set(x - k, y, v);
    b.set(x, y + k, v);
    b.set(x, y - k, v);
  }
}

// s: 0 = closed, 1 = ajar, 2 = open (kid walking out)
inline void door(Bm& b, double x, double y, double w, double h, int s) {
  b.rect(x, y, w, 2);
  b.rect(x, y, 2, h);
  b.rect(x + w - 2, y, 2, h);
  double x0 = x + 3, y0 = y + 3, iw = w - 6, ih = h - 3;
  if (s > 0) {
    double sx = x0 + iw * 0.66, sy = y0 + ih * 0.22, r = iw * 0.12 + 1;
    b.disc(sx, sy, r);
    for (int k = 0; k < 8; k++) {
      double a = k * PI_D / 4 + (s == 2 ? PI_D / 8 : 0);
      b.set(sx + cos(a) * (r + 2.2), sy + sin(a) * (r + 2.2));
    }
    for (int i = 0; i < iw; i += 2) b.set(x0 + i, y0 + ih - 1);
    if (s == 2) b.sprite(SPR_KID[0], 11, jsRound(x0 + iw * 0.58 - 3), y0 + ih - 13);
  }
  if (s == 0) {
    b.rect(x0, y0, iw, ih);
    double wh = jsRound(ih * 0.3);
    b.rrect(x0 + 3, y0 + 3, iw - 6, wh, 2, 0);
    b.rect(x0 + floor(iw / 2), y0 + 3, 1, wh);
    b.rect(x0 + 3, y0 + 3 + floor(wh / 2), iw - 6, 1);
    double py = y0 + jsRound(ih * 0.52);
    b.rrect(x0 + 3, py, iw - 6, ih - (py - y0) - 3, 1, 0);
    b.rrect(x0 + 4, py + 1, iw - 8, ih - (py - y0) - 5, 1);
    b.disc(x0 + iw - 3.5, y0 + ih * 0.44, 1.6, 0);
  } else {
    double pw = s == 1 ? iw * 0.55 : iw * 0.2;
    const double pts[4][2] = { { x0, y0 }, { x0 + pw, y0 + 2 }, { x0 + pw, y0 + ih - 2 }, { x0, y0 + ih } };
    b.poly(pts, 4);
    b.rect(jsRound(x0 + pw - 3), jsRound(y0 + ih * 0.45), 1, 2, 0);
  }
}

// Box-filter downscale: a destination pixel is lit when at least half of
// the source area it covers is lit.
inline void downscale(const Bm& src, Bm& dst) {
  dst.clear();
  const double sx = (double)src.w / dst.w, sy = (double)src.h / dst.h;
  for (int y = 0; y < dst.h; y++)
    for (int x = 0; x < dst.w; x++) {
      double x0 = x * sx, x1 = x0 + sx, y0 = y * sy, y1 = y0 + sy, cov = 0;
      for (int j = (int)floor(y0); j < (int)ceil(y1); j++)
        for (int i = (int)floor(x0); i < (int)ceil(x1); i++) {
          if (!src.get(i, j)) continue;
          cov += (fmin(i + 1, x1) - fmax(i, x0)) * (fmin(j + 1, y1) - fmax(j, y0));
        }
      if (cov >= 0.5 * sx * sy) dst.set(x, y);
    }
}

// --------------------------------------------------
// 32×32 icons (drawn into a cleared 32×32 Bm)
// --------------------------------------------------

enum Icon { ICON_SHIRT, ICON_BREAD, ICON_TEETH, ICON_SHOE, ICON_BIKE, ICON_STAR, ICON_HOURGLASS, ICON_DOOR, ICON_TOILET };

inline void iconShirt(Bm& b) {
  const double pts[10][2] = { { 11, 4 }, { 21, 4 }, { 29, 9 }, { 26, 16 }, { 23, 14 }, { 23, 30 }, { 9, 30 }, { 9, 14 }, { 6, 16 }, { 3, 9 } };
  b.poly(pts, 10);
  b.disc(16, 3.5, 4.6, 0);
  b.sprite(SPR_HEART, 6, 12, 14, 0);
  b.rect(10, 27, 12, 1, 0);
}

inline void iconBread(Bm& b, int f) {
  f %= 3;
  auto T = [&](int i, int v) {
    b.disc(10, 12, 7 - i, v);
    b.disc(22, 12, 7 - i, v);
    b.rrect(3 + i, 12, 26 - 2 * i, 18 - i, 3, v);
  };
  T(0, 1); T(2, 0); T(3, 1);
  b.rrect(12, 16, 8, 6, 1, 0);
  b.rrect(13, 17, 6, 4, 1);
  if (f >= 1) { b.disc(28, 8, 4.5, 0); b.set(30, 14); b.set(31, 12); }
  if (f >= 2) { b.disc(29.5, 16, 4, 0); b.set(30, 22); }
}

inline void iconBike(Bm& b, int f) {
  f %= 2;
  static const double WHEELS[2] = { 7.5, 24.5 };
  for (double cx : WHEELS) {
    b.ring(cx, 23, 7, 1.6);
    b.disc(cx, 23, 1.2);
    for (int k = 0; k < 2; k++) {
      double a = f * PI_D / 4 + k * PI_D / 2;
      b.line(cx + cos(a) * 5, 23 + sin(a) * 5, cx - cos(a) * 5, 23 - sin(a) * 5);
    }
  }
  b.line(8, 23, 16, 23); b.line(8, 23, 13, 14); b.line(16, 23, 13, 13);
  b.line(13, 15, 23, 15); b.line(13, 16, 23, 16); b.line(16, 23, 23, 15);
  b.line(23, 15, 25, 23); b.line(23, 15, 22, 10);
  b.rect(20, 9, 5, 2); b.rect(10, 12, 6, 2);
  b.ring(16, 23, 2.6, 1.2);
  if (f) b.line(16, 23, 14, 27);
  else b.line(16, 23, 18, 27);
}

inline void iconTeeth(Bm& b, int f) {
  f %= 2;
  b.rrect(3, 2, 14, 11, 4); b.rect(4, 11, 5, 6); b.rect(11, 11, 5, 6);
  static const int GAPS[4] = { 4, 8, 11, 15 };
  for (int x : GAPS) b.set(x, 16, 0);
  b.set(9, 2, 0); b.set(10, 2, 0);
  b.rect(6, 5, 1, 2, 0); b.rect(13, 5, 1, 2, 0);
  b.set(6, 8, 0); b.set(13, 8, 0); b.rect(7, 9, 6, 1, 0);
  sparkle(b, 22, 5, f ? 1 : 2);
  b.sprite(SPR_BUB, 3, f ? 18 : 19, f ? 14 : 11);
  int dx = f ? -2 : 0;
  b.rrect(3 + dx, 24, 19, 4, 2); b.rect(21 + dx, 24, 3, 3); b.rrect(23 + dx, 21, 8, 6, 1);
  for (int i = 0; i < 4; i++) b.rect(24 + dx + 2 * i, 17, 1, 4);
  b.rrect(23 + dx, 13, 8, 4, 2);
  b.rect(5 + dx, 25, 3, 2, 0);
}

inline void iconShoe(Bm& b) {
  const double sole[9][2] = { { 4, 24 }, { 4, 11 }, { 7, 9 }, { 12, 9 }, { 14, 14 }, { 18, 17 }, { 25, 19 }, { 29, 21 }, { 30, 24 } };
  b.poly(sole, 9);
  b.rrect(2, 24, 29, 5, 2); b.rect(4, 23, 26, 1, 0);
  b.rect(6, 10, 6, 1, 0);
  const double bolt[7][2] = { { 8, 12 }, { 12, 12 }, { 10, 16 }, { 13, 16 }, { 7, 22 }, { 9, 17 }, { 6, 17 } };
  b.poly(bolt, 7, 0);
  b.rect(15, 17, 2, 1, 0); b.rect(18, 18, 2, 1, 0); b.rect(21, 19, 2, 1, 0);
  static const int TREAD[6] = { 6, 10, 14, 18, 22, 26 };
  for (int x : TREAD) b.set(x, 28, 0);
}

inline void iconStar(Bm& b) {
  double pts[10][2];
  starPts(16, 17, 15, 6.4, pts);
  b.poly(pts, 10);
  face(b, 16, 18, 1.25);
}

// Toilet, side view facing right (not part of the Claude Design set).
inline void iconToilet(Bm& b) {
  b.rrect(4, 2, 10, 16, 2);              // tank
  b.rect(6, 5, 5, 2, 0);                 // flush handle cut-out
  b.rrect(3, 17, 27, 4, 2);              // seat + lid
  b.rect(5, 21, 23, 1, 0);               // gap under the seat
  const double bowl[6][2] = { { 5, 22 }, { 29, 22 }, { 27, 25 }, { 22, 28 }, { 8, 28 }, { 5, 25 } };
  b.poly(bowl, 6);
  b.rect(10, 28, 10, 2);                 // pedestal
  b.rrect(7, 29, 16, 3, 1);              // foot
}

inline void iconHourglass(Bm& b, int f) {
  f %= 2;
  b.rrect(5, 1, 22, 3, 1); b.rrect(5, 28, 22, 3, 1);
  b.line(7, 4, 15, 15); b.line(24, 4, 16, 15); b.line(15, 16, 7, 27); b.line(16, 16, 24, 27);
  if (f == 0) {
    const double top[3][2] = { { 10.5, 7 }, { 21.5, 7 }, { 16, 14 } };
    const double bot[3][2] = { { 16, 21 }, { 22, 27.5 }, { 10, 27.5 } };
    b.poly(top, 3); b.poly(bot, 3); b.rect(15, 14, 2, 7);
  } else {
    const double top[3][2] = { { 12.5, 10 }, { 19.5, 10 }, { 16, 14 } };
    const double bot[3][2] = { { 16, 18.5 }, { 24, 27.5 }, { 8, 27.5 } };
    b.poly(top, 3); b.poly(bot, 3); b.rect(15, 14, 2, 5);
  }
}

// Draws icon `id` (animation frame f) into `b`, which must be 32×32.
inline void renderIcon(Bm& b, Icon id, int f) {
  b.clear();
  switch (id) {
    case ICON_SHIRT: iconShirt(b); break;
    case ICON_BREAD: iconBread(b, f); break;
    case ICON_TEETH: iconTeeth(b, f); break;
    case ICON_SHOE: iconShoe(b); break;
    case ICON_BIKE: iconBike(b, f); break;
    case ICON_STAR: iconStar(b); break;
    case ICON_HOURGLASS: iconHourglass(b, f); break;
    case ICON_DOOR: door(b, 3, 0, 26, 32, f % 3); break;
    case ICON_TOILET: iconToilet(b); break;
  }
}

// --------------------------------------------------
// Routine definition
// --------------------------------------------------

struct Task {
  Icon icon;
  uint16_t durSec;  // ideal duration; also the ratio used to split the time left
};

constexpr uint8_t NUM_TASKS = 5;

static const Task TASKS[NUM_TASKS] = {
  { ICON_TOILET, 5 * 60 },  // toilet
  { ICON_SHIRT, 7 * 60 },   // get dressed
  { ICON_BREAD, 30 * 60 },  // breakfast
  { ICON_TEETH, 7 * 60 },   // brush teeth
  { ICON_SHOE, 7 * 60 },    // shoes + coat
};

// "m:ss"
inline void fmt(double secs, char* out, size_t n) {
  unsigned long s = (unsigned long)fmax(0, jsRound(secs));
  snprintf(out, n, "%lu:%02lu", s / 60, s % 60);
}

// --------------------------------------------------
// Screens
// --------------------------------------------------

enum ScreenId { SCR_ACTIVE, SCR_OVERVIEW, SCR_HURRY, SCR_REWARD, SCR_LEAVE, SCR_FINALE, SCR_SLEEP, SCR_DINO, SCR_EVOLVE };

struct Opts {
  int task;           // current task index 0..3
  double rem;         // fraction of the task time left (0..1)
  double secsLeft;    // seconds left (active and hurry screens)
  const char* clock;  // header clock, e.g. "7:42" (empty = not drawn)
  bool softInvert;    // hurry f0: invert in software (the firmware uses the controller's 0xA7 instead)
  int mornings;       // finished mornings, including today (dino screens)
};

// Frame durations in ms, per screen.
static const uint16_t FRAMES_ACTIVE[] = { 220, 220, 220 };
static const uint16_t FRAMES_OVERVIEW[] = { 450, 450 };
static const uint16_t FRAMES_HURRY[] = { 400, 400 };
static const uint16_t FRAMES_REWARD[] = { 200, 200, 500 };
static const uint16_t FRAMES_LEAVE[] = { 600, 300, 900 };
static const uint16_t FRAMES_FINALE[] = { 350, 350 };
static const uint16_t FRAMES_SLEEP[] = { 900, 900 };
static const uint16_t FRAMES_DINO[] = { 500, 500 };
static const uint16_t FRAMES_EVOLVE[] = { 250, 250 };

inline const uint16_t* framesFor(ScreenId s, int& n) {
  switch (s) {
    case SCR_ACTIVE: n = 3; return FRAMES_ACTIVE;
    case SCR_OVERVIEW: n = 2; return FRAMES_OVERVIEW;
    case SCR_HURRY: n = 2; return FRAMES_HURRY;
    case SCR_REWARD: n = 3; return FRAMES_REWARD;
    case SCR_LEAVE: n = 3; return FRAMES_LEAVE;
    case SCR_SLEEP: n = 2; return FRAMES_SLEEP;
    case SCR_DINO: n = 2; return FRAMES_DINO;
    case SCR_EVOLVE: n = 2; return FRAMES_EVOLVE;
    default: n = 2; return FRAMES_FINALE;
  }
}

inline uint32_t cycleMs(ScreenId s) {
  int n;
  const uint16_t* fr = framesFor(s, n);
  uint32_t total = 0;
  for (int i = 0; i < n; i++) total += fr[i];
  return total;
}

// Animation frame for screen `s`, `t` ms after the screen started.
inline int frameAt(ScreenId s, uint32_t t) {
  int n;
  const uint16_t* fr = framesFor(s, n);
  uint32_t m = t % cycleMs(s);
  for (int i = 0; i < n; i++) {
    if (m < fr[i]) return i;
    m -= fr[i];
  }
  return 0;
}

// Status strip (rows 0..15): wall clock on the left, stars on the right.
// (The design's step dots on the left were removed; only the stars remain.)
inline void header(Bm& b, int /*cur*/, int done, int f, const Opts& o, int blink = -1) {
  for (int i = 0; i < NUM_TASKS; i++) {
    int x = 128 - NUM_TASKS * 8 + i * 8;  // right-aligned (88 with 5 tasks)
    if (i < done) {
      if (!(i == blink && f == 0)) b.sprite(SPR_STAR7, 7, x, 4);
    } else {
      b.set(x + 3, 7);
    }
  }
  b.text(o.clock, 1, 4);  // left-aligned
}

// 01 — task in progress: bar drains left, kid walks at its tip, countdown below.
inline void screenActive(Bm& b, int f, const Opts& o) {
  const Task& T = TASKS[o.task];
  header(b, o.task, o.task, f, o);
  Bm ic(32, 32);
  renderIcon(ic, T.icon, f);
  b.blit(ic, 4, f == 1 ? 23 : 24);
  b.rrect(42, 37, 84, 13, 3);
  b.rrect(43, 38, 82, 11, 2, 0);
  double fw = jsRound(78 * o.rem);
  if (fw > 0) b.rect(45, 40, fw, 7);
  b.sprite(SPR_KID[f % 3], 11, fmax(42, fmin(118, 45 + fw - 4)), 25, 1, true);
  char s[24];
  fmt(o.secsLeft, s, sizeof(s));
  int w = b.tw(s);
  b.text(s, 125 - w, 55);
  b.sprite(SPR_CLOCK, 7, 125 - w - 10, 55);
}

// 02 — overview: done = dithered + check, now = solid + bouncing arrow, next = outline.
// Five tasks: icons shrunk to 24×24 in 25.6 px columns (the design has four 32×32).
inline void screenOverview(Bm& b, int f, const Opts& o) {
  header(b, o.task, o.task, f, o);
  const double col = 128.0 / NUM_TASKS;
  Bm ic(32, 32), small(24, 24);
  int mxs[NUM_TASKS];
  for (int i = 0; i < NUM_TASKS; i++) {
    int x = (int)jsRound(i * col + (col - 24) / 2), mx = x + 12;
    mxs[i] = mx;
    renderIcon(ic, TASKS[i].icon, 0);
    downscale(ic, small);
    if (i < o.task) b.blit(small, x, 22, Bm::DITHER);
    else if (i == o.task) b.blit(small, x, f ? 21 : 22);
    else b.blit(small, x, 22, Bm::OUTLINE);
    if (i < o.task) b.sprite(SPR_CHECK, 7, mx - 4, 53);
    else if (i == o.task) b.sprite(SPR_TRI, 4, mx - 4, f ? 53 : 55);
    else b.ring(mx, 56.5, 2.6, 1.1);
  }
  // Path between the marks: solid up to the current task, dotted after.
  for (int x = 0; x < 128; x++) {
    bool nearMark = false;
    for (int mx : mxs) if (fabs(x + .5 - mx) < 7) nearMark = true;
    if (nearMark) continue;
    if (x < mxs[o.task] || x % 3 == 0) b.set(x, 56);
  }
}

// 03 — hurry, last minute: hourglass sand falls on every flip.
inline void screenHurry(Bm& b, int f, const Opts& o) {
  header(b, o.task, o.task, 0, o);
  Bm ic(32, 32);
  renderIcon(ic, ICON_HOURGLASS, f);
  b.blit(ic, 6, 24);
  char s[24];
  fmt(o.secsLeft, s, sizeof(s));
  b.text(s, 85 - floor(b.tw(s, 2) / 2.0), 20, 2);
  const int by = 40;
  b.rrect(46, by, 78, 13, 3);
  b.rrect(47, by + 1, 76, 11, 2, 0);
  double fw = jsRound(72 * o.secsLeft / 60);
  if (fw > 0) b.rect(49, by + 3, fw, 7);
  if (f == 0 && o.softInvert) b.invert();
}

// 04 — task done: the star jumps, sparkles alternate, the new header star blinks.
inline void screenReward(Bm& b, int f, const Opts& o) {
  int n = o.task + 1;
  header(b, n, n, f, o, o.task);
  static const double RS[3] = { 17, 23, 21 };
  double R = RS[f];
  double pts[10][2];
  starPts(64, 41, R, R * 0.44, pts);
  b.poly(pts, 10);
  face(b, 64, 42, R / 12);
  static const int A[6][3] = { { 20, 28, 2 }, { 106, 24, 1 }, { 28, 54, 1 }, { 100, 52, 2 }, { 12, 42, 1 }, { 116, 40, 1 } };
  static const int B[6][3] = { { 24, 24, 1 }, { 104, 28, 2 }, { 30, 52, 2 }, { 98, 56, 1 }, { 14, 38, 2 }, { 114, 44, 2 } };
  if (f == 1) for (auto& s : A) sparkle(b, s[0], s[1], s[2]);
  if (f == 2) for (auto& s : B) sparkle(b, s[0], s[1], s[2]);
}

// 05 — time to leave: door opens, kid walks out, big bike (2×) wheels spin.
inline void screenLeave(Bm& b, int f, const Opts& o) {
  header(b, NUM_TASKS - 1, NUM_TASKS - 1, f, o);
  door(b, 6, 18, 32, 46, f);
  Bm ic(32, 32);
  renderIcon(ic, ICON_BIKE, f);
  b.blit2(ic, 52, f == 1 ? -1 : 0);
}

// 06 — all done: a smiling sun, rays turn 22.5° every frame. No status strip.
// Fitted below row 16 so it stays out of the yellow band on two-colour
// (yellow/blue) modules: the design's sun scaled to 23/28 and centred at y=40.
inline void screenFinale(Bm& b, int f, const Opts&) {
  const double cx = 64, cy = 40;
  b.disc(cx, cy, 12);
  face(b, cx, cy + 1, 1.25);
  for (int k = 0; k < 8; k++) {
    double a = k * PI_D / 4 + (f ? PI_D / 8 : 0), l = k % 2 ? 20.5 : 23;
    double c = cos(a), s = sin(a), px = -s * 1.1, py = c * 1.1;
    // Each ray is a solid ~2 px wide quad from r=15.5 to r=l.
    const double ray[4][2] = {
      { cx + c * 15.5 + px, cy + s * 15.5 + py },
      { cx + c * l + px, cy + s * l + py },
      { cx + c * l - px, cy + s * l - py },
      { cx + c * 15.5 - px, cy + s * 15.5 - py },
    };
    b.poly(ray, 4);
  }
}

// Idle (outside the morning window): wall clock, crescent moon, twinkling stars.
// Not part of the Claude Design set — drawn in the same style.
inline void screenSleep(Bm& b, int f, const Opts& o) {
  b.text(o.clock, 64 - floor(b.tw(o.clock) / 2.0), 4);
  b.disc(64, 40, 16);
  b.disc(72, 33, 14, 0);
  sparkle(b, 30, 28, f ? 1 : 2);
  sparkle(b, 98, 46, f ? 2 : 1);
  b.set(40, 52);
  b.set(90, 24);
}

// --------------------------------------------------
// Dino pet: grows one stage every few finished mornings.
// Not part of the Claude Design set — drawn in the same style.
// --------------------------------------------------

constexpr int DINO_STAGES = 6;

// Finished mornings needed for each stage: egg, cracked egg, baby in its
// shell, small dino, big dino, king dino (15 school mornings = 3 weeks).
static const uint8_t DINO_AT[DINO_STAGES] = { 0, 2, 4, 7, 11, 15 };

inline int dinoStage(int mornings) {
  int s = 0;
  while (s + 1 < DINO_STAGES && mornings >= DINO_AT[s + 1]) s++;
  return s;
}

static const char* const DINO_EGG[] = {
  "....####....",
  "...######...",
  "..########..",
  "..##..####..",
  ".###..#####.",
  ".##########.",
  ".######..##.",
  "#######..###",
  "############",
  "###..#######",
  "###..#######",
  "############",
  ".##########.",
  "..########..",
  "....####....",
};

// A zigzag crack, and an eye peeking out of a hole.
static const char* const DINO_CRACKED[] = {
  "....####....",
  "...######...",
  "..########..",
  "..###....#..",
  ".####.##..#.",
  ".####.##..#.",
  ".###....###.",
  "#.##########",
  ".#.#.#.#.#.#",
  "#.#.#.#.#.#.",
  "############",
  "############",
  ".##########.",
  "..########..",
  "....####....",
};

static const char* const DINO_BABY[] = {
  ".....####.......",
  "...########.....",
  "..##########....",
  "..#######..##...",
  "..#######..###..",
  "..#############.",
  "..#############.",
  "..###########...",
  "..########......",
  "..##.####.......",
  "................",
  ".#...#...#...#..",
  "###.###.###.###.",
  "################",
  "################",
  ".##############.",
  "...##########...",
};

static const char* const DINO_SMALL[] = {
  "..........#####...",
  ".........#######..",
  ".........##.#####.",
  ".........########.",
  ".........########.",
  ".........#####....",
  "........#######...",
  "#......#######....",
  "##....########....",
  "###..##########...",
  ".############.#...",
  "..###########.....",
  "...#########......",
  "....#######.......",
  "....##...##.......",
  "....##...##.......",
  "...###..###.......",
};

#define OLED_DINO_BIG_BODY                \
  "..............########..",             \
  ".............##########.",             \
  ".............###.######.",             \
  ".............##########.",             \
  ".............##########.",             \
  ".............#######....",             \
  ".........#..#########...",             \
  "........###.########....",             \
  "...#...#############....",             \
  "#.###.##############....",             \
  "##################.##...",             \
  ".#################......",             \
  "..###############.......",             \
  "...#############........",             \
  ".....#########..........",             \
  ".....###...###..........",             \
  ".....###...###..........",             \
  "....####..####.........."

static const char* const DINO_BIG[] = { OLED_DINO_BIG_BODY };

static const char* const DINO_KING[] = {
  "...............#.#.#....",
  "...............#####....",
  "........................",
  OLED_DINO_BIG_BODY,
};
#undef OLED_DINO_BIG_BODY

struct DinoSprite {
  const char* const* rows;
  int n;
};

#define OLED_DINO(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
static const DinoSprite DINO_SPRITES[DINO_STAGES] = {
  OLED_DINO(DINO_EGG), OLED_DINO(DINO_CRACKED), OLED_DINO(DINO_BABY),
  OLED_DINO(DINO_SMALL), OLED_DINO(DINO_BIG), OLED_DINO(DINO_KING),
};
#undef OLED_DINO

// Dino `stage` with its feet at (cx, bottom). scale 2 = pixel-doubled.
// f1: eggs rock sideways, dinos hop.
inline void dino(Bm& b, int stage, double cx, double bottom, int f, int scale = 2,
                 Bm::BlitMode mode = Bm::SOLID) {
  const DinoSprite& s = DINO_SPRITES[stage];
  Bm spr((int)strlen(s.rows[0]), s.n);
  spr.sprite(s.rows, s.n, 0, 0);
  double x = cx - floor(spr.w * scale / 2.0), y = bottom - spr.h * scale;
  if (f) {
    if (stage < 2) x += scale;
    else y -= scale;
  }
  if (scale == 2) b.blit2(spr, x, y);
  else b.blit(spr, x, y, mode);
}

// Today's dino on the left. On the right: the next stage as a mystery
// outline over one dot per morning at this stage (filled = done, today included).
inline void screenDino(Bm& b, int f, const Opts& o) {
  header(b, NUM_TASKS, NUM_TASKS, f, o);
  int s = dinoStage(o.mornings);
  dino(b, s, 36, 63, f);
  if (s + 1 < DINO_STAGES) {
    dino(b, s + 1, 100, 46, 0, 1, Bm::OUTLINE);
    // Today counts too, so a full row means "changes next morning".
    int need = DINO_AT[s + 1] - DINO_AT[s], done = o.mornings - DINO_AT[s] + 1;
    for (int i = 0; i < need; i++) {
      double x = 100 - (need - 1) * 5 + i * 10;
      if (i < done) b.disc(x, 56, 3);
      else b.ring(x, 56, 3, 1.1);
    }
  } else {
    sparkle(b, 84, 26, f ? 1 : 2);
    sparkle(b, 116, 34, f ? 2 : 1);
    sparkle(b, 98, 50, f ? 1 : 2);
  }
}

// Stage-up: old and new form alternate, sparkles on the new one.
inline void screenEvolve(Bm& b, int f, const Opts& o) {
  header(b, NUM_TASKS, NUM_TASKS, 0, o);
  int s = dinoStage(o.mornings);
  dino(b, f ? s : s - 1, 64, 63, 0);
  if (f) {
    sparkle(b, 22, 28, 2);
    sparkle(b, 106, 26, 2);
    sparkle(b, 16, 52, 1);
    sparkle(b, 112, 50, 1);
  }
}

// Clears `b` (128×64) and draws screen `s` at frame `f`.
inline void renderScreen(Bm& b, ScreenId s, int f, const Opts& o) {
  b.clear();
  switch (s) {
    case SCR_ACTIVE: screenActive(b, f, o); break;
    case SCR_OVERVIEW: screenOverview(b, f, o); break;
    case SCR_HURRY: screenHurry(b, f, o); break;
    case SCR_REWARD: screenReward(b, f, o); break;
    case SCR_LEAVE: screenLeave(b, f, o); break;
    case SCR_FINALE: screenFinale(b, f, o); break;
    case SCR_SLEEP: screenSleep(b, f, o); break;
    case SCR_DINO: screenDino(b, f, o); break;
    case SCR_EVOLVE: screenEvolve(b, f, o); break;
  }
}

}  // namespace oled
