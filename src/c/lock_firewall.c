#include "icebreaker.h"

// FIREWALL, the reflex lock: a packet rides four lanes of a data stream for twenty
// seconds while walls of ICE scroll in from the right. Shards charge a pulse that blows
// the next wall in the packet's lane away.
#define LANES 4
#define MAX_OBJS 24
#define RUN_MS 20000
#define CALM_MS 1800       // No new walls this close to the end, so the exit is clear
#define FP 16              // Positions are kept in sixteenths of a pixel
#define PACKET_X 24
#define PACKET_HALF 7
#define WALL_W 12
#define WALL_H 34
#define SHARD_R 6
#define LANE_TOP 12        // From the top of the play area
#define LANE_H 45
#define PULSE_RANGE 120
#define PULSE_MS 180
#define HIT_MS 900
#define SHARDS_PER_CHARGE 3
#define MAX_CHARGES 3
#define LIVES 3

typedef enum {
  OBJ_NONE = 0,
  OBJ_WALL,
  OBJ_SHARD
} ObjKind;

typedef struct {
  int32_t x;      // Left edge, in FP units
  uint8_t lane;
  uint8_t kind;
  bool passed;
} Obj;

static int s_level;
static Obj s_objs[MAX_OBJS];
static int s_lane;
static int s_draw_y;        // Eases toward the lane so a change reads as movement; from the
                            // top of the play area
static int s_speed;         // Pixels a second
static int s_spacing;       // Pixels between waves
static int s_min_walls;
static int s_max_walls;
static int32_t s_travel;    // FP units scrolled since the start
static int32_t s_next_wave;
static int s_time_ms;
static int s_hit_ms;
static int s_charges;
static int s_shards;
static int s_pulse_ms;
static int s_pulse_end;     // Where the beam stopped, in pixels

static int prv_lane_y(int lane, int top) {
  return top + LANE_TOP + lane * LANE_H + LANE_H / 2;
}

static void prv_spawn(ObjKind kind, int lane, int32_t x) {
  for (int i = 0; i < MAX_OBJS; i++) {
    if (s_objs[i].kind != OBJ_NONE) continue;
    s_objs[i] = (Obj) { .x = x, .lane = lane, .kind = kind, .passed = false };
    return;
  }
}

static void prv_wave(void) {
  int32_t x = (SCREEN_W + 4) * FP;
  int walls = rand_range(s_min_walls, s_max_walls);
  bool blocked[LANES] = { false };
  for (int placed = 0; placed < walls;) {
    int lane = rand() % LANES;
    if (blocked[lane]) continue;
    blocked[lane] = true;
    placed++;
  }
  for (int lane = 0; lane < LANES; lane++) {
    if (blocked[lane]) prv_spawn(OBJ_WALL, lane, x);
  }

  // Shards sit in the gaps, half a wave behind.
  if (rand() % 100 < 45) {
    prv_spawn(OBJ_SHARD, rand() % LANES, x + s_spacing * FP / 2);
  }
}

static void prv_start(int level) {
  s_level = level;
  memset(s_objs, 0, sizeof(s_objs));
  s_lane = 1;
  s_draw_y = prv_lane_y(s_lane, 0);
  s_speed = 70 + 14 * level;
  if (s_speed > 220) s_speed = 220;
  s_spacing = 125 - 7 * level;
  if (s_spacing < 80) s_spacing = 80;
  s_min_walls = level >= 4 ? 2 : 1;
  s_max_walls = level >= 3 ? 3 : 2;
  s_travel = 0;
  s_next_wave = 40 * FP;
  s_time_ms = 0;
  s_hit_ms = 0;
  s_charges = 1;
  s_shards = 0;
  s_pulse_ms = 0;
  g_play.lives = g_play.lives_max = LIVES;
}

static bool prv_overlaps(const Obj *obj) {
  int left = obj->x / FP;
  int right = left + (obj->kind == OBJ_WALL ? WALL_W : SHARD_R * 2);
  return obj->lane == s_lane && right >= PACKET_X - PACKET_HALF && left <= PACKET_X + PACKET_HALF;
}

static void prv_tick(int dt) {
  s_time_ms += dt;
  if (s_hit_ms > 0) s_hit_ms -= dt;
  if (s_pulse_ms > 0) s_pulse_ms -= dt;
  int lane_y = prv_lane_y(s_lane, 0);
  s_draw_y += (lane_y - s_draw_y + (lane_y > s_draw_y ? 1 : -1)) / 2;

  int32_t move = s_speed * dt * FP / 1000;
  s_travel += move;
  if (s_travel >= s_next_wave && s_time_ms < RUN_MS - CALM_MS) {
    prv_wave();
    s_next_wave += s_spacing * FP;
  }

  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *obj = &s_objs[i];
    if (obj->kind == OBJ_NONE) continue;
    obj->x -= move;
    if (obj->x < -WALL_W * 2 * FP) {
      obj->kind = OBJ_NONE;
      continue;
    }

    if (prv_overlaps(obj)) {
      if (obj->kind == OBJ_SHARD) {
        obj->kind = OBJ_NONE;
        g_play.score += 10 * s_level;
        if (++s_shards % SHARDS_PER_CHARGE == 0 && s_charges < MAX_CHARGES) s_charges++;
        buzz(BUZZ_TICK);
      } else if (s_hit_ms <= 0) {
        obj->kind = OBJ_NONE;
        s_hit_ms = HIT_MS;
        if (--g_play.lives <= 0) {
          g_play.state = PLAY_FAILED;
          return;
        }
        buzz(BUZZ_BAD);
      }
    } else if (obj->kind == OBJ_WALL && !obj->passed &&
               obj->x / FP + WALL_W < PACKET_X - PACKET_HALF) {
      obj->passed = true;
      g_play.score += 5;
    }
  }

  if (s_time_ms >= RUN_MS) {
    g_play.score += 60 * s_level + 30 * g_play.lives;
    g_play.state = PLAY_CRACKED;
  }
}

