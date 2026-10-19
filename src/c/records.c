#include "icebreaker.h"

#define RECORDS_KEY 1
#define RECORDS_VERSION 1

Records g_records;

void records_load(void) {
  memset(&g_records, 0, sizeof(g_records));
  g_records.version = RECORDS_VERSION;
  g_records.haptics = true;

  Records saved;
  if (persist_read_data(RECORDS_KEY, &saved, sizeof(saved)) == (int)sizeof(saved) &&
      saved.version == RECORDS_VERSION) {
    g_records = saved;
  }
}

void records_save(void) {
  persist_write_data(RECORDS_KEY, &g_records, sizeof(g_records));
}

// Earned on the heist, the only mode that mixes every kind of lock.
const char *records_rank(uint32_t heist_score) {
  if (heist_score >= 9000) return "LEGEND";
  if (heist_score >= 5000) return "ICEBREAKER";
  if (heist_score >= 2500) return "NETRUNNER";
  if (heist_score >= 1000) return "GLITCHER";
  return "SCRIPT KIDDIE";
}
