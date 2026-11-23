#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <U8g2lib.h>
#include <Preferences.h>

#include "oled_gfx.h"
#include "surprises.h"
#include "secrets.h"  // WIFI_SSID / WIFI_PASSWORD (see secrets.example.h)

// --------------------------------------------------
// Morning Routine timer
//
// Screens, timings and sounds follow the Claude Design project
// "Morning Routine OLED":
//   02 Overview (3 s) → 01 Task → (at 1:00) 03 Hurry → press ● → 04 Star
//   → repeat for each task → 05 Time to leave → press ● → 06 Finale
//   → (on a stage-up morning) dino evolves → dino pet
//
// Finale surprise: the finale is one of 13 random animations (see
// surprises.h): 9 common, 3 rare, 1 super-rare jackpot. Never the same
// one two mornings in a row. Preview from the Serial Monitor: send "s"
// for a random one, or "s0" … "s12" for a specific one.
//
// Dino pet: every finished morning (once per day) is saved in flash and
// grows the dino one step: egg → cracked egg → baby → small → big → king
// after 15 mornings (see oled::DINO_AT). Test runs show the next morning's
// dino without saving it.
//
// Timing: the routine starts at power-on and aims to finish by LEAVE_TIME.
// Each task normally gets its ideal duration (5 / 7 / 30 / 7 / 7 min, see
// oled::TASKS). When running late, the time left is split across the
// remaining tasks in that ratio instead (never longer than ideal),
// recalculated at the start of every task.
//
// Powered on outside the morning window (e.g. in the evening) it shows a
// sleep screen instead of starting.
//
// One button, "Feito" (done):
//   press        → task done / leaving / wake the screen
//   hold 1 s     → peek at the overview during a task (release goes back)
//   hold 5 s     → in the morning window (or without a clock): restart from
//                  the first task. Outside it: test run with time sped up
//                  TEST_TIME_SCALE×. Fires while still held.
// --------------------------------------------------

// --------------------------------------------------
// Hardware configuration — ESP32-C3 Super Mini
//   Board: "Nologo ESP32C3 Super Mini" (or "ESP32C3 Dev Module"),
//   USB CDC On Boot: Enabled (for the Serial Monitor over USB).
//   The button goes from the pin to GND (internal pull-up).
// --------------------------------------------------

constexpr uint8_t BUTTON_PIN = 20;  // arcade button — Feito (done) / hold = restart or test

constexpr uint8_t BUZZER_PIN = 5;   // passive piezo buzzer, via 100 Ω

constexpr uint8_t OLED_SDA = 3;
constexpr uint8_t OLED_SCL = 4;
constexpr uint8_t OLED_ADDRESS = 0x3C;

// The design targets a 1.3" SH1106 module. Set to 0 for a 0.96" SSD1306.
#define OLED_SH1106 0

#if OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
#endif

// --------------------------------------------------
// Routine settings
// --------------------------------------------------

// Header clock comes from NTP over WiFi. Until the first sync it stays blank.
// POSIX TZ string — Europe/Berlin (CET/CEST).
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// After the first sync WiFi is turned off; the ESP32 keeps time on its own.
constexpr bool WIFI_OFF_AFTER_SYNC = true;

// Time to leave home. Each task gets a share of the time left until then.
constexpr uint8_t LEAVE_HOUR = 8;
constexpr uint8_t LEAVE_MINUTE = 20;

// The routine starts at power-on only between LEAVE_TIME minus this window
// and LEAVE_TIME (05:00–08:00). Outside it the sleep screen shows instead.
constexpr uint32_t PLAN_WINDOW_MS = 3UL * 60UL * 60UL * 1000UL;

// No task gets less than this, even when running late.
constexpr uint32_t MIN_TASK_MS = 60000;

// Wait this long on the first overview for the NTP clock before falling
// back to the ideal durations.
constexpr uint32_t CLOCK_WAIT_MS = 15000;

// Test runs (long hold outside the morning window) run the task timer this
// many times faster: the 56 min routine takes under 3 min.
constexpr uint32_t TEST_TIME_SCALE = 20;

