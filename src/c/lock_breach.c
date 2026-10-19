#include "icebreaker.h"

// BREACH, the logic lock, after Cyberpunk 2077's breach protocol. Picks alternate between
// the lit row and the lit column, starting on the top row, and each pick lands in the
// buffer. A daemon uploads once its codes appear in the buffer in a row; UNLOCK has to
// make it for the lock to open, and the others are worth points.
#define MAX_N 6
#define MAX_BUF 8
#define MAX_DAEMONS 3
#define MAX_DLEN 4
#define USED 0xFF
#define GEN_TRIES 60
#define FLASH_MS 250

typedef struct {
  uint8_t n;
  uint8_t codes;
  uint8_t buf;
  uint8_t lengths[MAX_DAEMONS];  // 0 for no daemon
  uint8_t seconds;
} Tier;

static const Tier TIERS[] = {
  { 5, 5, 6, { 3, 2, 0 }, 60 },
  { 5, 5, 6, { 3, 3, 0 }, 60 },
  { 5, 5, 7, { 3, 3, 4 }, 60 },
  { 6, 5, 7, { 3, 4, 4 }, 55 },
  { 6, 6, 8, { 4, 4, 4 }, 55 },
  { 6, 6, 8, { 4, 4, 4 }, 45 }
};

static const char *const CODES[] = { "1C", "55", "BD", "E9", "7A", "FF" };
static const char *const DAEMON_NAMES[] = { "UNLOCK", "DATAMINE", "ICEPICK" };

typedef struct {
  uint8_t codes[MAX_DLEN];
  uint8_t len;
  bool done;
} Daemon;

static int s_level;
static int s_n;
static uint8_t s_grid[MAX_N][MAX_N];
static uint8_t s_buf[MAX_BUF];
static int s_buf_len;
static int s_buf_cap;
static Daemon s_daemons[MAX_DAEMONS];
static int s_daemon_count;
static bool s_on_row;       // Picking along row s_line, or else along column s_line
static int s_line;
static int s_cursor;        // Position along the line
static int s_time_ms;
static int s_limit_ms;
static int s_flash_ms;
static int s_cell_w;
static int s_cell_h;

static uint8_t *prv_cell(int along) {
  return s_on_row ? &s_grid[s_line][along] : &s_grid[along][s_line];
}

// Walks a route the player could take, so the daemons can be cut from codes on it and
// every one of them is reachable in the same run.
static bool prv_make_route(uint8_t route_codes[], int len) {
  bool taken[MAX_N][MAX_N];
  memset(taken, 0, sizeof(taken));
  int row = 0;
  int col = 0;

  for (int i = 0; i < len; i++) {
    int options[MAX_N];
    int count = 0;
    for (int k = 0; k < s_n; k++) {
      // Even picks run along a row, odd picks down a column.
      int r = i % 2 == 0 ? row : k;
      int c = i % 2 == 0 ? k : col;
      if (!taken[r][c]) options[count++] = k;
    }
    if (count == 0) return false;

    int pick = options[rand() % count];
    if (i % 2 == 0) {
      col = pick;
    } else {
      row = pick;
    }
    taken[row][col] = true;
    route_codes[i] = s_grid[row][col];
  }
  return true;
}

static bool prv_same(const Daemon *a, const Daemon *b) {
  return a->len == b->len && memcmp(a->codes, b->codes, a->len) == 0;
}

static void prv_generate(const Tier *tier) {
  uint8_t route[MAX_BUF];
  for (int attempt = 0; attempt < GEN_TRIES; attempt++) {
    for (int r = 0; r < s_n; r++) {
      for (int c = 0; c < s_n; c++) s_grid[r][c] = rand() % tier->codes;
    }
    if (!prv_make_route(route, s_buf_cap)) continue;

    bool distinct = true;
    for (int d = 0; d < s_daemon_count; d++) {
      Daemon *daemon = &s_daemons[d];
      daemon->len = tier->lengths[d];
      daemon->done = false;
      int offset = rand_range(0, s_buf_cap - daemon->len);
      memcpy(daemon->codes, &route[offset], daemon->len);
      for (int e = 0; e < d; e++) {
        if (prv_same(daemon, &s_daemons[e])) distinct = false;
      }
    }
    if (distinct) return;
  }
}

