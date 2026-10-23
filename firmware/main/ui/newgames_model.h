/* 0.1.2 new games: deterministic integer rules; no UI, audio or allocation. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { NG_READY, NG_RUN, NG_PAUSE, NG_OVER, NG_WIN };
typedef struct { int32_t xq; int16_t y; uint8_t passed; } ng_gate_t;
typedef struct {
  uint32_t seed, elapsed; int32_t yq; int16_t reference, score;
  uint16_t carry, silent_ms, flash_ms; uint8_t state, difficulty, lives, cleared, seen, combo;
  ng_gate_t gate[4];
} ng_flight_t;
void ng_flight_init(ng_flight_t *s, uint32_t seed, int difficulty);
void ng_flight_start(ng_flight_t *s, int midi100);
void ng_flight_step(ng_flight_t *s, int dt, int midi100, bool voiced);
int ng_flight_gap(const ng_flight_t *s);
typedef struct {
  uint32_t hit_mask; int32_t last_hit, now;
  int16_t score, last_error; uint16_t period;
  uint8_t state, judged, hits, perfect, misses, extra, combo, best_combo;
} ng_clap_t;
void ng_clap_init(ng_clap_t *s, int bpm);
void ng_clap_start(ng_clap_t *s);
void ng_clap_step(ng_clap_t *s, int now_ms);
/* elapsed sound onset (latency already compensated); returns 2 perfect,1 good,0 ignored,-1 extra. */
int ng_clap_hit(ng_clap_t *s, int onset_ms);