constexpr uint32_t INTRO_MS = 3000;          // overview before each task
constexpr uint32_t HURRY_AT_MS = 60000;      // hurry screen at 1:00 left in the task
constexpr uint32_t HURRY_BLINK_MS = 10000;    // blink continuously for 10 s…
constexpr uint32_t HURRY_REMIND_MS = 5000;   // …then flash + beep once every 5 s
constexpr uint32_t HURRY_REMIND_ON_MS = 400;
constexpr uint8_t REWARD_REPEATS = 2;        // star animation ×2 (~1.8 s)
// Finale surprise length: oled::SURPRISE_MS (10 s), then the dino.
constexpr uint32_t EVOLVE_MS = 3000;         // stage-up animation
constexpr uint32_t DINO_SCREEN_MS = 30000;   // dino pet, then the screen turns off
constexpr uint32_t SLEEP_SCREEN_MS = 30000;  // sleep screen, then off; a press wakes it
constexpr uint32_t PEEK_HOLD_MS = 1000;
constexpr uint32_t LONG_HOLD_MS = 5000;  // restart / test run

// --------------------------------------------------
// Button state
// --------------------------------------------------

// Arcade microswitches bounce; after any accepted change the pin is ignored for this long.
constexpr unsigned long DEBOUNCE_MS = 30;

bool btnDown = false;
unsigned long btnDownSince = 0;
unsigned long btnChangeAt = 0;
bool longHoldFired = false;  // the current hold already restarted / started a test

// --------------------------------------------------
// Buzzer (non-blocking note sequencer)
// --------------------------------------------------

struct Note {
  uint16_t hz;    // 0 = rest
  uint16_t ms;
  uint16_t toHz;  // non-zero = linear sweep from hz to toHz
};

const Note SND_START[] = { { 1568, 60, 0 } };
const Note SND_HURRY[] = { { 880, 80, 0 }, { 0, 80, 0 }, { 880, 80, 0 } };
// C6–E6–G6, one note at the start of each reward frame (0 / 200 / 400 ms).
const Note SND_REWARD[] = { { 1047, 80, 0 }, { 0, 120, 0 }, { 1319, 80, 0 }, { 0, 120, 0 }, { 1568, 80, 0 } };
const Note SND_CREAK[] = { { 400, 300, 800 } };
// G5 C6 E6 G6 — C6
// Rising sweep while the dino changes, then a short "ta-da".
const Note SND_EVOLVE[] = { { 400, 1200, 1200 }, { 0, 150, 0 }, { 1568, 100, 0 }, { 0, 60, 0 }, { 2093, 300, 0 } };
const Note SND_FANFARE[] = { { 784, 120, 0 }, { 1047, 120, 0 }, { 1319, 120, 0 }, { 1568, 120, 0 }, { 0, 120, 0 }, { 1047, 240, 0 } };

