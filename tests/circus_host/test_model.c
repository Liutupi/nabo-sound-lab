/* Host only: rules of the four-stage challenge. No UI, audio or device access. */
#include "circus_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define check(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); exit(2); } } while (0)

static void snapshot(const cr_t *s, int first) {
  printf("%s{\"phase\":%u,\"stage\":%u,\"t\":%ld,\"total\":%u,\"score\":[%u,%u,%u,%u],\"lit\":%u,\"blow_ms\":%ld,\"target\":%d,\"cents\":%d,\"prog\":%u,\"dev_sum\":%ld,\"dev_n\":%u,"
         "\"flight\":[%u,%u,%u,%ld],\"clap\":[%u,%d,%u],\"seed\":%lu}", first ? "" : ",", s->phase, s->stage, (long)s->t, s->total, s->score[0], s->score[1], s->score[2], s->score[3],
         s->lit, (long)s->blow_ms, s->target, s->cents, s->prog, (long)s->dev_sum, s->dev_n, s->flight.state, s->flight.cleared, s->flight.lives, (long)s->flight.yq,
         s->clap.state, s->clap.score, s->clap.hits, (unsigned long)s->seed);
}
static void trace(void) {
  cr_t s; cr_init(&s, 20261110); int clap_t = 0, first = 1;
  printf("[");
  for (int round = 0; round < 2; round++) {
    cr_ok(&s);
    for (int i = 0; i < 9000 && s.phase != CR_FINAL; i++) {
      if (s.phase == CR_PLAY && s.stage == CR_CLAP) {
        clap_t += 20; int p = s.clap.period;
        if (clap_t >= 4 * p) { int k = (clap_t - 4 * p) / p; if ((clap_t - 4 * p) % p < 20 && k % 5 != 4) cr_clap_hit(&s, clap_t + (k % 3 == 0 ? 0 : k % 3 == 1 ? 60 : -100)); }
      } else clap_t = 0;
      if (i == 40 && round == 1) cr_ok(&s);
      if (i == 5000 && round == 1) cr_skip(&s);
      cr_in_t in = {.blow = (uint16_t)(i % 40 < 30 ? 150 + (i * 37) % 851 : 0), .midi100 = (int16_t)(6000 + ((i / 45) % 7 - 3) * 110), .voiced = i % 97 < 88, .clap_ms = clap_t};
      cr_step(&s, i % 7 == 0 ? 40 : 20, &in);
      if (i % 10 == 0) { snapshot(&s, first); first = 0; }
    }
    snapshot(&s, first); first = 0;
  }
  puts("]");
}

/* Run the current PLAY stage with a fixed input until it ends; returns elapsed ms. */
static int play(cr_t *s, cr_in_t in, int limit_ms) {
  int t = 0; while (s->phase == CR_PLAY && t < limit_ms) { cr_step(s, 20, &in); t += 20; } return t;
}
static void to_play(cr_t *s, int stage) { while (!(s->phase == CR_PLAY && s->stage == stage)) { if (s->phase == CR_PLAY) cr_skip(s); else cr_ok(s); } }

static void flow_tests(void) {
  cr_t s; cr_init(&s, 7); cr_in_t quiet = {0};
  check(s.phase == CR_TITLE && cr_step(&s, 5000, &quiet) == 0 && s.phase == CR_TITLE);       /* title waits for OK */
  check(cr_ok(&s) == 0 && s.phase == CR_INTRO && s.stage == CR_CANDLE);
  check(cr_skip(&s) == 0 && s.phase == CR_INTRO);                                            /* skip only acts during play */
  int ev = 0; for (int t = 0; t < CR_INTRO_MS; t += 20) ev = cr_step(&s, 20, &quiet);
  check(ev == CR_EV_PLAY && s.phase == CR_PLAY && s.lit == CR_CANDLES);
  check(cr_ok(&s) == 0 && s.phase == CR_PLAY);                                               /* OK cannot end a stage */
  /* split and merged time steps agree */
  cr_t a = s, b = s; cr_in_t blow = {.blow = 400};
  cr_step(&a, 200, &blow); for (int i = 0; i < 10; i++) cr_step(&b, 20, &blow);
  check(!memcmp(&a, &b, sizeof a));
  /* silence in every stage ends each stage by its own limit with 0 points, and the run still finishes */
  check(play(&s, quiet, 60000) == CR_CANDLE_LIMIT_MS && s.phase == CR_SCORE && s.score[CR_CANDLE] == 0);
  for (int t = 0; t < CR_SCORE_MS; t += 20) cr_step(&s, 20, &quiet);
  check(s.phase == CR_INTRO && s.stage == CR_ROPE);
  to_play(&s, CR_ROPE); check(play(&s, quiet, 60000) == CR_ROPE_LIMIT_MS && s.score[CR_ROPE] == 0);
  to_play(&s, CR_FLIGHT); check(play(&s, quiet, 60000) == CR_FLIGHT_WAIT_MS && s.score[CR_FLIGHT] == 0);
  to_play(&s, CR_CLAP);
  for (int t = 0; s.phase == CR_PLAY && t < 60000; t += 20) { cr_in_t in = {.clap_ms = t}; cr_step(&s, 20, &in); }
  check(s.phase == CR_SCORE && s.score[CR_CLAP] == 0 && s.clap.misses == 16);
  check(cr_ok(&s) == CR_EV_FINAL && s.phase == CR_FINAL && s.total == 0 && cr_notes(s.total) == 1 && cr_rank(s.total) == 0);
  uint32_t seed = s.seed; check(cr_ok(&s) == 0 && s.phase == CR_INTRO && s.stage == 0 && s.total == 0 && s.seed == seed);   /* play again */
}