static void prv_pulse(void) {
  if (s_charges <= 0) return;
  Obj *target = NULL;
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *obj = &s_objs[i];
    int left = obj->x / FP;
    if (obj->kind != OBJ_WALL || obj->lane != s_lane || left < PACKET_X ||
        left > PACKET_X + PULSE_RANGE) {
      continue;
    }
    if (!target || obj->x < target->x) target = obj;
  }

  s_pulse_ms = PULSE_MS;
  if (!target) {
    s_pulse_end = PACKET_X + PULSE_RANGE;
    return;
  }
  s_pulse_end = target->x / FP + WALL_W / 2;
  target->kind = OBJ_NONE;
  s_charges--;
  g_play.score += 5;
}

static void prv_press(ButtonId button) {
  if (button == BUTTON_ID_UP && s_lane > 0) s_lane--;
  else if (button == BUTTON_ID_DOWN && s_lane < LANES - 1) s_lane++;
  else if (button == BUTTON_ID_SELECT) prv_pulse();
}

static void prv_draw(GContext *ctx, GRect area) {
  int top = area.origin.y;

  int progress = s_time_ms * 100 / RUN_MS;
  ui_segments(ctx, GRect(4, top + 1, 150, 5), progress, 4, 1, COL_CYAN, COL_DARK);
  for (int i = 0; i < MAX_CHARGES; i++) {
    ui_diamond(ctx, GPoint(166 + i * 12, top + 3), 4, COL_YELLOW, i < s_charges);
  }

  // Lane dividers dash past at the stream's speed.
  int scroll = (s_travel / FP) % 16;
  graphics_context_set_stroke_color(ctx, COL_DARK);
  for (int lane = 0; lane <= LANES; lane++) {
    int y = top + LANE_TOP + lane * LANE_H;
    for (int x = -scroll; x < SCREEN_W; x += 16) {
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x + 7, y));
    }
  }

  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *obj = &s_objs[i];
    if (obj->kind == OBJ_NONE) continue;
    int x = obj->x / FP;
    int y = prv_lane_y(obj->lane, top);
    if (obj->kind == OBJ_WALL) {
      GRect wall = GRect(x, y - WALL_H / 2, WALL_W, WALL_H);
      ui_fill(ctx, wall, COL_TRACK);
      ui_frame(ctx, wall, COL_RED, 1);
      graphics_context_set_stroke_color(ctx, COL_RED);
      for (int stripe = 6; stripe < WALL_H; stripe += 7) {
        graphics_draw_line(ctx, GPoint(x + 2, wall.origin.y + stripe),
                           GPoint(x + WALL_W - 3, wall.origin.y + stripe - 4));
      }
    } else {
      ui_diamond(ctx, GPoint(x + SHARD_R, y), SHARD_R, COL_YELLOW, true);
    }
  }

  int packet_y = top + s_draw_y;
  if (s_pulse_ms > 0) {
    int beam_y = prv_lane_y(s_lane, top);
    graphics_context_set_stroke_color(ctx, COL_YELLOW);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, GPoint(PACKET_X + PACKET_HALF, beam_y), GPoint(s_pulse_end, beam_y));
    graphics_context_set_stroke_width(ctx, 1);
  }

  // Blinks while it's shaking off a hit.
  bool visible = s_hit_ms <= 0 || (s_hit_ms / 100) % 2 == 0;
  if (visible) {
    GColor color = s_hit_ms > 0 ? COL_RED : COL_CYAN;
    for (int t = 1; t <= 3; t++) {
      ui_fill(ctx, GRect(PACKET_X - PACKET_HALF - t * 6, packet_y - 1, 3, 2), COL_DIM);
    }
    GPoint points[] = {
      { PACKET_X + PACKET_HALF, packet_y }, { PACKET_X - PACKET_HALF, packet_y - PACKET_HALF },
      { PACKET_X - PACKET_HALF + 4, packet_y }, { PACKET_X - PACKET_HALF, packet_y + PACKET_HALF }
    };
    GPathInfo info = { .num_points = ARRAY_LENGTH(points), .points = points };
    GPath *path = gpath_create(&info);
    if (path) {
      graphics_context_set_fill_color(ctx, color);
      gpath_draw_filled(ctx, path);
      gpath_destroy(path);
    }
  }
}

const LockDef LOCK_DEF_FIREWALL = {
  .name = "FIREWALL",
  .skill = "REFLEX",
  .brief = "Ride the stream for 20 seconds. Dodge red ICE. Every 3 yellow shards charges a "
           "pulse.",
  .controls = "UP/DN: SWITCH LANE\nSEL: PULSE THE WALL AHEAD",
  .start = prv_start,
  .tick = prv_tick,
  .draw = prv_draw,
  .press = prv_press
};