// Finale surprises (order of oled::Surprise).
// Rocket: three countdown beeps, then a long rising whoosh.
const Note SND_ROCKET[] = { { 880, 100, 0 }, { 0, 570, 0 }, { 880, 100, 0 }, { 0, 570, 0 }, { 1760, 200, 0 }, { 0, 460, 0 }, { 200, 3000, 1600 } };
// Fireworks: whistle up, pop — for the first two bursts.
const Note SND_FIREWORKS[] = { { 800, 350, 2000 }, { 0, 50, 0 }, { 3000, 40, 0 }, { 0, 160, 0 }, { 800, 350, 2000 }, { 0, 50, 0 }, { 3000, 40, 0 }, { 0, 300, 0 }, { 900, 350, 2200 }, { 0, 50, 0 }, { 3000, 40, 0 } };
const Note SND_DANCE[] = { { 1047, 150, 0 }, { 0, 150, 0 }, { 1319, 150, 0 }, { 0, 150, 0 }, { 1568, 150, 0 }, { 1319, 150, 0 }, { 1047, 150, 0 }, { 0, 150, 0 }, { 1568, 150, 0 }, { 1760, 150, 0 }, { 2093, 300, 0 } };
const Note SND_RAINBOW[] = { { 1047, 140, 0 }, { 1175, 140, 0 }, { 1319, 140, 0 }, { 1397, 140, 0 }, { 1568, 140, 0 }, { 1760, 140, 0 }, { 1976, 140, 0 }, { 2093, 400, 0 } };
const Note SND_BALLOONS[] = { { 1568, 100, 0 }, { 1319, 100, 0 }, { 1047, 100, 0 }, { 1319, 100, 0 }, { 1568, 300, 0 }, { 0, 200, 0 }, { 600, 250, 1800 } };
const Note SND_FISH[] = { { 600, 80, 1200 }, { 0, 150, 0 }, { 700, 80, 1400 }, { 0, 150, 0 }, { 500, 80, 1000 }, { 0, 300, 0 }, { 650, 80, 1300 } };
const Note SND_SPACE[] = { { 2000, 600, 800 }, { 800, 600, 2000 }, { 0, 200, 0 }, { 2000, 600, 800 } };
const Note SND_RACECAR[] = { { 200, 2500, 900 }, { 300, 1600, 1100 }, { 0, 2500, 0 }, { 1200, 1100, 300 } };
const Note SND_TRAIN[] = { { 1480, 250, 0 }, { 0, 100, 0 }, { 1480, 700, 0 }, { 0, 400, 0 }, { 1480, 250, 0 }, { 0, 100, 0 }, { 1480, 700, 0 } };
const Note SND_UFO[] = { { 800, 200, 1600 }, { 1600, 200, 800 }, { 800, 200, 1600 }, { 1600, 200, 800 }, { 0, 600, 0 }, { 400, 1400, 2400 } };
const Note SND_VOLCANO[] = { { 100, 1200, 150 }, { 0, 100, 0 }, { 300, 500, 80 } };
const Note SND_SKATE[] = { { 300, 4500, 400 }, { 0, 200, 0 }, { 800, 500, 2000 }, { 0, 1950, 0 }, { 1568, 120, 0 }, { 2093, 300, 0 } };
#define TICK { 2500, 15, 0 }, { 0, 85, 0 }
#define TICK5 TICK, TICK, TICK, TICK, TICK
// Jackpot: slot machine ticks while the reels spin, then a big fanfare.
const Note SND_JACKPOT[] = { TICK5, TICK5, TICK5, TICK, TICK, TICK, TICK,
  { 1568, 120, 0 }, { 0, 40, 0 }, { 1568, 120, 0 }, { 0, 40, 0 }, { 1568, 120, 0 }, { 0, 40, 0 }, { 2093, 600, 0 },
  { 0, 500, 0 }, { 784, 120, 0 }, { 1047, 120, 0 }, { 1319, 120, 0 }, { 1568, 120, 0 }, { 2093, 400, 0 } };
#undef TICK5
#undef TICK

struct Sound {
  const Note* seq;
  uint8_t len;
};
#define SOUND(seq) { seq, sizeof(seq) / sizeof(seq[0]) }
const Sound SURPRISE_SOUNDS[oled::NUM_SURPRISES] = {
  SOUND(SND_ROCKET), SOUND(SND_FIREWORKS), SOUND(SND_DANCE), SOUND(SND_RAINBOW),
  SOUND(SND_BALLOONS), SOUND(SND_FISH), SOUND(SND_SPACE), SOUND(SND_RACECAR), SOUND(SND_TRAIN),
  SOUND(SND_UFO), SOUND(SND_VOLCANO), SOUND(SND_SKATE), SOUND(SND_JACKPOT),
};
#undef SOUND

#define PLAY(seq) playSound(seq, sizeof(seq) / sizeof(seq[0]))

const Note* soundSeq = nullptr;
uint8_t soundLen = 0;
uint8_t soundIdx = 0;
unsigned long noteStart = 0;
uint32_t buzzerHz = 0;

// --------------------------------------------------
// Routine state
// --------------------------------------------------

enum class Phase : uint8_t { Intro, Active, Hurry, Reward, Leave, Finale, Evolve, Dino, Sleep };

Phase phase = Phase::Intro;
unsigned long phaseStart = 0;
uint8_t task = 0;
unsigned long taskStart = 0;
uint32_t taskMs = 0;         // this task's slot
bool fitToLeave = false;     // true = slots are fitted to LEAVE_TIME
bool testRun = false;        // long hold outside the morning window: start now, sped up
uint32_t timeScale = 1;      // 1 = real time, TEST_TIME_SCALE during a test run

bool peeking = false;
unsigned long peekStart = 0;

int rewardCycle = -1;
int hurryRemind = -1;
bool creakPlayed = false;

// --------------------------------------------------
// Dino pet state (saved in flash, namespace "routine")
// --------------------------------------------------

