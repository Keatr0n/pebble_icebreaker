#include "icebreaker.h"

const LockDef *const LOCKS[LOCK_COUNT] = {
  [LOCK_ROTOR] = &LOCK_DEF_ROTOR,
  [LOCK_BREACH] = &LOCK_DEF_BREACH,
  [LOCK_ECHO] = &LOCK_DEF_ECHO,
  [LOCK_CIPHER] = &LOCK_DEF_CIPHER,
  [LOCK_BITLOCK] = &LOCK_DEF_BITLOCK,
  [LOCK_FIREWALL] = &LOCK_DEF_FIREWALL
};

static void prv_init(void) {
  srand(time(NULL));
  records_load();
  ui_init();
  card_init();
  game_init();
  menu_init();
}

static void prv_deinit(void) {
  session_bank();
  menu_deinit();
  game_deinit();
  card_deinit();
  ui_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
