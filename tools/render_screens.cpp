// Renders the OLED screens to PNG files on a desktop, using the same
// drawing code as the firmware (oled_gfx.h / surprises.h).
//
//   c++ -std=c++17 -O2 -I.. render_screens.cpp -lz -o render_screens
//   ./render_screens ../docs/images
//
// Each display pixel becomes a 4×4 block (3×3 lit + 1 px gap) so the
// images look like a real 128×64 OLED.

#include <zlib.h>

#include <string>
#include <vector>

#include "oled_gfx.h"
#include "surprises.h"

using namespace oled;

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((x >> (i * 8)) & 0xff);
}

static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> c;
  put32(c, data.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  uint32_t crc = crc32(0, c.data() + 4, c.size() - 4);
  put32(c, crc);
  fwrite(c.data(), 1, c.size(), f);
}

static void writePng(const Bm& b, const std::string& path) {
  const int S = 4, W = b.w * S, H = b.h * S;
  std::vector<uint8_t> raw;
  for (int y = 0; y < H; y++) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < W; x++) {
      bool gap = (x % S == S - 1) || (y % S == S - 1);
      int on = b.get(x / S, y / S);
      raw.push_back(gap ? 0 : on ? 2 : 1);  // palette index
    }
  }
  uLongf len = compressBound(raw.size());
  std::vector<uint8_t> z(len);
  compress2(z.data(), &len, raw.data(), raw.size(), 9);
  z.resize(len);

  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, W);
  put32(ihdr, H);
  ihdr.insert(ihdr.end(), { 8, 3, 0, 0, 0 });  // 8-bit, palette
  chunk(f, "IHDR", ihdr);
  chunk(f, "PLTE", { 0x05, 0x07, 0x0a,     // gap
                     0x10, 0x13, 0x18,     // pixel off
                     0xe6, 0xf2, 0xff });  // pixel on
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  printf("%s\n", path.c_str());
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  Bm b(128, 64);

  struct Shot { const char* name; ScreenId s; int f; Opts o; };
  const Shot shots[] = {
    { "overview", SCR_OVERVIEW, 0, { 1, 1.0, 420, "7:24", false, 5 } },
    { "task", SCR_ACTIVE, 0, { 2, 0.62, 1116, "7:41", false, 5 } },
    { "hurry", SCR_HURRY, 1, { 3, 0.1, 42, "8:02", true, 5 } },
    { "star", SCR_REWARD, 2, { 3, 0, 0, "8:03", false, 5 } },
    { "leave", SCR_LEAVE, 2, { 4, 0, 0, "8:18", false, 5 } },
    { "dino", SCR_DINO, 0, { 0, 0, 0, "8:19", false, 12 } },
    { "sleep", SCR_SLEEP, 0, { 0, 0, 0, "21:30", false, 12 } },
  };
  for (const Shot& s : shots) {
    renderScreen(b, s.s, s.f, s.o);
    writePng(b, out + "/" + s.name + ".png");
  }

  const Opts o = { 0, 0, 0, "", false, 12 };
  struct Sur { const char* name; int id; uint32_t t; };
  const Sur surs[] = {
    { "surprise-rocket", SUR_ROCKET, 3200 },
    { "surprise-train", SUR_TRAIN, 3000 },
    { "surprise-ufo", SUR_UFO, 4000 },
  };
  for (const Sur& s : surs) {
    renderSurprise(b, s.id, s.t, o);
    writePng(b, out + "/" + s.name + ".png");
  }
  return 0;
}
