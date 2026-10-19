#include "icebreaker.h"

// CIPHER, the deduction lock, played like Mastermind. Each guess at the hidden key comes
// back with a cyan pip for every symbol in the right slot and a yellow one for every
// symbol that's in the key but somewhere else. No symbol appears in the key twice, since
// with repeats the pips stop narrowing things down and it turns into guessing.
#define MAX_LEN 5
#define MAX_ROWS 8
#define SYMBOLS 7
#define ROW_H 16
#define BOX 26
#define BOX_GAP 4
#define SEND_W 44
#define FLASH_MS 300

typedef struct {
  uint8_t len;
  uint8_t symbols;
  uint8_t rows;
  uint8_t seconds;
} Tier;

static const Tier TIERS[] = {
  { 3, 5, 8, 150 },
  { 4, 5, 8, 150 },
  { 4, 6, 8, 150 },
  { 4, 6, 7, 130 },
  { 5, 6, 8, 160 },
  { 5, 7, 8, 150 }
};

static const char *const GLYPHS[SYMBOLS] = { "0", "3", "7", "A", "C", "E", "F" };
static const GColor8 SYMBOL_COLORS[SYMBOLS] = {
  { .argb = GColorCyanARGB8 }, { .argb = GColorYellowARGB8 },
  { .argb = GColorShockingPinkARGB8 }, { .argb = GColorScreaminGreenARGB8 },
  { .argb = GColorOrangeARGB8 }, { .argb = GColorLavenderIndigoARGB8 },
  { .argb = GColorWhiteARGB8 }
};

typedef struct {
  uint8_t symbols[MAX_LEN];
  uint8_t exact;
  uint8_t near;
} Guess;

static int s_level;
static int s_len;
static int s_symbols;
static int s_max_rows;
static uint8_t s_key[MAX_LEN];
static uint8_t s_input[MAX_LEN];
static Guess s_rows[MAX_ROWS];
static int s_row_count;
static int s_cursor;        // A slot, or s_len for the SEND button
static int s_time_ms;
static int s_limit_ms;
static int s_flash_ms;
static bool s_sent_on_press;  // So holding Select on SEND doesn't send twice

static void prv_start(int level) {
  s_level = level;
  int extra = level - (int)ARRAY_LENGTH(TIERS);
  const Tier *tier = &TIERS[extra < 0 ? level - 1 : (int)ARRAY_LENGTH(TIERS) - 1];
  s_len = tier->len;
  s_symbols = tier->symbols;
  s_max_rows = tier->rows;
  int seconds = tier->seconds;
  if (extra > 0) {
    // Past the last tier, the clock and the number of tries keep shrinking.
    s_max_rows -= extra / 2;
    if (s_max_rows < 6) s_max_rows = 6;
    seconds -= 10 * extra;
    if (seconds < 90) seconds = 90;
  }

  uint8_t pool[SYMBOLS];
  for (int s = 0; s < s_symbols; s++) pool[s] = s;
  for (int i = 0; i < s_len; i++) {
    int pick = rand_range(i, s_symbols - 1);
    s_key[i] = pool[pick];
    pool[pick] = pool[i];
    s_input[i] = 0;
  }
  s_row_count = 0;
  s_cursor = 0;
  s_time_ms = 0;
  s_limit_ms = seconds * 1000;
  s_flash_ms = 0;
  g_play.trace = 0;
}

static void prv_score(Guess *guess) {
  int key_counts[SYMBOLS] = { 0 };
  int guess_counts[SYMBOLS] = { 0 };
  guess->exact = 0;
  for (int i = 0; i < s_len; i++) {
    if (guess->symbols[i] == s_key[i]) guess->exact++;
    key_counts[s_key[i]]++;
    guess_counts[guess->symbols[i]]++;
  }
  int shared = 0;
  for (int s = 0; s < SYMBOLS; s++) {
    shared += key_counts[s] < guess_counts[s] ? key_counts[s] : guess_counts[s];
  }
  guess->near = shared - guess->exact;
}

static void prv_submit(void) {
  Guess *guess = &s_rows[s_row_count++];
  memcpy(guess->symbols, s_input, s_len);
  prv_score(guess);
  s_cursor = 0;
  s_flash_ms = FLASH_MS;

  if (guess->exact == s_len) {
    int seconds_left = (s_limit_ms - s_time_ms) / 1000;
    g_play.score += 80 * s_level + 40 * (s_max_rows - s_row_count) + 2 * seconds_left;
    g_play.state = PLAY_CRACKED;
  } else if (s_row_count >= s_max_rows) {
    g_play.state = PLAY_FAILED;
  } else {
    buzz(BUZZ_TICK);
  }
}

static void prv_tick(int dt) {
  s_time_ms += dt;
  if (s_flash_ms > 0) s_flash_ms -= dt;
  g_play.trace = s_time_ms * 100 / s_limit_ms;
  if (s_time_ms >= s_limit_ms) g_play.state = PLAY_FAILED;
}

