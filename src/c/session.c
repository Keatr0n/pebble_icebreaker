#include "icebreaker.h"

#define HEIST_INTEGRITY 3
#define FLOOR_BONUS 25

Session g_session;

// Heist locks come out of a shuffled bag, so every kind turns up once per six floors.
static uint8_t s_bag[LOCK_COUNT];
static int s_bag_left;

static LockId prv_next_heist_lock(void) {
  if (s_bag_left == 0) {
    for (int i = 0; i < LOCK_COUNT; i++) s_bag[i] = i;
    for (int i = LOCK_COUNT - 1; i > 0; i--) {
      int j = rand_range(0, i);
      uint8_t swap = s_bag[i];
      s_bag[i] = s_bag[j];
      s_bag[j] = swap;
    }
    // The last lock of one bag mustn't open the next.
    if (g_session.has_last && s_bag[LOCK_COUNT - 1] == g_session.lock) {
      s_bag[LOCK_COUNT - 1] = s_bag[0];
      s_bag[0] = g_session.lock;
    }
    s_bag_left = LOCK_COUNT;
  }
  return s_bag[--s_bag_left];
}

static void prv_end(EndReason reason) {
  g_session.end = reason;
  bool scored = g_session.score > 0;

  if (g_session.mode == MODE_HEIST) {
    if (scored && (uint32_t)g_session.score > g_records.heist_best) {
      g_session.new_record = true;
      g_records.heist_best = g_session.score;
    }
    if (g_session.floor > g_records.heist_floor) g_records.heist_floor = g_session.floor;
  } else {
    LockId lock = g_session.lock;
    if (scored && (uint32_t)g_session.score > g_records.lock_best[lock]) {
      g_session.new_record = true;
      g_records.lock_best[lock] = g_session.score;
    }
    if (g_session.level > g_records.lock_level[lock]) {
      g_records.lock_level[lock] = g_session.level;
    }
  }
  records_save();
}

void session_start(Mode mode, LockId lock) {
  memset(&g_session, 0, sizeof(g_session));
  g_session.mode = mode;
  g_session.level = 1;
  g_session.floor = 1;
  g_session.integrity = HEIST_INTEGRITY;
  s_bag_left = 0;
  g_session.lock = mode == MODE_HEIST ? prv_next_heist_lock() : lock;
  card_show();
}

void session_lock_finished(bool cracked, int score) {
  g_session.has_last = true;
  g_session.last_lock = g_session.lock;
  g_session.last_cracked = cracked;
  g_session.last_score = score;
  if (cracked) {
    g_session.cracked++;
    g_records.cracked++;
  }

  if (g_session.mode == MODE_HEIST) {
    if (cracked) {
      g_session.last_score += FLOOR_BONUS * g_session.floor;
    } else {
      g_session.integrity--;
    }
    g_session.score += g_session.last_score;
    if (g_session.integrity <= 0) {
      prv_end(END_FLATLINED);
    } else {
      g_session.floor++;
      g_session.level = (g_session.floor + 1) / 2;
      g_session.lock = prv_next_heist_lock();
      records_save();
    }
  } else {
    g_session.score += score;
    if (cracked) {
      g_session.level++;
      records_save();
    } else {
      prv_end(END_ICE_HOLDS);
    }
  }
  card_show();
}

void session_abort(void) {
  prv_end(END_JACKED_OUT);
  card_show();
}

// Leaving the app mid-run still counts the run.
void session_bank(void) {
  if (g_session.end == END_NONE && g_session.score > 0) prv_end(END_JACKED_OUT);
}
