/* Integer rhythm rules. Browser counterpart: prototype/duel-rules.js. */
#include "duel_rules.h"
#include <string.h>
#include <stdlib.h>
static uint32_t random32(uint32_t *a) {
  *a += 0x6D2B79F5u; uint32_t t = *a;
  t = (t ^ (t >> 15)) * (1u | t);
  t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}
void sd_score_init(sd_score_t *s, uint32_t seed, int round) {
  memset(s, 0, sizeof *s);
  s->round = (uint8_t)round;
  s->beat = (uint16_t)(60000 / (90 + (round - 1) * 15));
  seed ^= (uint32_t)round * 0x9e3779b9u;
  for (int i = 0; i < 8; i++) {
    s->at[s->n++] = (uint16_t)((4 + i) * s->beat);
    if (round > 1 && (random32(&seed) & 3) < (uint32_t)(round - 1))
      s->at[s->n++] = (uint16_t)((4 + i) * s->beat + s->beat / 2);
  }
  s->duration = (uint16_t)(12 * s->beat + 250);
  s->last_error = 999;
  s->last_index = -1;
}
int sd_score_tap(sd_score_t *s, int32_t elapsed) {
  if (elapsed < (int32_t)s->at[0] - 180 || elapsed > s->duration) return -1;
  int best = -1, err = 181;
  for (int i = 0; i < s->n; i++) {
    int e = abs((int)(elapsed - s->at[i]));
    if (!(s->hit & (1u << i)) && e < err) { best = i; err = e; }
  }
  if (best < 0) { if (s->extras < 1000) s->extras++; s->combo = 0; return 0; }
  s->hit |= (uint16_t)(1u << best); s->last_error = (int16_t)(elapsed - s->at[best]);
  int points = err <= 60 ? 100 : err <= 120 ? 60 : 20;
  s->earned += (uint16_t)points;
  if (best != s->last_index + 1) s->combo = 0;
  s->last_index = (int8_t)best; s->combo++;
  if (s->combo > s->best) s->best = s->combo;
  return points;
}
int sd_score_value(const sd_score_t *s) {
  int score = s->earned - s->extras * 20;
  return score > 0 ? score * 1000 / (s->n * 100) : 0;
}
