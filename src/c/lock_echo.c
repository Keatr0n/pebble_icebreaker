#include "icebreaker.h"

// ECHO, the memory lock: three nodes, one per button, fire a pattern that has to be
// played back. Each of the three rounds adds a step, and REVERSE rounds are entered
// backwards.
#define NODES 3
#define ROUNDS 3
#define MAX_SEQ 16
#define LIVES 3
#define NODE_R 25
#define LEAD_MS 800
#define PRESS_LIT_MS 180
#define GOOD_MS 700
#define BAD_MS 1000
#define SLOT 9
#define SLOT_GAP 3
#define SLOTS_PER_ROW 8

typedef enum {
  ECHO_LEAD,     // A pause before the pattern plays
  ECHO_SHOW,
  ECHO_INPUT,
  ECHO_GOOD,
  ECHO_BAD
} Phase;

static const GColor8 NODE_COLORS[NODES] = {
  { .argb = GColorCyanARGB8 }, { .argb = GColorYellowARGB8 }, { .argb = GColorShockingPinkARGB8 }
};
static const int NODE_Y[NODES] = { 38, 97, 156 };   // From the top of the play area

static int s_level;
static uint8_t s_seq[MAX_SEQ];
static int s_base;          // Length of the first round
static int s_round;
static bool s_reverse;
static Phase s_phase;
static int s_phase_ms;
static int s_step;
static int s_lit;           // Node lit right now, or -1
static int s_lit_ms;
static int s_on_ms;
static int s_gap_ms;
static int s_input_limit;

static int prv_len(void) {
  return s_base + s_round;
}

static bool prv_round_reversed(int round) {
  if (s_level >= 5) return round >= 1;
  if (s_level >= 3) return round == ROUNDS - 1;
  return false;
}

static void prv_begin_round(void) {
  s_reverse = prv_round_reversed(s_round);
  s_phase = ECHO_LEAD;
  s_phase_ms = 0;
  s_step = 0;
  s_lit = -1;
  s_input_limit = 1400 * prv_len() + 1500;
  g_play.trace = 0;
}

static void prv_start(int level) {
  s_level = level;
  s_base = 2 + level;
  if (s_base > MAX_SEQ - ROUNDS + 1) s_base = MAX_SEQ - ROUNDS + 1;
  for (int i = 0; i < MAX_SEQ; i++) {
    // No node three times running; it reads as a stall rather than a pattern.
    do {
      s_seq[i] = rand() % NODES;
    } while (i >= 2 && s_seq[i] == s_seq[i - 1] && s_seq[i] == s_seq[i - 2]);
  }
  s_on_ms = 520 - 30 * level;
  if (s_on_ms < 220) s_on_ms = 220;
  s_gap_ms = 220 - 15 * level;
  if (s_gap_ms < 90) s_gap_ms = 90;
  s_round = 0;
  g_play.lives = g_play.lives_max = LIVES;
  prv_begin_round();
}

static void prv_tick(int dt) {
  s_phase_ms += dt;
  if (s_lit_ms > 0) {
    s_lit_ms -= dt;
    if (s_lit_ms <= 0) s_lit = -1;
  }

  switch (s_phase) {
    case ECHO_LEAD:
      if (s_phase_ms >= LEAD_MS) {
        s_phase = ECHO_SHOW;
        s_phase_ms = 0;
      }
      break;
    case ECHO_SHOW: {
      int period = s_on_ms + s_gap_ms;
      s_step = s_phase_ms / period;
      if (s_step >= prv_len()) {
        s_phase = ECHO_INPUT;
        s_phase_ms = 0;
        s_step = 0;
        s_lit = -1;
      } else {
        s_lit = s_phase_ms % period < s_on_ms ? s_seq[s_step] : -1;
      }
      break;
    }
    case ECHO_INPUT:
      g_play.trace = s_phase_ms * 100 / s_input_limit;
      if (s_phase_ms >= s_input_limit) {
        s_phase = ECHO_BAD;
        s_phase_ms = 0;
        if (--g_play.lives <= 0) g_play.state = PLAY_FAILED;
        else buzz(BUZZ_BAD);
      }
      break;
    case ECHO_GOOD:
      if (s_phase_ms >= GOOD_MS) {
        s_round++;
        prv_begin_round();
      }
      break;
    case ECHO_BAD:
      if (s_phase_ms >= BAD_MS) prv_begin_round();
      break;
  }
}