static void candle_tests(void) {
  cr_t s; cr_init(&s, 1); to_play(&s, CR_CANDLE);
  cr_in_t wait = {0}, hard = {.blow = 1000}, soft = {.blow = 150};
  play(&s, wait, 3000); check(s.phase == CR_PLAY && s.blow_t0 < 0);                            /* waiting costs stage time, not score */
  play(&s, hard, 60000); check(s.phase == CR_SCORE && s.lit == 0 && s.blow_ms <= 1500 && s.score[CR_CANDLE] == 100);
  cr_init(&s, 1); to_play(&s, CR_CANDLE); play(&s, soft, 60000);
  check(s.lit == 0 && s.blow_ms > 4000 && s.score[CR_CANDLE] < 80 && s.score[CR_CANDLE] >= 40);
  cr_init(&s, 1); to_play(&s, CR_CANDLE); play(&s, hard, 400); check(s.lit > 0 && s.lit < CR_CANDLES);
  int out = CR_CANDLES - s.lit; play(&s, wait, 60000);
  check(s.phase == CR_SCORE && s.score[CR_CANDLE] == out * 40 / CR_CANDLES && s.score[CR_CANDLE] < 40);   /* unfinished is always below finished */
}

static void rope_tests(void) {
  cr_t s; cr_init(&s, 1); to_play(&s, CR_ROPE);
  cr_in_t steady = {.midi100 = 5700, .voiced = true};
  play(&s, steady, 60000);
  check(s.target == 5700 && s.prog == CR_ROPE_WALK_MS && s.score[CR_ROPE] == 100);
  /* 60 cents sharp after locking: finishes, but scores clearly lower */
  cr_init(&s, 1); to_play(&s, CR_ROPE); play(&s, steady, 400); check(s.target == 5700);
  cr_in_t sharp = {.midi100 = 5760, .voiced = true}; play(&s, sharp, 60000);
  check(s.prog == CR_ROPE_WALK_MS && s.score[CR_ROPE] >= 35 && s.score[CR_ROPE] < 70);
  /* stopping half way keeps partial credit only */
  cr_init(&s, 1); to_play(&s, CR_ROPE); play(&s, steady, 160 + 3000); cr_in_t quiet = {0}; play(&s, quiet, 60000);
  check(s.phase == CR_SCORE && s.score[CR_ROPE] >= 45 && s.score[CR_ROPE] <= 50);
  /* an octave jump from the pitch tracker is bounded to 200 cents */
  cr_init(&s, 1); to_play(&s, CR_ROPE); play(&s, steady, 400); cr_in_t octave = {.midi100 = 6900, .voiced = true}; play(&s, octave, 1000);
  check(s.cents <= 200 && s.cents > 150);
}

static void flight_clap_tests(void) {
  cr_t s; cr_init(&s, 99); to_play(&s, CR_FLIGHT);
  cr_in_t hum = {.midi100 = 6000, .voiced = true};
  play(&s, hum, 280); check(s.flight.state == NG_READY);
  play(&s, hum, 20); check(s.flight.state == NG_RUN && s.flight.reference == 6000);             /* auto start, no key */
  for (int i = 0; i < 4000 && s.phase == CR_PLAY; i++) {                                         /* steer through every gate */
    ng_gate_t *g = NULL; for (int j = 0; j < 4; j++) if (!s.flight.gate[j].passed && (!g || s.flight.gate[j].xq < g->xq)) g = &s.flight.gate[j];
    cr_in_t in = {.midi100 = (int16_t)(g ? 6000 + (156 - g->y) * 100 / 6 : 6000), .voiced = true}; cr_step(&s, 20, &in);
  }
  check(s.phase == CR_SCORE && s.flight.state == NG_WIN && s.score[CR_FLIGHT] == 100);
  check(cr_clap_hit(&s, 5000) == 0);                                                             /* claps outside the clap stage are ignored */
  to_play(&s, CR_CLAP); int p = s.clap.period; check(p == 60000 / CR_CLAP_BPM);
  for (int t = 0; s.phase == CR_PLAY && t < 60000; t += 20) {
    if (t >= 4 * p && (t - 4 * p) % p < 20) check(cr_clap_hit(&s, t) == 2);
    cr_in_t in = {.clap_ms = t}; cr_step(&s, 20, &in);
  }
  check(s.phase == CR_SCORE && s.score[CR_CLAP] == 100 && s.clap.perfect == 16);
  cr_skip(&s); check(s.phase == CR_SCORE);
}

static void total_tests(void) {
  cr_t s; cr_init(&s, 5); to_play(&s, CR_CANDLE); cr_in_t hard = {.blow = 1000}; play(&s, hard, 60000);
  to_play(&s, CR_ROPE); check(cr_skip(&s) == CR_EV_SCORE && s.score[CR_ROPE] == 0 && s.total == 100);
  check(cr_notes(0) == 1 && cr_notes(79) == 1 && cr_notes(80) == 2 && cr_notes(160) == 3 && cr_notes(260) == 4 && cr_notes(339) == 4 && cr_notes(340) == 5 && cr_notes(400) == 5);
  check(cr_rank(159) == 0 && cr_rank(160) == 1 && cr_rank(260) == 2 && cr_rank(340) == 3);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--trace")) { trace(); return 0; }
  flow_tests(); candle_tests(); rope_tests(); flight_clap_tests(); total_tests();
  puts("PASS circus rules: flow, timeouts, split steps, candle/rope/flight/clap scoring, skip and grades");
  return 0;
}
