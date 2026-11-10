#include "circus_model.h"
#include <string.h>

static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t random32(uint32_t *seed) {
  uint32_t t = (*seed += 0x6D2B79F5u);
  t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}

void cr_init(cr_t *s, uint32_t seed) { memset(s, 0, sizeof *s); s->seed = seed ? seed : 1; s->phase = CR_TITLE; }

static void stage_begin(cr_t *s) {
  s->phase = CR_PLAY; s->t = 0; s->carry = 0;
  switch (s->stage) {
  case CR_CANDLE:
    for (int i = 0; i < CR_CANDLES; i++) s->life[i] = (int16_t)(500 + ((i * 7) % 5) * 150);
    s->lit = CR_CANDLES; s->blow_t0 = -1; s->blow_ms = 0; break;
  case CR_ROPE:
    s->target = s->cents = 0; s->lock = 0; s->prog = s->dev_n = 0; s->dev_sum = 0; break;
  case CR_FLIGHT:
    ng_flight_init(&s->flight, random32(&s->seed), 0); s->hum_ms = 0; break;
  default:
    ng_clap_init(&s->clap, CR_CLAP_BPM); ng_clap_start(&s->clap); break;
  }
}

static int stage_score(const cr_t *s) {
  switch (s->stage) {
  case CR_CANDLE:
    if (s->lit) return (CR_CANDLES - s->lit) * 40 / CR_CANDLES;            /* 没吹完：最多 33 分 */
    return 100 - clamp((s->blow_ms - 1500) * 60 / 6500, 0, 60);              /* 1.5 秒内满分，8 秒以上 40 分 */
  case CR_ROPE: {
    if (!s->dev_n) return 0;
    int mean = s->dev_sum / s->dev_n;                                        /* 平均偏差，音分 */
    int q = clamp(1000 - mean * 1000 / 80, 0, 1000);
    return s->prog * 100 / CR_ROPE_WALK_MS * (350 + 650 * q / 1000) / 1000;  /* 走完至少 35 分 */
  }
  case CR_FLIGHT: return s->flight.cleared * 100 / 12;
  default: return clamp(s->clap.score / 16, 0, 100);
  }
}

static int stage_end(cr_t *s) {
  s->score[s->stage] = (uint8_t)clamp(stage_score(s), 0, 100);
  s->total = 0; for (int i = 0; i <= s->stage; i++) s->total = (uint16_t)(s->total + s->score[i]);
  s->phase = CR_SCORE; s->t = 0; s->carry = 0;
  return CR_EV_SCORE;
}

static int advance(cr_t *s) {     /* 离开 SCORE */
  s->t = 0; s->carry = 0;
  if (s->stage + 1 >= CR_STAGES) { s->phase = CR_FINAL; return CR_EV_FINAL; }
  s->stage++; s->phase = CR_INTRO; return 0;
}

static int tick(cr_t *s, const cr_in_t *in) {     /* PLAY 阶段的一个 20 ms 步 */
  s->t += 20;
  switch (s->stage) {
  case CR_CANDLE:
    if (in->blow) {
      if (s->blow_t0 < 0) s->blow_t0 = s->t - 20;
      for (int i = 0; i < CR_CANDLES; i++) if (s->life[i] > 0) {
        s->life[i] = (int16_t)(s->life[i] - clamp(in->blow, 0, 1000) * 3 / 100);
        if (s->life[i] <= 0) { s->life[i] = 0; s->lit--; }
      }
    }
    if (!s->lit) { s->blow_ms = s->t - s->blow_t0; return stage_end(s); }
    if (s->t >= CR_CANDLE_LIMIT_MS) return stage_end(s);
    break;
  case CR_ROPE:
    if (!s->target) {                                   /* 连续 160 ms 有音高就锁定目标音 */
      s->lock = in->voiced ? (uint8_t)(s->lock + 1) : 0;
      if (s->lock >= 8) s->target = (int16_t)clamp(in->midi100, 2400, 9600);
    } else if (in->voiced) {
      int d = clamp(in->midi100 - s->target, -200, 200);
      s->cents = (int16_t)(s->cents + (d - s->cents) * 2 / 10);
      s->dev_sum += s->cents < 0 ? -s->cents : s->cents; s->dev_n++;
      s->prog = (uint16_t)(s->prog + 20);
      if (s->prog >= CR_ROPE_WALK_MS) return stage_end(s);
    }
    if (s->t >= CR_ROPE_LIMIT_MS) return stage_end(s);
    break;
  case CR_FLIGHT:
    if (s->flight.state == NG_READY) {                  /* 连续哼 300 ms 自动校准起飞，不用按键 */
      s->hum_ms = in->voiced ? (uint16_t)(s->hum_ms + 20) : 0;
      if (s->hum_ms >= 300) ng_flight_start(&s->flight, in->midi100);
      else if (s->t >= CR_FLIGHT_WAIT_MS) return stage_end(s);
    } else {
      ng_flight_step(&s->flight, 20, in->midi100, in->voiced);
      if (s->flight.state == NG_WIN || s->flight.state == NG_OVER) return stage_end(s);
    }
    break;
  default:
    ng_clap_step(&s->clap, in->clap_ms);
    if (s->clap.state == NG_OVER) return stage_end(s);
    break;
  }
  return 0;
}

int cr_step(cr_t *s, int dt, const cr_in_t *in) {
  if (dt <= 0 || s->phase == CR_TITLE || s->phase == CR_FINAL) return 0;
  int ev = 0;
  s->carry = (uint16_t)(s->carry + clamp(dt, 0, 1000));
  while (s->carry >= 20) {
    s->carry -= 20;
    if (s->phase == CR_PLAY) { ev |= tick(s, in); if (ev) break; }
    else {
      s->t += 20;
      if (s->phase == CR_INTRO && s->t >= CR_INTRO_MS) { stage_begin(s); ev |= CR_EV_PLAY; break; }
      if (s->phase == CR_SCORE && s->t >= CR_SCORE_MS) { ev |= advance(s); break; }
    }
  }
  if (ev) s->carry = 0;
  return ev;
}

int cr_ok(cr_t *s) {
  if (s->phase == CR_INTRO) { stage_begin(s); return CR_EV_PLAY; }
  if (s->phase == CR_SCORE) return advance(s);
  if (s->phase == CR_PLAY) return 0;
  if (s->phase == CR_FINAL) { uint32_t seed = s->seed; cr_init(s, seed); }   /* 再来一局：换一条飞行路线 */
  s->stage = 0; s->phase = CR_INTRO; s->t = 0; s->carry = 0;
  return 0;
}

int cr_skip(cr_t *s) { return s->phase == CR_PLAY ? stage_end(s) : 0; }
int cr_clap_hit(cr_t *s, int onset_ms) { return s->phase == CR_PLAY && s->stage == CR_CLAP ? ng_clap_hit(&s->clap, onset_ms) : 0; }
int cr_notes(int total) { return total >= 340 ? 5 : total >= 260 ? 4 : total >= 160 ? 3 : total >= 80 ? 2 : 1; }
int cr_rank(int total) { return total >= 340 ? 3 : total >= 260 ? 2 : total >= 160 ? 1 : 0; }