static void prv_press(ButtonId button) {
  if (s_phase != ECHO_INPUT) return;
  int node = button == BUTTON_ID_UP ? 0 : button == BUTTON_ID_SELECT ? 1 : 2;
  s_lit = node;
  s_lit_ms = PRESS_LIT_MS;

  int len = prv_len();
  int expected = s_seq[s_reverse ? len - 1 - s_step : s_step];
  if (node != expected) {
    s_phase = ECHO_BAD;
    s_phase_ms = 0;
    if (--g_play.lives <= 0) g_play.state = PLAY_FAILED;
    else buzz(BUZZ_BAD);
    return;
  }

  if (++s_step < len) return;
  g_play.score += (10 * len + 20 * s_level) * (s_reverse ? 2 : 1);
  if (s_round == ROUNDS - 1) {
    g_play.score += 50 * s_level + 25 * g_play.lives;
    g_play.state = PLAY_CRACKED;
    return;
  }
  s_phase = ECHO_GOOD;
  s_phase_ms = 0;
  buzz(BUZZ_TICK);
}

static void prv_node(GContext *ctx, GPoint center, int index) {
  GColor color = NODE_COLORS[index];
  bool lit = s_lit == index;
  bool wrong = s_phase == ECHO_BAD;

  if (lit) {
    ui_hexagon(ctx, center, NODE_R + 5, wrong ? COL_RED : color, false);
    ui_hexagon(ctx, center, NODE_R, wrong ? COL_RED : color, true);
  } else {
    ui_hexagon(ctx, center, NODE_R, COL_BG, true);
    ui_hexagon(ctx, center, NODE_R, wrong ? COL_RED : color, false);
    ui_hexagon(ctx, center, NODE_R - 4, COL_DIM, false);
  }

  // The mark inside says which button the node belongs to.
  GColor mark = lit ? COL_BG : color;
  if (index == 1) {
    graphics_context_set_fill_color(ctx, mark);
    graphics_fill_circle(ctx, center, 5);
  } else {
    ui_triangle(ctx, center, 7, index == 0, mark);
  }
}

static void prv_draw(GContext *ctx, GRect area) {
  int top = area.origin.y;
  for (int i = 0; i < NODES; i++) {
    prv_node(ctx, GPoint(area.origin.x + area.size.w - 44, top + NODE_Y[i]), i);
  }

  char text[24];
  snprintf(text, sizeof(text), "ROUND %d/%d", s_round + 1, ROUNDS);
  ui_label(ctx, text, GRect(4, top + 2, 120, 20), GTextAlignmentLeft, COL_CYAN);

  const char *status = "";
  GColor status_color = COL_YELLOW;
  switch (s_phase) {
    case ECHO_LEAD:
    case ECHO_SHOW: status = "INCOMING"; status_color = COL_DIM; break;
    case ECHO_INPUT: status = "ECHO IT"; break;
    case ECHO_GOOD: status = "MATCHED"; status_color = COL_GREEN; break;
    case ECHO_BAD: status = "DESYNC"; status_color = COL_RED; break;
  }
  ui_text(ctx, status, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(4, top + 22, 122, 32),
          GTextAlignmentLeft, status_color);

  if (s_reverse) {
    GRect flag = GRect(4, top + 66, 92, 20);
    ui_cut_rect(ctx, flag, 6, COL_PINK, true);
    ui_label(ctx, "REVERSE", flag, GTextAlignmentCenter, COL_BG);
  }

  // One slot per step: filled as the pattern plays, then as it's echoed back.
  int len = prv_len();
  int filled = 0;
  if (s_phase == ECHO_SHOW) filled = s_step + (s_lit >= 0 ? 1 : 0);
  else if (s_phase == ECHO_INPUT) filled = s_step;
  else if (s_phase == ECHO_GOOD) filled = len;
  GColor slot_color = s_phase == ECHO_SHOW ? COL_DIM : COL_CYAN;
  for (int i = 0; i < len; i++) {
    GRect slot = GRect(6 + (i % SLOTS_PER_ROW) * (SLOT + SLOT_GAP),
                       top + 96 + (i / SLOTS_PER_ROW) * (SLOT + SLOT_GAP), SLOT, SLOT);
    if (i < filled) ui_fill(ctx, slot, slot_color);
    else ui_frame(ctx, slot, COL_DIM, 1);
  }

  snprintf(text, sizeof(text), "%d STEPS", len);
  ui_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(4, top + 122, 100, 16),
          GTextAlignmentLeft, COL_DIM);
}

const LockDef LOCK_DEF_ECHO = {
  .name = "ECHO",
  .skill = "MEMORY",
  .brief = "The nodes fire a pattern. Play it back on the matching buttons.",
  .controls = "UP/SEL/DN: FIRE NODE\nREVERSE: ECHO IT BACKWARDS",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press
};
