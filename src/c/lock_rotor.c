#include "icebreaker.h"

// ROTOR, the timing lock: a pick sweeps round each tumbler ring in turn, and Select sets
// the pin while it's inside the lit gate. The rings speed up and the gates shrink.
#define MAX_RINGS 5
#define OUTER_R 86
#define RING_STEP 16
#define RING_W 9
#define FLASH_MS 280
#define SLACK_DEG 3         // Forgives the width of the pick itself
#define PERFECT_PCT 70
#define LIVES 3

static int s_level;
static int s_rings;
static int s_ring;          // The ring being picked; 0 is the outermost
static int32_t s_needle;    // Trig angle, 0 at twelve o'clock, running clockwise
static int32_t s_speed;     // Trig units a second, negative for anticlockwise
static int32_t s_gate;      // Centre of the sweet spot
static int32_t s_gate_half;
static int32_t s_drift;     // From level 4 the sweet spot wanders too
static int s_flash_ms;
static bool s_flash_good;
static bool s_perfect;

static int32_t prv_wrap(int32_t angle) {
  angle %= TRIG_MAX_ANGLE;
  return angle < 0 ? angle + TRIG_MAX_ANGLE : angle;
}

static int32_t prv_gap(int32_t a, int32_t b) {
  int32_t gap = prv_wrap(a - b);
  return gap > TRIG_MAX_ANGLE / 2 ? TRIG_MAX_ANGLE - gap : gap;
}

static void prv_arm_ring(void) {
  int speed_deg = 90 + 25 * s_level + 15 * s_ring;
  if (speed_deg > 420) speed_deg = 420;
  bool backwards = s_level >= 3 ? rand() % 2 : s_ring % 2;
  s_speed = DEG_TO_TRIGANGLE(speed_deg) * (backwards ? -1 : 1);

  int half_deg = 26 - 2 * s_level - 2 * s_ring;
  if (half_deg < 8) half_deg = 8;
  s_gate_half = DEG_TO_TRIGANGLE(half_deg);
  s_gate = rand() % TRIG_MAX_ANGLE;
  // Somewhere on the far side of the gate, so the wait for it can't be learned.
  s_needle = prv_wrap(s_gate + DEG_TO_TRIGANGLE(rand_range(100, 260)));

  s_drift = 0;
  if (s_level >= 4) {
    int drift_deg = 8 * s_level;
    if (drift_deg > 70) drift_deg = 70;
    s_drift = DEG_TO_TRIGANGLE(drift_deg) * (rand() % 2 ? 1 : -1);
  }
}

static void prv_start(int level) {
  s_level = level;
  s_rings = 3 + (level - 1) / 2;
  if (s_rings > MAX_RINGS) s_rings = MAX_RINGS;
  s_ring = 0;
  s_flash_ms = 0;
  g_play.lives = g_play.lives_max = LIVES;
  prv_arm_ring();
}

static void prv_tick(int dt) {
  s_needle = prv_wrap(s_needle + s_speed * dt / 1000);
  s_gate = prv_wrap(s_gate + s_drift * dt / 1000);
  if (s_flash_ms > 0) s_flash_ms -= dt;
}

static void prv_press(ButtonId button) {
  if (button != BUTTON_ID_SELECT) return;

  int32_t reach = s_gate_half + DEG_TO_TRIGANGLE(SLACK_DEG);
  int32_t miss = prv_gap(s_needle, s_gate);
  s_flash_ms = FLASH_MS;

  if (miss > reach) {
    s_flash_good = false;
    s_perfect = false;
    if (--g_play.lives <= 0) {
      g_play.state = PLAY_FAILED;
    } else {
      buzz(BUZZ_BAD);
    }
    return;
  }

  int precision = 100 - miss * 100 / reach;
  s_perfect = precision >= PERFECT_PCT;
  s_flash_good = true;
  g_play.score += 20 * s_level + precision * 3 / 10 + (s_perfect ? 20 : 0);
  s_ring++;

  if (s_ring >= s_rings) {
    g_play.score += 50 * s_level + 25 * g_play.lives;
    g_play.state = PLAY_CRACKED;
    return;
  }
  buzz(BUZZ_TICK);
  prv_arm_ring();
}