Preferences prefs;
uint16_t mornings = 0;        // finished mornings, saved
int32_t lastMorningDay = -1;  // dayKey() of the last counted morning, saved
bool countedThisBoot = false; // without a clock, count at most once per boot
uint16_t shownMornings = 0;   // what the dino screens show (test runs add to it)
bool evolvedToday = false;    // the last count reached a new stage
uint8_t surprise = 0;         // finale surprise playing now
int8_t lastSurprise = -1;     // last morning's surprise, saved (no repeats)

// --------------------------------------------------
// Display state
// --------------------------------------------------

oled::Bm frameBuf(128, 64);
char clockText[8] = "";
bool timeSynced = false;

bool displayOn = true;
bool hwInverted = false;

struct FrameKey {
  int8_t screen;
  int8_t frame;
  int8_t task;
  int16_t remQ;
  int32_t secs;
  int16_t clockMin;
};

FrameKey lastKey = { -1, -1, -1, -1, -1, -1 };

// --------------------------------------------------
// Helpers
// --------------------------------------------------

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Intro: return "INTRO";
    case Phase::Active: return "ACTIVE";
    case Phase::Hurry: return "HURRY";
    case Phase::Reward: return "REWARD";
    case Phase::Leave: return "LEAVE";
    case Phase::Finale: return "FINALE";
    case Phase::Evolve: return "EVOLVE";
    case Phase::Dino: return "DINO";
    default: return "SLEEP";
  }
}

uint32_t taskRemainingMs(unsigned long now) {
  uint32_t elapsed = (now - taskStart) * timeScale;
  return elapsed >= taskMs ? 0 : taskMs - elapsed;
}

// Milliseconds from now until today's LEAVE_TIME (negative once it has passed).
int32_t msUntilLeave() {
  time_t now = time(nullptr);
  struct tm leave;
  localtime_r(&now, &leave);
  leave.tm_hour = LEAVE_HOUR;
  leave.tm_min = LEAVE_MINUTE;
  leave.tm_sec = 0;
  return (int32_t)difftime(mktime(&leave), now) * 1000;
}

bool inMorningWindow() {
  int32_t left = msUntilLeave();
  return left > 0 && (uint32_t)left <= PLAN_WINDOW_MS;
}

// Without a clock the routine runs anyway, so treat it as morning.
bool morningNow() {
  return !timeSynced || inMorningWindow();
}

// Slot for task i: the time left until LEAVE_TIME, shared by tasks i..last in
// the ratio of their ideal durations, capped at the ideal duration.
uint32_t planTaskMs(uint8_t i) {
  uint32_t ideal = (uint32_t)oled::TASKS[i].durSec * 1000UL;
  if (!fitToLeave) return ideal;

  uint32_t idealRest = 0;
  for (uint8_t k = i; k < oled::NUM_TASKS; k++) idealRest += oled::TASKS[k].durSec;

  int32_t left = msUntilLeave();
  if (left <= 0) return MIN_TASK_MS;

  uint32_t slot = (uint64_t)left * oled::TASKS[i].durSec / idealRest;
  slot = min(slot, ideal);
  return max(slot, MIN_TASK_MS);
}

void printMinSec(uint32_t ms) {
  uint32_t s = (ms + 500) / 1000;
  Serial.print(s / 60);
  Serial.print(':');
  if (s % 60 < 10) Serial.print('0');
  Serial.print(s % 60);
}

void enterPhase(Phase p) {
  phase = p;
  phaseStart = millis();
  peeking = false;
  lastKey.screen = -1;  // force redraw

  Serial.print("[ROUTINE] -> ");
  Serial.print(phaseName(p));
  Serial.print(" (task ");
  Serial.print(task + 1);
  Serial.println(")");
}

void startTask(uint8_t i) {
  task = i;

  // Decide once, at the first task, whether to fit the routine to LEAVE_TIME.
  if (i == 0) {
    fitToLeave = !testRun && timeSynced && inMorningWindow();
    Serial.println(fitToLeave
      ? "[ROUTINE] Morning window: tasks shrink if running late"
      : testRun
        ? "[ROUTINE] Test run: ideal durations, sped up"
        : "[ROUTINE] No clock: ideal durations");
  }

  taskMs = planTaskMs(i);
  taskStart = millis();

  Serial.print("[ROUTINE] Task ");
  Serial.print(i + 1);
  Serial.print(" slot ");
  printMinSec(taskMs);
  Serial.println();

  enterPhase(Phase::Active);
  PLAY(SND_START);
}

