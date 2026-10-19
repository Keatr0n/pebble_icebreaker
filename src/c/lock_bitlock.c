#include "icebreaker.h"

// BITLOCK, the arithmetic lock: flip the eight bits of a register until it holds the
// target. Targets go from plain decimal to hex to sums, the place values stop being
// printed, and the register starts scrambled.
#define BITS 8
#define SWITCH_W 22
#define SWITCH_H 46
#define NIBBLE_GAP 8
#define FLASH_MS 450

typedef enum {
  FMT_DEC,
  FMT_HEX,
  FMT_SUM,
  FMT_DIFF
} Format;

static int s_level;
static int s_rounds;
static int s_round;
static uint8_t s_value;
static uint8_t s_target;
static Format s_format;
static int s_a;
static int s_b;
static int s_cursor;        // 0 is the leftmost switch, bit 7
static int s_time_ms;
static int s_limit_ms;
static int s_flash_ms;

static void prv_new_target(void) {
  int pick = rand() % 4;
  if (s_level <= 1) {
    s_format = FMT_DEC;
    s_target = rand_range(3, 63);
  } else if (s_level == 2) {
    s_format = FMT_DEC;
    s_target = rand_range(16, 255);
  } else if (s_level <= 4) {
    s_format = pick < 2 ? FMT_HEX : FMT_DEC;
    s_target = rand_range(16, 255);
  } else {
    s_format = (Format)pick;
    s_target = rand_range(16, 255);
  }

  if (s_format == FMT_SUM) {
    s_a = rand_range(s_target / 4, s_target * 3 / 4);
    s_b = s_target - s_a;
  } else if (s_format == FMT_DIFF) {
    s_b = rand_range(5, 120);
    s_a = s_target + s_b;
  }

  // From level 4 the register starts scrambled, so some bits have to come off.
  s_value = s_level >= 4 ? rand() % 256 : 0;
  if (s_value == s_target) s_value ^= 0x81;
}

static void prv_start(int level) {
  s_level = level;
  s_rounds = 3 + (level >= 3) + (level >= 5);
  // Each round can take a dozen presses before any thinking, so the clock is generous.
  int per_round_ms = 30000 - 1500 * level;
  if (per_round_ms < 18000) per_round_ms = 18000;
  s_limit_ms = s_rounds * per_round_ms;
  s_time_ms = 0;
  s_round = 0;
  s_cursor = 0;
  s_flash_ms = 0;
  g_play.trace = 0;
  prv_new_target();
}

static void prv_tick(int dt) {
  s_time_ms += dt;
  if (s_flash_ms > 0) s_flash_ms -= dt;
  g_play.trace = s_time_ms * 100 / s_limit_ms;
  if (s_time_ms >= s_limit_ms) g_play.state = PLAY_FAILED;
}

static void prv_press(ButtonId button) {
  if (button != BUTTON_ID_SELECT) {
    int step = button == BUTTON_ID_UP ? -1 : 1;
    s_cursor = (s_cursor + step + BITS) % BITS;
    return;
  }

  s_value ^= 1 << (BITS - 1 - s_cursor);
  if (s_value != s_target) return;

  g_play.score += 25 * s_level + 10;
  s_flash_ms = FLASH_MS;
  if (++s_round >= s_rounds) {
    int seconds_left = (s_limit_ms - s_time_ms) / 1000;
    g_play.score += 40 * s_level + 3 * seconds_left;
    g_play.state = PLAY_CRACKED;
    return;
  }
  buzz(BUZZ_TICK);
  prv_new_target();
}

static void prv_format(char *buf, size_t size, int value, bool hex) {
  if (hex) snprintf(buf, size, "0x%02X", value);
  else snprintf(buf, size, "%d", value);
}

static int prv_switch_x(int index) {
  int total = BITS * SWITCH_W + (BITS - 1) * 2 + NIBBLE_GAP;
  return (SCREEN_W - total) / 2 + index * (SWITCH_W + 2) + (index >= 4 ? NIBBLE_GAP : 0);
}

