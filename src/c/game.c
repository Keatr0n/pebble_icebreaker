#include "icebreaker.h"

#define HOLD_MS 450
#define MAX_DT_MS 100
#define OUTRO_MS 1800
#define OUTRO_SKIP_MS 500   // Select skips the rest of the outro after this long
#define GLITCH_MS 450

typedef enum {
  PHASE_PLAY,
  PHASE_PAUSE,
  PHASE_OUTRO
} Phase;

Play g_play;

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_frame_timer;
static AppTimer *s_hold_timer;
static const LockDef *s_lock;
static int s_level;
static Phase s_phase;
static uint32_t s_last_ms;
static int s_outro_ms;
static bool s_pause_quit;   // Which pause option is highlighted
static ButtonId s_held;

static uint32_t prv_now_ms(void) {
  time_t seconds;
  uint16_t millis;
  time_ms(&seconds, &millis);
  return (uint32_t)seconds * 1000 + millis;
}

static void prv_frame(void *data);

static void prv_schedule_frame(void) {
  if (!s_frame_timer) s_frame_timer = app_timer_register(FRAME_MS, prv_frame, NULL);
}

static void prv_stop_timers(void) {
  if (s_frame_timer) app_timer_cancel(s_frame_timer);
  if (s_hold_timer) app_timer_cancel(s_hold_timer);
  s_frame_timer = NULL;
  s_hold_timer = NULL;
}

static void prv_check_outcome(void) {
  if (s_phase != PHASE_PLAY || g_play.state == PLAY_ON) return;
  s_phase = PHASE_OUTRO;
  s_outro_ms = 0;
  buzz(g_play.state == PLAY_CRACKED ? BUZZ_WIN : BUZZ_LOSE);
}

static void prv_finish(void) {
  prv_stop_timers();
  session_lock_finished(g_play.state == PLAY_CRACKED, g_play.score);
  window_stack_remove(s_window, false);
}

static void prv_frame(void *data) {
  s_frame_timer = NULL;
  uint32_t now = prv_now_ms();
  int dt = (int)(now - s_last_ms);
  s_last_ms = now;
  if (dt < 0) dt = 0;
  if (dt > MAX_DT_MS) dt = MAX_DT_MS;

  if (s_phase == PHASE_PLAY) {
    s_lock->tick(dt);
    prv_check_outcome();
  } else if (s_phase == PHASE_OUTRO) {
    s_outro_ms += dt;
    if (s_outro_ms >= OUTRO_MS) {
      prv_finish();
      return;
    }
  }
  layer_mark_dirty(s_canvas);
  if (s_phase != PHASE_PAUSE) prv_schedule_frame();
}

static void prv_draw_hud(GContext *ctx) {
  char text[24];
  ui_cut_rect(ctx, GRect(0, 0, SCREEN_W, HEADER_H), 8, COL_YELLOW, true);
  snprintf(text, sizeof(text), "%s L%d", s_lock->name, s_level);
  ui_label(ctx, text, GRect(6, 0, 120, HEADER_H), GTextAlignmentLeft, COL_BG);
  snprintf(text, sizeof(text), "%d", g_session.score + g_play.score);
  ui_label(ctx, text, GRect(110, 0, 80, HEADER_H), GTextAlignmentRight, COL_BG);

  if (g_play.trace >= 0) {
    ui_segments(ctx, GRect(4, 25, 140, 6), g_play.trace, 3, 1,
                g_play.trace >= 75 ? COL_RED : COL_PINK, COL_TRACK);
  }
  if (g_play.lives_max > 0) {
    ui_pips(ctx, GRect(148, 25, 48, 6), g_play.lives, g_play.lives_max, COL_CYAN);
  }
}

static void prv_draw_pause(GContext *ctx) {
  GRect box = GRect(20, 70, SCREEN_W - 40, 92);
  ui_fill(ctx, box, COL_BG);
  ui_cut_rect(ctx, box, 10, COL_CYAN, false);
  ui_label(ctx, "LINK PAUSED", GRect(box.origin.x, box.origin.y + 6, box.size.w, 20),
           GTextAlignmentCenter, COL_CYAN);

  const char *options[] = { "RESUME", "JACK OUT" };
  for (int i = 0; i < 2; i++) {
    GRect row = GRect(box.origin.x + 12, box.origin.y + 34 + i * 26, box.size.w - 24, 22);
    bool selected = (i == 1) == s_pause_quit;
    GColor color = i == 1 ? COL_RED : COL_YELLOW;
    if (selected) ui_cut_rect(ctx, row, 6, color, true);
    ui_label(ctx, options[i], row, GTextAlignmentCenter, selected ? COL_BG : color);
  }
}