// Back to the first task; slots are planned when each task starts.
// Outside the morning window this ends on the sleep screen unless it is a test run.
void startRoutine(bool test = false) {
  task = 0;
  testRun = test;
  timeScale = test ? TEST_TIME_SCALE : 1;
  enterPhase(Phase::Intro);
}

// Long hold: restart in the morning, test run at any other time.
void onLongHold() {
  wakeDisplay();
  if (morningNow()) {
    Serial.println("[ROUTINE] Restart");
    startRoutine();
  } else {
    Serial.print("[ROUTINE] Test run (x");
    Serial.print(TEST_TIME_SCALE);
    Serial.println(")");
    startRoutine(true);
  }
}

// Calendar day as year * 1000 + day of the year, or -1 before the first NTP sync.
int32_t dayKey() {
  if (!timeSynced) return -1;
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  return (local.tm_year + 1900) * 1000 + local.tm_yday;
}

// The routine was finished: grow the dino, once per day. A restart and a
// second finish on the same day does not count again. Test runs only add
// to shownMornings, so each one previews one more morning.
void countMorning() {
  uint16_t before = testRun ? shownMornings : mornings;

  if (testRun) {
    shownMornings++;
  } else {
    int32_t day = dayKey();
    bool already = day >= 0 ? day == lastMorningDay : countedThisBoot;
    if (!already) {
      mornings++;
      countedThisBoot = true;
      if (day >= 0) lastMorningDay = day;
      prefs.putUShort("mornings", mornings);
      prefs.putInt("lastDay", lastMorningDay);
    }
    shownMornings = mornings;
  }

  evolvedToday = oled::dinoStage(shownMornings) != oled::dinoStage(before);

  Serial.print("[DINO] Mornings: ");
  Serial.print(shownMornings);
  Serial.print(testRun ? " (test, not saved)" : "");
  Serial.print(", stage ");
  Serial.print(oled::dinoStage(shownMornings));
  Serial.println(evolvedToday ? " (evolved!)" : "");
}

// --------------------------------------------------
// Buzzer
// --------------------------------------------------

// Shift every note up this many octaves. Small passive buzzers are loudest
// around 2–4 kHz, so one octave up (1–3 kHz) is much louder than the design's
// pitches. 0 = original pitches.
constexpr uint8_t BUZZER_OCTAVES_UP = 1;

// While silent the pin is detached from LEDC and held LOW as a plain output,
// so nothing (PWM glitches, crosstalk) can reach the buzzer between sounds.
void setBuzzer(uint32_t hz) {
  if (hz == buzzerHz) return;

  if (hz == 0) {
    ledcDetach(BUZZER_PIN);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    uint32_t out = hz << BUZZER_OCTAVES_UP;
    if (buzzerHz == 0) ledcAttach(BUZZER_PIN, out, 10);
    ledcWriteTone(BUZZER_PIN, out);
  }

  buzzerHz = hz;
}

void playSound(const Note* seq, uint8_t len) {
  soundSeq = seq;
  soundLen = len;
  soundIdx = 0;
  noteStart = millis();
  setBuzzer(seq[0].hz);
}

void updateSound() {
  if (!soundSeq) return;

  unsigned long now = millis();

  while (now - noteStart >= soundSeq[soundIdx].ms) {
    noteStart += soundSeq[soundIdx].ms;
    soundIdx++;
    if (soundIdx >= soundLen) {
      soundSeq = nullptr;
      setBuzzer(0);
      return;
    }
  }

  const Note& n = soundSeq[soundIdx];
  if (n.toHz && n.hz) {
    uint32_t e = now - noteStart;
    setBuzzer(n.hz + ((int32_t)n.toHz - n.hz) * (int32_t)e / n.ms);
  } else {
    setBuzzer(n.hz);
  }
}

// --------------------------------------------------
// Display
// --------------------------------------------------

void setHwInvert(bool on) {
  if (on == hwInverted) return;
  hwInverted = on;
  // Controller inversion (works on SH1106 and SSD1306): no redraw needed.
  u8g2.sendF("c", on ? 0xA7 : 0xA6);
}

void sleepDisplay() {
  if (!displayOn) return;
  Serial.println("[OLED] Screen off");
  setHwInvert(false);
  u8g2.setPowerSave(1);
  displayOn = false;
}

void wakeDisplay() {
  if (displayOn) return;
  Serial.println("[OLED] Screen on");
  u8g2.setPowerSave(0);
  displayOn = true;
  lastKey.screen = -1;
}