static GRect prv_circle(GPoint center, int radius) {
  return GRect(center.x - radius, center.y - radius, radius * 2 + 1, radius * 2 + 1);
}

// graphics_fill_radial wants start < end within one turn, so an arc over twelve o'clock
// goes in two pieces.
static void prv_arc(GContext *ctx, GRect rect, int thickness, int32_t center, int32_t half) {
  int32_t start = prv_wrap(center - half);
  int32_t end = start + 2 * half;
  if (end <= TRIG_MAX_ANGLE) {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, thickness, start, end);
  } else {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, thickness, start, TRIG_MAX_ANGLE);
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, thickness, 0, end - TRIG_MAX_ANGLE);
  }
}

static void prv_ring(GContext *ctx, GPoint center, int radius, int thickness, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_radial(ctx, prv_circle(center, radius), GOvalScaleModeFitCircle, thickness, 0,
                       TRIG_MAX_ANGLE);
}

static void prv_draw(GContext *ctx, GRect area) {
  GPoint center = GPoint(area.origin.x + area.size.w / 2, area.origin.y + area.size.h / 2 - 2);
  bool flashing = s_flash_ms > 0;

  for (int i = 0; i < s_rings; i++) {
    int radius = OUTER_R - i * RING_STEP;
    // The rim of the rings is a thin dark line, so unset rings still read as rings.
    if (i < s_ring) {
      bool just_set = flashing && s_flash_good && i == s_ring - 1;
      prv_ring(ctx, center, radius - 1, RING_W - 2, just_set ? GColorWhite : COL_YELLOW);
    } else if (i > s_ring) {
      prv_ring(ctx, center, radius - RING_W / 2, 2, COL_TRACK);
    }
  }

  if (s_ring < s_rings) {
    int radius = OUTER_R - s_ring * RING_STEP;
    bool missed = flashing && !s_flash_good;
    prv_ring(ctx, center, radius - RING_W / 2 + 1, 3, missed ? COL_RED : COL_DIM);
    graphics_context_set_fill_color(ctx, COL_CYAN);
    prv_arc(ctx, prv_circle(center, radius), RING_W, s_gate, s_gate_half);

    GPoint tip = gpoint_from_polar(prv_circle(center, radius + 5), GOvalScaleModeFitCircle,
                                   s_needle);
    GPoint tail = gpoint_from_polar(prv_circle(center, radius - RING_W - 5),
                                    GOvalScaleModeFitCircle, s_needle);
    graphics_context_set_stroke_color(ctx, missed ? COL_RED : COL_YELLOW);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, tail, tip);
    graphics_context_set_fill_color(ctx, missed ? COL_RED : COL_YELLOW);
    graphics_fill_circle(ctx, tip, 3);
  }

  bool open = s_ring >= s_rings;
  ui_hexagon(ctx, center, 9, open ? COL_CYAN : COL_PINK, open);

  char text[12];
  snprintf(text, sizeof(text), "PIN %d/%d", s_ring < s_rings ? s_ring + 1 : s_rings, s_rings);
  ui_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
          GRect(area.origin.x + 4, area.origin.y - 2, 80, 16), GTextAlignmentLeft, COL_CYAN);
  if (flashing) {
    const char *call = s_flash_good ? (s_perfect ? "PERFECT" : "SET") : "SLIP";
    ui_text(ctx, call, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(area.origin.x + area.size.w - 84, area.origin.y - 2, 80, 16),
            GTextAlignmentRight, s_flash_good ? COL_YELLOW : COL_RED);
  }
}

const LockDef LOCK_DEF_ROTOR = {
  .name = "ROTOR",
  .skill = "TIMING",
  .brief = "Rings spin a pick around the lock. Set each pin while the pick is in the lit "
           "gate.",
  .controls = "SELECT: SET PIN\n3 SLIPS AND THE PICK SNAPS",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press
};
