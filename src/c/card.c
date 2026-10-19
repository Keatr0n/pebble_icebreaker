#include "icebreaker.h"

// The screen between locks: a briefing for the next one, or the tally once a run is over.
static Window *s_window;
static Layer *s_canvas;

static void prv_header(GContext *ctx, const char *text, GColor color) {
  ui_cut_rect(ctx, GRect(0, 0, SCREEN_W, HEADER_H), 8, color, true);
  ui_label(ctx, text, GRect(6, 0, SCREEN_W - 12, HEADER_H), GTextAlignmentLeft, COL_BG);
}

static void prv_button(GContext *ctx, const char *text, GColor color) {
  GRect box = GRect(SCREEN_W - 100, SCREEN_H - 24, 96, 20);
  ui_cut_rect(ctx, box, 6, color, true);
  ui_label(ctx, text, box, GTextAlignmentCenter, COL_BG);
}

static void prv_draw_briefing(GContext *ctx) {
  const LockDef *lock = LOCKS[g_session.lock];
  char line[48];
  bool heist = g_session.mode == MODE_HEIST;

  if (heist) {
    snprintf(line, sizeof(line), "HEIST // FLOOR %d", g_session.floor);
    prv_header(ctx, line, COL_PINK);
    ui_pips(ctx, GRect(SCREEN_W - 52, 7, 40, 8), g_session.integrity, 3, COL_BG);
  } else {
    prv_header(ctx, "TRAINING", COL_YELLOW);
    uint32_t best = g_records.lock_best[g_session.lock];
    if (best > 0) {
      snprintf(line, sizeof(line), "HI %d  LV %d", (int)best, g_records.lock_level[g_session.lock]);
      ui_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
              GRect(90, 1, SCREEN_W - 104, 16), GTextAlignmentRight, COL_BG);
    }
  }

  if (g_session.has_last) {
    const char *name = LOCKS[g_session.last_lock]->name;
    if (g_session.last_cracked) {
      snprintf(line, sizeof(line), "%s CRACKED  +%d", name, g_session.last_score);
    } else if (heist) {
      snprintf(line, sizeof(line), "%s HELD  -1 INTEGRITY", name);
    } else {
      snprintf(line, sizeof(line), "%s HELD", name);
    }
    ui_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(6, 24, 188, 16),
            GTextAlignmentLeft, g_session.last_cracked ? COL_GREEN : COL_RED);
  }

  ui_title(ctx, lock->name, GRect(6, 44, 188, 34), GTextAlignmentLeft, COL_YELLOW, 2);

  GRect tag = GRect(6, 82, 86, 18);
  ui_cut_rect(ctx, tag, 5, COL_CYAN, true);
  ui_label(ctx, lock->skill, tag, GTextAlignmentCenter, COL_BG);
  snprintf(line, sizeof(line), "LV %d", g_session.level);
  ui_label(ctx, line, GRect(100, 82, 94, 18), GTextAlignmentRight, COL_CYAN);

  ui_wrap(ctx, lock->brief, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(6, 102, 188, 64),
          GTextAlignmentLeft, GColorWhite);
  ui_wrap(ctx, lock->controls, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
          GRect(6, 168, 188, 34), GTextAlignmentLeft, COL_PALE);

  snprintf(line, sizeof(line), "%d", g_session.score);
  ui_text(ctx, "SCORE", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(6, SCREEN_H - 22, 40, 16),
          GTextAlignmentLeft, COL_DIM);
  ui_label(ctx, line, GRect(44, SCREEN_H - 24, 56, 20), GTextAlignmentLeft, GColorWhite);
  prv_button(ctx, "JACK IN", COL_YELLOW);
}

static void prv_draw_over(GContext *ctx) {
  char line[48];
  bool heist = g_session.mode == MODE_HEIST;
  prv_header(ctx, heist ? "HEIST // RUN OVER" : "TRAINING // RUN OVER", COL_DIM);

  const char *title = "JACKED OUT";
  GColor title_color = COL_YELLOW;
  if (g_session.end == END_FLATLINED) {
    title = "FLATLINED";
    title_color = COL_RED;
  } else if (g_session.end == END_ICE_HOLDS) {
    title = "ICE HOLDS";
    title_color = COL_RED;
  }
  ui_title(ctx, title, GRect(0, 30, SCREEN_W, 34), GTextAlignmentCenter, title_color, 3);

  ui_text(ctx, "FINAL SCORE", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 70, SCREEN_W, 16),
          GTextAlignmentCenter, COL_DIM);
  snprintf(line, sizeof(line), "%d", g_session.score);
  ui_text(ctx, line, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS), GRect(0, 84, SCREEN_W, 46),
          GTextAlignmentCenter, GColorWhite);

  uint32_t best = heist ? g_records.heist_best : g_records.lock_best[g_session.lock];
  GRect banner = GRect(30, 134, SCREEN_W - 60, 20);
  if (g_session.new_record) {
    ui_cut_rect(ctx, banner, 6, COL_YELLOW, true);
    ui_label(ctx, "NEW RECORD", banner, GTextAlignmentCenter, COL_BG);
  } else {
    snprintf(line, sizeof(line), "BEST %d", (int)best);
    ui_cut_rect(ctx, banner, 6, COL_YELLOW, false);
    ui_label(ctx, line, banner, GTextAlignmentCenter, COL_YELLOW);
  }

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (heist) {
    snprintf(line, sizeof(line), "FLOOR %d  //  %d CRACKED", g_session.floor, g_session.cracked);
    ui_text(ctx, line, small, GRect(0, 158, SCREEN_W, 22), GTextAlignmentCenter, COL_CYAN);
    snprintf(line, sizeof(line), "RANK: %s", records_rank(g_records.heist_best));
    ui_text(ctx, line, small, GRect(0, 178, SCREEN_W, 22), GTextAlignmentCenter, COL_PINK);
  } else {
    snprintf(line, sizeof(line), "%s  //  REACHED LV %d", LOCKS[g_session.lock]->name,
             g_session.level);
    ui_text(ctx, line, small, GRect(0, 158, SCREEN_W, 22), GTextAlignmentCenter, COL_CYAN);
  }

  prv_button(ctx, "DONE", COL_CYAN);
}

static void prv_draw(Layer *layer, GContext *ctx) {
  ui_fill(ctx, layer_get_bounds(layer), COL_BG);
  if (g_session.end == END_NONE) {
    prv_draw_briefing(ctx);
  } else {
    prv_draw_over(ctx);
  }
}

static void prv_select(ClickRecognizerRef recognizer, void *context) {
  if (g_session.end != END_NONE) {
    window_stack_remove(s_window, true);
    return;
  }
  game_show(g_session.lock, g_session.level);
  window_stack_remove(s_window, false);
}

static void prv_back(ClickRecognizerRef recognizer, void *context) {
  // Backing out before anything's been played leaves no score to bank.
  if (g_session.end != END_NONE || !g_session.has_last) {
    window_stack_remove(s_window, true);
    return;
  }
  session_abort();
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
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

void card_show(void) {
  if (!window_stack_contains_window(s_window)) {
    // Sliding in from the menu, cutting straight in from a game.
    bool from_menu = !g_session.has_last && g_session.end == END_NONE;
    window_stack_push(s_window, from_menu);
  } else if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

void card_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, COL_BG);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload
  });
}

void card_deinit(void) {
  window_destroy(s_window);
}