// Copies the 1-byte-per-pixel frame into U8g2's page buffer and sends it.
void pushFrame(const oled::Bm& bm) {
  uint8_t* buf = u8g2.getBufferPtr();
  const int bufWidth = u8g2.getBufferTileWidth() * 8;
  memset(buf, 0, bufWidth * u8g2.getBufferTileHeight());

  for (int y = 0; y < bm.h; y++) {
    uint8_t* row = buf + (y >> 3) * bufWidth;
    uint8_t mask = 1 << (y & 7);
    for (int x = 0; x < bm.w; x++) {
      if (bm.d[y * bm.w + x]) row[x] |= mask;
    }
  }

  u8g2.sendBuffer();
}

// Fills clockText ("7:42") and returns the minute of the day, or -1 before the first NTP sync.
int updateClockText() {
  if (!timeSynced) {
    clockText[0] = '\0';
    return -1;
  }

  time_t t = time(nullptr);
  struct tm local;
  localtime_r(&t, &local);
  snprintf(clockText, sizeof(clockText), "%d:%02d", local.tm_hour, local.tm_min);
  return local.tm_hour * 60 + local.tm_min;
}

void updateScreen() {
  if (!displayOn) return;

  unsigned long now = millis();

  oled::ScreenId screen;
  unsigned long t = now - phaseStart;

  switch (phase) {
    case Phase::Intro: screen = oled::SCR_OVERVIEW; break;
    case Phase::Active: screen = oled::SCR_ACTIVE; break;
    case Phase::Hurry: screen = oled::SCR_HURRY; break;
    case Phase::Reward: screen = oled::SCR_REWARD; break;
    case Phase::Leave: screen = oled::SCR_LEAVE; break;
    case Phase::Finale: screen = oled::SCR_FINALE; break;
    case Phase::Evolve: screen = oled::SCR_EVOLVE; break;
    case Phase::Dino: screen = oled::SCR_DINO; break;
    default: screen = oled::SCR_SLEEP; break;
  }

  if (peeking) {
    screen = oled::SCR_OVERVIEW;
    t = now - peekStart;
  }

  // The finale surprise animates by time: one frame per SURPRISE_FRAME_MS.
  int f = screen == oled::SCR_FINALE
    ? (int)min(t / oled::SURPRISE_FRAME_MS, 127UL)
    : oled::frameAt(screen, t);

  // Hurry: blink with the hourglass frames for 10 s, then once every 5 s.
  if (screen == oled::SCR_HURRY) {
    bool inv = t < HURRY_BLINK_MS
      ? f == 0
      : (t - HURRY_BLINK_MS) % HURRY_REMIND_MS < HURRY_REMIND_ON_MS;
    setHwInvert(inv);
  } else {
    setHwInvert(false);
  }

  uint32_t remMs = taskRemainingMs(now);
  double rem = taskMs ? (double)remMs / taskMs : 0;
  double secsLeft = remMs / 1000.0;

  int clockMin = updateClockText();

  FrameKey key = {
    (int8_t)screen,
    (int8_t)f,
    (int8_t)task,
    (int16_t)oled::jsRound(rem * 400),
    (int32_t)oled::jsRound(secsLeft),
    (int16_t)clockMin,
  };

  if (memcmp(&key, &lastKey, sizeof(key)) == 0) return;
  lastKey = key;

  oled::Opts o = { task, rem, secsLeft, clockText, false, shownMornings };
  if (screen == oled::SCR_FINALE) oled::renderSurprise(frameBuf, surprise, f * oled::SURPRISE_FRAME_MS, o);
  else oled::renderScreen(frameBuf, screen, f, o);
  pushFrame(frameBuf);
}

// --------------------------------------------------
// WiFi / NTP clock
// --------------------------------------------------

void startTimeSync() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // ESP32-C3 Super Mini: full TX power can brown out the small regulator
  // (resets in a loop / USB port keeps reconnecting). 8.5 dBm is plenty indoors.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // SNTP starts once WiFi is up; non-blocking.
  configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
}

void updateTimeSync() {
  if (timeSynced) return;

  // Any time after 2023 means SNTP has set the clock.
  if (time(nullptr) < 1700000000) return;

  timeSynced = true;
  updateClockText();
  Serial.print("[WIFI] Time synced: ");
  Serial.println(clockText);

  if (WIFI_OFF_AFTER_SYNC) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] WiFi off");
  }
}