static void prv_start(int level) {
  s_level = level;
  int tier_index = level - 1 < (int)ARRAY_LENGTH(TIERS) ? level - 1 : (int)ARRAY_LENGTH(TIERS) - 1;
  const Tier *tier = &TIERS[tier_index];

  s_n = tier->n;
  s_cell_w = s_n == 5 ? 30 : 26;
  s_cell_h = s_n == 5 ? 22 : 20;
  s_buf_cap = tier->buf;
  s_buf_len = 0;
  s_daemon_count = 0;
  while (s_daemon_count < MAX_DAEMONS && tier->lengths[s_daemon_count]) s_daemon_count++;
  prv_generate(tier);

  int seconds = tier->seconds - 3 * (level - (int)ARRAY_LENGTH(TIERS));
  if (seconds > tier->seconds) seconds = tier->seconds;
  if (seconds < 30) seconds = 30;
  s_limit_ms = seconds * 1000;
  s_time_ms = 0;
  s_flash_ms = 0;
  s_on_row = true;
  s_line = 0;
  s_cursor = 0;
  g_play.trace = 0;
}

// How many of the daemon's codes the end of the buffer already lines up with.
static int prv_progress(const Daemon *daemon) {
  if (daemon->done) return daemon->len;
  for (int k = daemon->len - 1; k > 0; k--) {
    if (k <= s_buf_len && memcmp(&s_buf[s_buf_len - k], daemon->codes, k) == 0) return k;
  }
  return 0;
}

static bool prv_dead(const Daemon *daemon) {
  return !daemon->done && s_buf_cap - s_buf_len < daemon->len - prv_progress(daemon);
}

static bool prv_line_open(void) {
  for (int k = 0; k < s_n; k++) {
    if (*prv_cell(k) != USED) return true;
  }
  return false;
}

static void prv_finish(void) {
  if (s_daemons[0].done) {
    int seconds_left = (s_limit_ms - s_time_ms) / 1000;
    g_play.score += seconds_left * s_level + (s_buf_cap - s_buf_len) * 15;
    g_play.state = PLAY_CRACKED;
  } else {
    g_play.state = PLAY_FAILED;
  }
}

static void prv_pick(void) {
  uint8_t *cell = prv_cell(s_cursor);
  if (*cell == USED) {
    s_flash_ms = FLASH_MS;
    return;
  }
  s_buf[s_buf_len++] = *cell;
  *cell = USED;

  for (int d = 0; d < s_daemon_count; d++) {
    Daemon *daemon = &s_daemons[d];
    if (daemon->done || daemon->len > s_buf_len) continue;
    if (memcmp(&s_buf[s_buf_len - daemon->len], daemon->codes, daemon->len) == 0) {
      daemon->done = true;
      g_play.score += d == 0 ? 60 * s_level : 40 * s_level + 10 * daemon->len;
    }
  }

  // The pick's position on this line becomes the line, and the old line the position.
  int along = s_cursor;
  s_cursor = s_line;
  s_line = along;
  s_on_row = !s_on_row;

  bool all_settled = true;
  for (int d = 0; d < s_daemon_count; d++) {
    if (!s_daemons[d].done && !prv_dead(&s_daemons[d])) all_settled = false;
  }
  if (all_settled || s_buf_len >= s_buf_cap || prv_dead(&s_daemons[0]) || !prv_line_open()) {
    prv_finish();
  } else {
    buzz(BUZZ_TICK);
  }
}

static void prv_tick(int dt) {
  s_time_ms += dt;
  if (s_flash_ms > 0) s_flash_ms -= dt;
  g_play.trace = s_time_ms * 100 / s_limit_ms;
  if (s_time_ms >= s_limit_ms) prv_finish();
}

static void prv_press(ButtonId button) {
  if (button == BUTTON_ID_SELECT) {
    prv_pick();
    return;
  }
  // Skips over cells that have already been picked.
  int step = button == BUTTON_ID_UP ? -1 : 1;
  for (int i = 0; i < s_n; i++) {
    s_cursor = (s_cursor + step + s_n) % s_n;
    if (*prv_cell(s_cursor) != USED) break;
  }
}