static void prv_draw(GContext *ctx, GRect area) {
  int top = area.origin.y;
  char text[16];
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  ui_text(ctx, "TARGET", small, GRect(6, top - 2, 80, 16), GTextAlignmentLeft, COL_DIM);
  snprintf(text, sizeof(text), "ROUND %d/%d", s_round < s_rounds ? s_round + 1 : s_rounds,
           s_rounds);
  ui_text(ctx, text, small, GRect(114, top - 2, 80, 16), GTextAlignmentRight, COL_DIM);

  switch (s_format) {
    case FMT_DEC: prv_format(text, sizeof(text), s_target, false); break;
    case FMT_HEX: prv_format(text, sizeof(text), s_target, true); break;
    case FMT_SUM: snprintf(text, sizeof(text), "%d+%d", s_a, s_b); break;
    case FMT_DIFF: snprintf(text, sizeof(text), "%d-%d", s_a, s_b); break;
  }
  ui_title(ctx, text, GRect(0, top + 14, SCREEN_W, 34), GTextAlignmentCenter,
           s_flash_ms > 0 ? COL_GREEN : COL_YELLOW, s_flash_ms > 0 ? 3 : 1);

  // The register read back in the target's own base, so it can be compared at a glance.
  bool hex = s_format == FMT_HEX;
  char value[8];
  prv_format(value, sizeof(value), s_value, hex);
  snprintf(text, sizeof(text), "REG %s", value);
  ui_label(ctx, text, GRect(0, top + 54, SCREEN_W, 20), GTextAlignmentCenter,
           s_value == s_target ? COL_GREEN : COL_CYAN);

  int switch_y = top + 86;
  for (int i = 0; i < BITS; i++) {
    bool on = s_value & (1 << (BITS - 1 - i));
    GRect slot = GRect(prv_switch_x(i), switch_y, SWITCH_W, SWITCH_H);
    ui_frame(ctx, slot, on ? COL_CYAN : COL_DIM, 1);
    GRect knob = on ? GRect(slot.origin.x + 3, slot.origin.y + 3, SWITCH_W - 6, SWITCH_H / 2 - 3)
                    : GRect(slot.origin.x + 3, slot.origin.y + SWITCH_H / 2, SWITCH_W - 6,
                            SWITCH_H / 2 - 3);
    ui_fill(ctx, knob, on ? COL_CYAN : COL_DIM);
    ui_text(ctx, on ? "1" : "0", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(slot.origin.x, knob.origin.y + knob.size.h / 2 - 9, SWITCH_W, 16),
            GTextAlignmentCenter, COL_BG);

    if (i == s_cursor) {
      ui_frame(ctx, grect_inset(slot, GEdgeInsets(-3)), COL_YELLOW, 2);
      ui_triangle(ctx, GPoint(slot.origin.x + SWITCH_W / 2, switch_y + SWITCH_H + 10), 5, true,
                  COL_YELLOW);
    }
  }

  // Place values are a crutch for the first two levels only.
  if (s_level <= 2) {
    static const char *const WEIGHTS[BITS] = { "128", "64", "32", "16", "8", "4", "2", "1" };
    for (int i = 0; i < BITS; i++) {
      ui_text(ctx, WEIGHTS[i], small, GRect(prv_switch_x(i) - 4, switch_y + SWITCH_H + 16,
                                            SWITCH_W + 8, 16), GTextAlignmentCenter, COL_DIM);
    }
  }
}

const LockDef LOCK_DEF_BITLOCK = {
  .name = "BITLOCK",
  .skill = "MATH",
  .brief = "Flip bits until the register matches the target. Clear every round before "
           "the trace fills.",
  .controls = "UP/DN: MOVE   SEL: FLIP BIT",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press
};