// --------------------------------------------------
// Routine logic
// --------------------------------------------------

// Plays finale surprise `id`. save: remember it, so tomorrow's is different.
void startSurprise(int id, bool save) {
  surprise = id;
  if (save) {
    lastSurprise = id;
    prefs.putChar("lastSurp", lastSurprise);
  }
  Serial.print("[SURPRISE] ");
  Serial.println(oled::surpriseName(id));
  wakeDisplay();
  enterPhase(Phase::Finale);
  playSound(SURPRISE_SOUNDS[id].seq, SURPRISE_SOUNDS[id].len);
}

// Serial Monitor: "s" = random surprise, "s0" … "s12" = that one.
// Preview only: nothing is saved and the dino does not grow.
void updateSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (!cmd.startsWith("s")) return;
  int id = cmd.length() > 1 ? cmd.substring(1).toInt() : oled::pickSurprise(esp_random(), surprise);
  if (id < 0 || id >= oled::NUM_SURPRISES) return;
  evolvedToday = false;
  startSurprise(id, false);
}

void onDonePressed() {
  switch (phase) {
    case Phase::Active:
    case Phase::Hurry:
      rewardCycle = -1;
      enterPhase(Phase::Reward);
      break;

    case Phase::Leave:
      countMorning();
      startSurprise(oled::pickSurprise(esp_random(), lastSurprise), !testRun);
      break;

    case Phase::Dino:
      if (!displayOn) {
        wakeDisplay();
        enterPhase(Phase::Dino);  // show the dino for another 30 s
      }
      break;

    default:
      break;  // ignore presses on the overview and reward screens
  }
}

void updateRoutine() {
  unsigned long now = millis();
  unsigned long e = now - phaseStart;

  switch (phase) {
    case Phase::Intro:
      if (e < INTRO_MS) break;
      if (task > 0 || testRun) {
        startTask(task);
      } else if (timeSynced) {
        if (inMorningWindow()) startTask(0);
        else enterPhase(Phase::Sleep);
      } else if (e >= CLOCK_WAIT_MS) {
        // No clock: better to run the routine than to sleep through a morning.
        startTask(0);
      }
      break;

    case Phase::Active:
      if (taskRemainingMs(now) <= HURRY_AT_MS) {
        hurryRemind = -1;
        enterPhase(Phase::Hurry);
        PLAY(SND_HURRY);
      }
      break;

    case Phase::Hurry:
      // Stays on 0:00 until the task is marked done. After the continuous
      // blink, beep with each reminder flash.
      if (e >= HURRY_BLINK_MS) {
        int remind = (e - HURRY_BLINK_MS) / HURRY_REMIND_MS;
        if (remind != hurryRemind) {
          hurryRemind = remind;
          PLAY(SND_HURRY);
        }
      }
      break;

    case Phase::Reward: {
      int cycle = e / oled::cycleMs(oled::SCR_REWARD);
      if (cycle >= REWARD_REPEATS) {
        if (task + 1 < oled::NUM_TASKS) {
          task++;
          enterPhase(Phase::Intro);
        } else {
          creakPlayed = false;
          enterPhase(Phase::Leave);
        }
      } else if (cycle != rewardCycle) {
        rewardCycle = cycle;
        PLAY(SND_REWARD);
      }
      break;
    }

    case Phase::Leave:
      // Door creak with the "ajar" frame (f2), on the first opening only.
      if (!creakPlayed && e >= oled::FRAMES_LEAVE[0]) {
        creakPlayed = true;
        PLAY(SND_CREAK);
      }
      break;

    case Phase::Finale:
      if (e < oled::SURPRISE_MS) break;
      if (evolvedToday) {
        enterPhase(Phase::Evolve);
        PLAY(SND_EVOLVE);
      } else {
        enterPhase(Phase::Dino);
      }
      break;

    case Phase::Evolve:
      if (e >= EVOLVE_MS) {
        enterPhase(Phase::Dino);
        PLAY(SND_REWARD);
      }
      break;

    case Phase::Dino:
      if (displayOn && e >= DINO_SCREEN_MS) sleepDisplay();
      break;

    case Phase::Sleep:
      if (displayOn && e >= SLEEP_SCREEN_MS) sleepDisplay();
      break;
  }
}