// Holding Down cashes out early, once UNLOCK is in and the rest aren't worth chasing.
// Not Select, whose press would spend a pick on the way.
static void prv_hold(ButtonId button) {
  if (button == BUTTON_ID_DOWN && s_daemons[0].done) prv_finish();
}

static void prv_draw(GContext *ctx, GRect area) {
  GFont code_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int top = area.origin.y;

  // Buffer
  ui_text(ctx, "BUF", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(4, top, 26, 16),
          GTextAlignmentLeft, COL_DIM);
  for (int i = 0; i < s_buf_cap; i++) {
    GRect slot = GRect(30 + i * 21, top + 1, 19, 16);
    if (i < s_buf_len) {
      ui_frame(ctx, slot, COL_CYAN, 1);
      ui_text(ctx, CODES[s_buf[i]], small_font, GRect(slot.origin.x, slot.origin.y - 2, 19, 16),
              GTextAlignmentCenter, COL_CYAN);
    } else {
      ui_frame(ctx, slot, COL_DIM, 1);
    }
  }

  // Grid
  int grid_x = (SCREEN_W - s_n * s_cell_w) / 2;
  int grid_y = top + 22;
  GRect band = s_on_row
      ? GRect(grid_x - 2, grid_y + s_line * s_cell_h, s_n * s_cell_w + 4, s_cell_h)
      : GRect(grid_x + s_line * s_cell_w, grid_y - 2, s_cell_w, s_n * s_cell_h + 4);
  ui_fill(ctx, band, COL_DARK);
  ui_frame(ctx, band, s_flash_ms > 0 ? COL_RED : COL_CYAN, 1);

  for (int r = 0; r < s_n; r++) {
    for (int c = 0; c < s_n; c++) {
      GRect cell = GRect(grid_x + c * s_cell_w, grid_y + r * s_cell_h, s_cell_w, s_cell_h);
      bool on_line = s_on_row ? r == s_line : c == s_line;
      bool cursor = on_line && (s_on_row ? c : r) == s_cursor;
      if (cursor) ui_fill(ctx, grect_inset(cell, GEdgeInsets(1)), COL_YELLOW);

      if (s_grid[r][c] == USED) {
        ui_frame(ctx, GRect(cell.origin.x + 8, cell.origin.y + 6, 8, 8),
                 cursor ? COL_BG : COL_DIM, 1);
        continue;
      }
      GColor ink = cursor ? COL_BG : on_line ? GColorWhite : COL_PALE;
      ui_text(ctx, CODES[s_grid[r][c]], code_font,
              GRect(cell.origin.x, cell.origin.y - 3, s_cell_w, s_cell_h + 2), GTextAlignmentCenter,
              ink);
    }
  }

  // Daemons
  int daemon_y = grid_y + s_n * s_cell_h + 6;
  for (int d = 0; d < s_daemon_count; d++) {
    const Daemon *daemon = &s_daemons[d];
    int y = daemon_y + d * 15;
    bool dead = prv_dead(daemon);
    int progress = prv_progress(daemon);
    for (int k = 0; k < daemon->len; k++) {
      GColor ink = daemon->done ? COL_GREEN : dead ? COL_DIM : k < progress ? COL_CYAN
                                                                             : GColorWhite;
      ui_text(ctx, CODES[daemon->codes[k]], small_font, GRect(4 + k * 21, y - 3, 21, 16),
              GTextAlignmentCenter, ink);
    }
    GColor name_ink = daemon->done ? COL_GREEN : dead ? COL_RED : d == 0 ? COL_YELLOW
                                                                         : COL_DIM;
    const char *status = daemon->done ? " OK" : dead ? " X" : "";
    char label[16];
    snprintf(label, sizeof(label), "%s%s", DAEMON_NAMES[d], status);
    ui_text(ctx, label, small_font, GRect(92, y - 3, 104, 16), GTextAlignmentRight, name_ink);
  }
}

const LockDef LOCK_DEF_BREACH = {
  .name = "BREACH",
  .skill = "LOGIC",
  .brief = "Picks alternate row, column, row. Land UNLOCK's codes in the buffer, in "
           "order.",
  .controls = "UP/DN: MOVE   SEL: PICK\nHOLD DN: CASH OUT EARLY",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press,
  .hold = prv_hold
};