static void prv_press(ButtonId button) {
  bool on_send = s_cursor == s_len;
  s_sent_on_press = button == BUTTON_ID_SELECT && on_send;
  if (button == BUTTON_ID_SELECT) {
    if (on_send) prv_submit();
    else s_cursor++;
    return;
  }

  // From SEND, Up steps back to the last slot and Down wraps round to the first.
  int step = button == BUTTON_ID_UP ? 1 : -1;
  if (on_send) {
    s_cursor = button == BUTTON_ID_UP ? s_len - 1 : 0;
  } else {
    s_input[s_cursor] = (s_input[s_cursor] + step + s_symbols) % s_symbols;
  }
}

static void prv_hold(ButtonId button) {
  if (button == BUTTON_ID_SELECT && !s_sent_on_press && g_play.state == PLAY_ON) prv_submit();
}

// Back steps to the previous slot. On the first one it falls through to pausing.
static bool prv_back(void) {
  if (s_cursor == 0) return false;
  s_cursor--;
  return true;
}

// lift is how far above the box's middle the font's line box has to start.
static void prv_symbol(GContext *ctx, GRect box, int symbol, GFont font, int lift) {
  ui_fill(ctx, box, SYMBOL_COLORS[symbol]);
  ui_text(ctx, GLYPHS[symbol], font, GRect(box.origin.x, box.origin.y + box.size.h / 2 - lift,
                                            box.size.w, lift * 2 + 4), GTextAlignmentCenter,
          COL_BG);
}

static void prv_draw(GContext *ctx, GRect area) {
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int top = area.origin.y + 2;
  char text[12];

  for (int r = 0; r < s_max_rows; r++) {
    int y = top + r * ROW_H;
    snprintf(text, sizeof(text), "%d", r + 1);
    ui_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(2, y - 3, 12, 16),
            GTextAlignmentCenter, COL_DIM);

    if (r >= s_row_count) {
      for (int i = 0; i < s_len; i++) {
        ui_fill(ctx, GRect(26 + i * 21, y + 6, 6, 2), COL_DIM);
      }
      continue;
    }

    const Guess *guess = &s_rows[r];
    bool fresh = r == s_row_count - 1 && s_flash_ms > 0;
    for (int i = 0; i < s_len; i++) {
      prv_symbol(ctx, GRect(18 + i * 21, y, 19, ROW_H - 2), guess->symbols[i], small, 11);
    }
    for (int p = 0; p < s_len; p++) {
      GRect pip = GRect(132 + p * 13, y + 2, 10, 10);
      if (p < guess->exact) {
        ui_fill(ctx, pip, fresh ? GColorWhite : COL_CYAN);
      } else if (p < guess->exact + guess->near) {
        ui_frame(ctx, pip, COL_YELLOW, 2);
      } else {
        ui_fill(ctx, GRect(pip.origin.x + 4, pip.origin.y + 4, 2, 2), COL_DIM);
      }
    }
  }

  // The guess being built, or the key once the lock is over.
  bool reveal = g_play.state != PLAY_ON;
  int row_w = s_len * (BOX + BOX_GAP) + SEND_W;
  int x = (SCREEN_W - row_w) / 2;
  int y = top + MAX_ROWS * ROW_H + 10;
  GFont big = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  for (int i = 0; i < s_len; i++) {
    GRect box = GRect(x + i * (BOX + BOX_GAP), y, BOX, BOX);
    prv_symbol(ctx, box, reveal ? s_key[i] : s_input[i], big, 17);
    if (!reveal && i == s_cursor) {
      ui_frame(ctx, grect_inset(box, GEdgeInsets(-3)), COL_YELLOW, 2);
      ui_triangle(ctx, GPoint(box.origin.x + BOX / 2, y - 8), 4, true, COL_YELLOW);
      ui_triangle(ctx, GPoint(box.origin.x + BOX / 2, y + BOX + 7), 4, false, COL_YELLOW);
    }
  }

  GRect send = GRect(x + s_len * (BOX + BOX_GAP), y, SEND_W, BOX);
  if (reveal) {
    ui_label(ctx, "KEY", send, GTextAlignmentCenter, COL_DIM);
  } else if (s_cursor == s_len) {
    ui_cut_rect(ctx, send, 6, COL_YELLOW, true);
    ui_label(ctx, "SEND", send, GTextAlignmentCenter, COL_BG);
  } else {
    ui_cut_rect(ctx, send, 6, COL_CYAN, false);
    ui_label(ctx, "SEND", send, GTextAlignmentCenter, COL_CYAN);
  }
}

const LockDef LOCK_DEF_CIPHER = {
  .name = "CIPHER",
  .skill = "DEDUCE",
  .brief = "Find the key. No symbol repeats. Cyan pip: one in the right place. Yellow: one "
           "in the wrong place.",
  .controls = "UP/DN: SYMBOL   SEL: NEXT\nBACK: PREV  HOLD SEL: SEND",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press,
  .hold = prv_hold,
  .back = prv_back
};