static void prv_draw_outro(GContext *ctx) {
  bool cracked = g_play.state == PLAY_CRACKED;
  GColor color = cracked ? COL_CYAN : COL_RED;
  GRect band = GRect(0, 84, SCREEN_W, 70);

  // The band opens out from a line, then holds.
  int grow = s_outro_ms * band.size.h / 180;
  if (grow < band.size.h) {
    band.origin.y += (band.size.h - grow) / 2;
    band.size.h = grow;
  }
  ui_fill(ctx, band, COL_BG);
  ui_fill(ctx, GRect(0, band.origin.y, SCREEN_W, 2), color);
  ui_fill(ctx, GRect(0, band.origin.y + band.size.h - 2, SCREEN_W, 2), color);
  if (band.size.h < 70) return;

  int glitch = s_outro_ms < GLITCH_MS ? rand_range(2, 6) : 2;
  int jitter = s_outro_ms < GLITCH_MS ? rand_range(-3, 3) : 0;
  ui_label(ctx, "ACCESS", GRect(0, band.origin.y + 4, SCREEN_W, 18), GTextAlignmentCenter, color);
  ui_title(ctx, cracked ? "GRANTED" : "DENIED", GRect(jitter, band.origin.y + 20, SCREEN_W, 32),
           GTextAlignmentCenter, cracked ? COL_YELLOW : GColorWhite, glitch);
  char text[16];
  snprintf(text, sizeof(text), "+%d", g_play.score);
  ui_label(ctx, text, GRect(0, band.origin.y + 50, SCREEN_W, 18), GTextAlignmentCenter, color);
}

static void prv_draw(Layer *layer, GContext *ctx) {
  ui_fill(ctx, layer_get_bounds(layer), COL_BG);
  // The board is hidden while paused, so a pause can't buy thinking time on the clock.
  if (s_phase != PHASE_PAUSE) s_lock->draw(ctx, GRect(0, PLAY_TOP, SCREEN_W, SCREEN_H - PLAY_TOP));
  prv_draw_hud(ctx);
  if (s_phase == PHASE_PAUSE) prv_draw_pause(ctx);
  if (s_phase == PHASE_OUTRO) prv_draw_outro(ctx);
}

static void prv_hold_fired(void *data) {
  s_hold_timer = NULL;
  if (s_phase != PHASE_PLAY || !s_lock->hold) return;
  s_lock->hold(s_held);
  prv_check_outcome();
  layer_mark_dirty(s_canvas);
}

// Play acts on the way down, not on release, so timing games feel immediate. The pause
// menu and the outro wait for the release.
static void prv_button_down(ClickRecognizerRef recognizer, void *context) {
  if (s_phase != PHASE_PLAY) return;
  ButtonId button = click_recognizer_get_button_id(recognizer);
  s_lock->press(button);
  prv_check_outcome();
  layer_mark_dirty(s_canvas);

  if (s_lock->hold && s_phase == PHASE_PLAY) {
    if (s_hold_timer) app_timer_cancel(s_hold_timer);
    s_held = button;
    s_hold_timer = app_timer_register(HOLD_MS, prv_hold_fired, NULL);
  }
}

static void prv_button_up(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  if (s_hold_timer && button == s_held) {
    app_timer_cancel(s_hold_timer);
    s_hold_timer = NULL;
  }

  if (s_phase == PHASE_OUTRO) {
    if (button == BUTTON_ID_SELECT && s_outro_ms >= OUTRO_SKIP_MS) prv_finish();
    return;
  }
  if (s_phase != PHASE_PAUSE) return;

  if (button != BUTTON_ID_SELECT) {
    s_pause_quit = !s_pause_quit;
  } else if (s_pause_quit) {
    prv_stop_timers();
    session_abort();
    window_stack_remove(s_window, false);
    return;
  } else {
    s_phase = PHASE_PLAY;
    s_last_ms = prv_now_ms();
    prv_schedule_frame();
  }
  layer_mark_dirty(s_canvas);
}

static void prv_back(ClickRecognizerRef recognizer, void *context) {
  if (s_phase == PHASE_PLAY && s_lock->back && s_lock->back()) {
    layer_mark_dirty(s_canvas);
    return;
  }
  if (s_phase == PHASE_PLAY) {
    s_phase = PHASE_PAUSE;
    s_pause_quit = false;
    prv_stop_timers();
  } else if (s_phase == PHASE_PAUSE) {
    s_phase = PHASE_PLAY;
    s_last_ms = prv_now_ms();
    prv_schedule_frame();
  }
  layer_mark_dirty(s_canvas);
}

static void prv_click_config(void *context) {
  window_raw_click_subscribe(BUTTON_ID_UP, prv_button_down, prv_button_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, prv_button_down, prv_button_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_button_down, prv_button_up, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back);
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, prv_draw);
  layer_add_child(root, s_canvas);
}

static void prv_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void prv_appear(Window *window) {
  s_last_ms = prv_now_ms();
  if (s_phase != PHASE_PAUSE) prv_schedule_frame();
}

static void prv_disappear(Window *window) {
  prv_stop_timers();
  // A notification covering a live lock shouldn't let its clock run on.
  if (s_phase == PHASE_PLAY) {
    s_phase = PHASE_PAUSE;
    s_pause_quit = false;
  }
}

void game_show(LockId lock, int level) {
  s_lock = LOCKS[lock];
  s_level = level;
  g_play = (Play) { .state = PLAY_ON, .score = 0, .trace = -1, .lives = -1, .lives_max = 0 };
  s_lock->start(level);
  s_phase = PHASE_PLAY;
  window_stack_push(s_window, false);
}

void game_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, COL_BG);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
    .appear = prv_appear,
    .disappear = prv_disappear
  });
}

void game_deinit(void) {
  window_destroy(s_window);
}
