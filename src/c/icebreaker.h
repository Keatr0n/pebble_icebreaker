#pragma once

#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228
#define HEADER_H 22
#define PLAY_TOP 34      // Below the header and the trace strip
#define FRAME_MS 33

#define COL_BG GColorBlack
#define COL_YELLOW GColorYellow
#define COL_RED GColorFolly
#define COL_CYAN GColorCyan
#define COL_PINK GColorShockingPink
#define COL_GREEN GColorScreaminGreen
#define COL_PALE GColorIcterine
#define COL_DIM GColorDarkGray
#define COL_DARK GColorOxfordBlue
#define COL_TRACK GColorBulgarianRose

typedef enum {
  LOCK_ROTOR = 0,
  LOCK_BREACH,
  LOCK_ECHO,
  LOCK_CIPHER,
  LOCK_BITLOCK,
  LOCK_FIREWALL,
  LOCK_COUNT
} LockId;

typedef enum {
  PLAY_ON = 0,
  PLAY_CRACKED,
  PLAY_FAILED
} PlayState;

// What the running lock reports to the game window, which draws the HUD from it and
// ends the lock once state leaves PLAY_ON.
typedef struct {
  PlayState state;
  int score;      // Points earned inside this lock
  int trace;      // 0-100 alarm meter; -1 for a lock without a clock
  int lives;      // Mistakes left; -1 for a lock that doesn't count them
  int lives_max;
} Play;

// One minigame. Only one runs at a time, so each keeps its state in file statics.
typedef struct {
  const char *name;
  const char *skill;
  const char *brief;
  const char *controls;
  void (*start)(int level);
  void (*tick)(int dt_ms);
  void (*draw)(GContext *ctx, GRect area);
  void (*press)(ButtonId button);
  void (*hold)(ButtonId button);   // Optional; fires once a button has been held a while
  bool (*back)(void);              // Optional; returns false to let Back pause as usual
} LockDef;

typedef enum {
  MODE_TRAINING = 0,  // One lock, a level higher each time it's cracked, until it isn't
  MODE_HEIST          // A random lock per floor with three strikes of integrity
} Mode;

typedef enum {
  END_NONE = 0,
  END_FLATLINED,     // Heist integrity ran out
  END_ICE_HOLDS,     // A training lock wasn't cracked
  END_JACKED_OUT     // The player quit
} EndReason;

typedef struct {
  Mode mode;
  LockId lock;
  int level;
  int floor;
  int integrity;
  int score;
  int cracked;
  EndReason end;
  bool new_record;
  bool has_last;      // A lock has been played, and last_* describe it
  LockId last_lock;
  bool last_cracked;
  int last_score;
} Session;

typedef struct {
  uint8_t version;
  bool haptics;
  uint16_t heist_floor;
  uint32_t heist_best;
  uint32_t lock_best[LOCK_COUNT];
  uint8_t lock_level[LOCK_COUNT];
  uint32_t cracked;
} Records;

typedef enum {
  BUZZ_TICK,
  BUZZ_BAD,
  BUZZ_WIN,
  BUZZ_LOSE
} Buzz;

extern Play g_play;
extern Session g_session;
extern Records g_records;
extern GFont g_font_label;
extern GFont g_font_title;
extern const LockDef *const LOCKS[LOCK_COUNT];

extern const LockDef LOCK_DEF_ROTOR;
extern const LockDef LOCK_DEF_BREACH;
extern const LockDef LOCK_DEF_ECHO;
extern const LockDef LOCK_DEF_CIPHER;
extern const LockDef LOCK_DEF_BITLOCK;
extern const LockDef LOCK_DEF_FIREWALL;

// records.c
void records_load(void);
void records_save(void);
const char *records_rank(uint32_t heist_score);

// session.c
void session_start(Mode mode, LockId lock);
void session_lock_finished(bool cracked, int score);
void session_abort(void);
void session_bank(void);

// menu.c, card.c, game.c
void menu_init(void);
void menu_deinit(void);
void card_init(void);
void card_deinit(void);
void card_show(void);
void game_init(void);
void game_deinit(void);
void game_show(LockId lock, int level);

// ui.c
void ui_init(void);
void ui_deinit(void);
void ui_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
             GColor color);
void ui_wrap(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
             GColor color);
void ui_label(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color);
void ui_title(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color,
              int glitch);
void ui_fill(GContext *ctx, GRect rect, GColor color);
void ui_frame(GContext *ctx, GRect rect, GColor color, int width);
void ui_cut_rect(GContext *ctx, GRect rect, int cut, GColor color, bool filled);
void ui_segments(GContext *ctx, GRect rect, int pct, int seg_w, int gap, GColor fill,
                 GColor track);
void ui_hexagon(GContext *ctx, GPoint center, int radius, GColor color, bool filled);
void ui_diamond(GContext *ctx, GPoint center, int radius, GColor color, bool filled);
void ui_triangle(GContext *ctx, GPoint center, int radius, bool up, GColor color);
void ui_pips(GContext *ctx, GRect rect, int lit, int count, GColor color);
void buzz(Buzz kind);
int rand_range(int lo, int hi);  // Inclusive of both ends