// --------------------------------------------------
// Buttons
// --------------------------------------------------

// A pin interrupt latches every press edge, so a quick tap that starts and
// ends while the loop is busy drawing a frame is still counted.
volatile bool tapLatched = false;
volatile uint32_t tapAt = 0;

void IRAM_ATTR onButtonEdge() {
  tapAt = millis();
  tapLatched = true;
}

void onButtonPressed() {
  Serial.println("[BUTTON] Pressed");

  longHoldFired = false;
  if (phase == Phase::Sleep) {
    wakeDisplay();
    enterPhase(Phase::Sleep);  // show the sleep screen for another 30 s
  }
}

void onButtonReleased(unsigned long heldMs) {
  if (longHoldFired) return;  // restart / test run already happened

  if (peeking) {
    Serial.println("[BUTTON] Overview peek ended");
    peeking = false;
    lastKey.screen = -1;
    return;
  }

  if (heldMs < PEEK_HOLD_MS) onDonePressed();
}

void updateButtons() {
  unsigned long now = millis();

  // Lockout debounce: act on the first change right away, then ignore the
  // pin for DEBOUNCE_MS. Works even when frames make the loop slow.
  if (now - btnChangeAt >= DEBOUNCE_MS) {
    bool down = digitalRead(BUTTON_PIN) == LOW;  // LOW = pressed

    if (down != btnDown) {
      btnDown = down;
      btnChangeAt = now;
      if (down) {
        tapLatched = false;
        btnDownSince = now;
        onButtonPressed();
      } else {
        onButtonReleased(now - btnDownSince);
      }
    } else if (!down && tapLatched) {
      // Pressed and released between two polls. Ignore edges that are just
      // contact bounce from the last release.
      tapLatched = false;
      if ((int32_t)(tapAt - btnChangeAt) > (int32_t)DEBOUNCE_MS) {
        Serial.print("[BUTTON] Quick tap caught by interrupt: ");
        btnChangeAt = now;
        onButtonPressed();
        onButtonReleased(0);
      }
    }
  }

  if (!btnDown || longHoldFired) return;
  unsigned long held = now - btnDownSince;

  // Holding for 5 s restarts (morning) or starts a test run (otherwise).
  if (held >= LONG_HOLD_MS) {
    longHoldFired = true;
    onLongHold();
    return;
  }

  // Holding for 1 s during a task shows the overview.
  if (
    !peeking && held >= PEEK_HOLD_MS &&
    (phase == Phase::Active || phase == Phase::Hurry)
  ) {
    Serial.println("[BUTTON] Overview peek");
    peeking = true;
    peekStart = now;
  }
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);  // serial commands must not stall the loop
  // Never wait to send logs. Plugged into a computer with no Serial Monitor
  // open, each print would otherwise block up to 2 s (20 × 100 ms) and the
  // routine froze with the buzzer stuck on.
  Serial.setTxTimeoutMs(0);
  delay(200);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("            MORNING ROUTINE INITIALIZATION        ");
  Serial.println("==================================================");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, onButtonEdge, FALLING);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // silent until the first sound

  Serial.print("[SETUP] Initializing OLED (SDA = GPIO ");
  Serial.print(OLED_SDA);
  Serial.print(", SCL = GPIO ");
  Serial.print(OLED_SCL);
  Serial.print(", address 0x");
  Serial.print(OLED_ADDRESS, HEX);
  Serial.println(")...");

  u8g2.setI2CAddress(OLED_ADDRESS * 2);
  u8g2.setBusClock(400000);
  if (!u8g2.begin()) {
    Serial.println("[ERROR] OLED initialization failed! Check I2C address/wiring.");
  }

  prefs.begin("routine", false);
  mornings = prefs.getUShort("mornings", 0);
  lastMorningDay = prefs.getInt("lastDay", -1);
  lastSurprise = prefs.getChar("lastSurp", -1);
  shownMornings = mornings;
  Serial.print("[DINO] Mornings so far: ");
  Serial.print(mornings);
  Serial.print(", stage ");
  Serial.println(oled::dinoStage(mornings));

  startTimeSync();

  Serial.println("[SETUP] Button = Feito (hold 1 s = overview, 5 s = restart / test run)");
  Serial.println("==================================================");

  startRoutine();
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  updateButtons();
  updateSerialCommands();
  updateTimeSync();
  updateRoutine();
  updateSound();
  updateScreen();
}
