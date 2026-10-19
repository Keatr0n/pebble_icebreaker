#include "icebreaker.h"

#define ROW_H 21
#define ROWS_TOP 50
#define ROW_HEIST 0
#define ROW_FIRST_LOCK 1
#define ROW_HAPTICS (ROW_FIRST_LOCK + LOCK_COUNT)
#define ROW_COUNT (ROW_HAPTICS + 1)

static Window *s_window;
static Layer *s_canvas;
static int s_selected;

static void prv_row(GContext *ctx, int row, const char *name, const char *tag,
                    const char *value, GColor accent) {
  GRect box = GRect(4, ROWS_TOP + row * ROW_H, SCREEN_W - 8, ROW_H - 2);
  bool selected = row == s_selected;
  if (selected) ui_cut_rect(ctx, box, 6, accent, true);

  GColor ink = selected ? COL_BG : accent;
  ui_label(ctx, name, GRect(box.origin.x + 5, box.origin.y, 96, box.size.h), GTextAlignmentLeft,
           ink);
  ui_text(ctx, tag, fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(box.origin.x + 92, box.origin.y + 1, 56, 16), GTextAlignmentLeft,
          selected ? COL_BG : COL_DIM);
  ui_label(ctx, value, GRect(box.origin.x + 120, box.origin.y, box.size.w - 129, box.size.h),
           GTextAlignmentRight, selected ? COL_BG : GColorWhite);
}

static void prv_draw(Layer *layer, GContext *ctx) {
  ui_fill(ctx, layer_get_bounds(layer), COL_BG);

  ui_title(ctx, "ICEBREAKER", GRect(0, 2, SCREEN_W, 30), GTextAlignmentCenter, COL_YELLOW, 2);
  char status[40];
  snprintf(status, sizeof(status), "%s // %d ICE BROKEN", records_rank(g_records.heist_best),
           (int)g_records.cracked);
  ui_text(ctx, status, fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(0, 30, SCREEN_W, 16), GTextAlignmentCenter, COL_CYAN);

  char value[12];
  char tag[12] = "ALL LOCKS";
  if (g_records.heist_floor > 0) snprintf(tag, sizeof(tag), "FLOOR %d", g_records.heist_floor);
  snprintf(value, sizeof(value), "%d", (int)g_records.heist_best);
  prv_row(ctx, ROW_HEIST, "HEIST", tag, value, COL_PINK);

  for (int i = 0; i < LOCK_COUNT; i++) {
    snprintf(value, sizeof(value), "%d", (int)g_records.lock_best[i]);
    prv_row(ctx, ROW_FIRST_LOCK + i, LOCKS[i]->name, LOCKS[i]->skill, value, COL_YELLOW);
  }

  prv_row(ctx, ROW_HAPTICS, "HAPTICS", "", g_records.haptics ? "ON" : "OFF", COL_CYAN);
}

static void prv_move(ClickRecognizerRef recognizer, void *context) {
  int step = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP ? -1 : 1;
  s_selected = (s_selected + step + ROW_COUNT) % ROW_COUNT;
  layer_mark_dirty(s_canvas);
}

static void prv_select(ClickRecognizerRef recognizer, void *context) {
  if (s_selected == ROW_HAPTICS) {
    g_records.haptics = !g_records.haptics;
    records_save();
    buzz(BUZZ_TICK);
    layer_mark_dirty(s_canvas);
  } else if (s_selected == ROW_HEIST) {
    session_start(MODE_HEIST, LOCK_ROTOR);
  } else {
    session_start(MODE_TRAINING, s_selected - ROW_FIRST_LOCK);
  }
}

static void prv_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, prv_move);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, prv_move);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
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

// Scores may have changed while a run was on top.
static void prv_appear(Window *window) {
  layer_mark_dirty(s_canvas);
}

void menu_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, COL_BG);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
    .appear = prv_appear
  });
  window_stack_push(s_window, true);
}

void menu_deinit(void) {
  window_destroy(s_window);
}
