#pragma once
#include <stdint.h>
#include <stdbool.h>
#define SD_NOTES 16
typedef struct {
  uint16_t at[SD_NOTES], hit;
  uint8_t n, round, combo, best;
  int8_t last_index; /* Previous matched target, so skipped/reversed notes break combo. */
  uint16_t earned, extras;
  int16_t last_error;
  uint16_t duration, beat;
} sd_score_t;
void sd_score_init(sd_score_t *s, uint32_t seed, int round);
int sd_score_tap(sd_score_t *s, int32_t elapsed);
int sd_score_value(const sd_score_t *s);
