#include "icebreaker.h"

// Chakra Petch sits low in its line box. These put the caps where a box's centre is.
#define LABEL_CAP 14
#define LABEL_LIFT 4
#define TITLE_CAP 22
#define TITLE_LIFT 9

GFont g_font_label;
GFont g_font_title;

void ui_init(void) {
  g_font_label = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHAKRA_16));
  g_font_title = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHAKRA_30));
}

void ui_deinit(void) {
  fonts_unload_custom_font(g_font_label);
  fonts_unload_custom_font(g_font_title);
}

void ui_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
             GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

void ui_wrap(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
             GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeWordWrap, align, NULL);
}

void ui_label(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color) {
  box.origin.y += (box.size.h - LABEL_CAP) / 2 - LABEL_LIFT;
  box.size.h = LABEL_CAP + 8;
  ui_text(ctx, text, g_font_label, box, align, color);
}

// glitch pulls a cyan and a red copy that many pixels out to either side.
void ui_title(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color,
              int glitch) {
  box.origin.y += (box.size.h - TITLE_CAP) / 2 - TITLE_LIFT;
  box.size.h = TITLE_CAP + 16;
  if (glitch) {
    GRect ghost = box;
    ghost.origin.x = box.origin.x - glitch;
    ui_text(ctx, text, g_font_title, ghost, align, COL_CYAN);
    ghost.origin.x = box.origin.x + glitch;
    ui_text(ctx, text, g_font_title, ghost, align, COL_RED);
  }
  ui_text(ctx, text, g_font_title, box, align, color);
}

void ui_fill(GContext *ctx, GRect rect, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

void ui_frame(GContext *ctx, GRect rect, GColor color, int width) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < width; i++) {
    graphics_draw_rect(ctx, GRect(rect.origin.x + i, rect.origin.y + i, rect.size.w - 2 * i,
                                  rect.size.h - 2 * i));
  }
}

static void prv_poly(GContext *ctx, GPoint *points, int count, GColor color, bool filled) {
  GPathInfo info = { .num_points = count, .points = points };
  GPath *path = gpath_create(&info);
  if (!path) return;

  if (filled) {
    graphics_context_set_fill_color(ctx, color);
    gpath_draw_filled(ctx, path);
  }
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  gpath_draw_outline(ctx, path);
  gpath_destroy(path);
}

// A rectangle with its bottom-right corner sliced off.
void ui_cut_rect(GContext *ctx, GRect rect, int cut, GColor color, bool filled) {
  int16_t left = rect.origin.x;
  int16_t top = rect.origin.y;
  int16_t right = left + rect.size.w - 1;
  int16_t bottom = top + rect.size.h - 1;
  GPoint points[] = {
    { left, top }, { right, top }, { right, bottom - cut }, { right - cut, bottom },
    { left, bottom }
  };
  prv_poly(ctx, points, ARRAY_LENGTH(points), color, filled);
}

void ui_segments(GContext *ctx, GRect rect, int pct, int seg_w, int gap, GColor fill,
                 GColor track) {
  int count = (rect.size.w + gap) / (seg_w + gap);
  int lit = pct <= 0 ? 0 : (pct * count + 99) / 100;
  if (lit > count) lit = count;

  for (int i = 0; i < count; i++) {
    ui_fill(ctx, GRect(rect.origin.x + i * (seg_w + gap), rect.origin.y, seg_w, rect.size.h),
            i < lit ? fill : track);
  }
}

// Flat top and bottom, points to the left and right.
void ui_hexagon(GContext *ctx, GPoint center, int radius, GColor color, bool filled) {
  int half = radius / 2;
  int height = radius * 866 / 1000;
  GPoint points[] = {
    { center.x - radius, center.y }, { center.x - half, center.y - height },
    { center.x + half, center.y - height }, { center.x + radius, center.y },
    { center.x + half, center.y + height }, { center.x - half, center.y + height }
  };
  prv_poly(ctx, points, ARRAY_LENGTH(points), color, filled);
}

void ui_diamond(GContext *ctx, GPoint center, int radius, GColor color, bool filled) {
  GPoint points[] = {
    { center.x, center.y - radius }, { center.x + radius, center.y },
    { center.x, center.y + radius }, { center.x - radius, center.y }
  };
  prv_poly(ctx, points, ARRAY_LENGTH(points), color, filled);
}

void ui_triangle(GContext *ctx, GPoint center, int radius, bool up, GColor color) {
  int tip = up ? -radius : radius;
  GPoint points[] = {
    { center.x, center.y + tip }, { center.x + radius, center.y - tip / 2 },
    { center.x - radius, center.y - tip / 2 }
  };
  prv_poly(ctx, points, ARRAY_LENGTH(points), color, true);
}

// A row of little squares, right-aligned in rect: lit ones filled, the rest hollow.
void ui_pips(GContext *ctx, GRect rect, int lit, int count, GColor color) {
  int size = rect.size.h;
  int gap = 3;
  int x = rect.origin.x + rect.size.w - count * (size + gap) + gap;
  for (int i = 0; i < count; i++) {
    GRect pip = GRect(x + i * (size + gap), rect.origin.y, size, size);
    if (i < lit) {
      ui_fill(ctx, pip, color);
    } else {
      ui_frame(ctx, pip, COL_DIM, 1);
    }
  }
}

void buzz(Buzz kind) {
  if (!g_records.haptics) return;

  static const uint32_t TICK[] = { 22 };
  static const uint32_t BAD[] = { 70, 60, 70 };
  static const uint32_t WIN[] = { 40, 70, 40, 70, 140 };
  static const uint32_t LOSE[] = { 350 };
  VibePattern pattern;
  switch (kind) {
    case BUZZ_TICK: pattern = (VibePattern) { TICK, ARRAY_LENGTH(TICK) }; break;
    case BUZZ_BAD: pattern = (VibePattern) { BAD, ARRAY_LENGTH(BAD) }; break;
    case BUZZ_WIN: pattern = (VibePattern) { WIN, ARRAY_LENGTH(WIN) }; break;
    case BUZZ_LOSE:
    default: pattern = (VibePattern) { LOSE, ARRAY_LENGTH(LOSE) }; break;
  }
  vibes_cancel();
  vibes_enqueue_custom_pattern(pattern);
}

int rand_range(int lo, int hi) {
  return lo + rand() % (hi - lo + 1);
}
