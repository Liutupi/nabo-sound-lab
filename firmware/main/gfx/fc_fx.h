/* 定点数工具：千分比进度、缓动、正弦表。全部整数，ESP32-C3 无浮点单元。 */
#pragma once
#include <stdint.h>

#define FX_ONE 1000                      /* 进度用千分比 0..1000 */
#define ANG_TURN 1024                    /* 角度：1 圈 = 1024 */
#define ANG_DEG(d) ((int)((d) * 1024 / 360))

static inline int fx_clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline int fx_lerp(int a, int b, int p) { return a + (int)((int64_t)(b - a) * p / FX_ONE); }
static inline int fx_prog(int t, int dur) { return dur <= 0 ? FX_ONE : fx_clamp((int)((int64_t)t * FX_ONE / dur), 0, FX_ONE); }

static inline int ease_out(int t) { int u = FX_ONE - t; return FX_ONE - (int)((int64_t)u * u / FX_ONE * u / FX_ONE); }
static inline int ease_in(int t) { return (int)((int64_t)t * t / FX_ONE * t / FX_ONE); }
static inline int ease_io(int t) {
  if (t < FX_ONE / 2) return (int)(4LL * t * t / FX_ONE * t / FX_ONE);
  int u = 2 * FX_ONE - 2 * t; return FX_ONE - (int)((int64_t)u * u / FX_ONE * u / FX_ONE / 2);
}
/* 回弹：s = 1.9 */
static inline int ease_back(int t) {
  int64_t v = t - FX_ONE, s = 1900;
  return (int)(v * v / FX_ONE * ((s + FX_ONE) * v / FX_ONE + s) / FX_ONE + FX_ONE);
}

/* 正弦表：fx_sin(a) 返回 Q14（-16384..16384），a 以 1/1024 圈计 */
int fx_sin(int a);
static inline int fx_cos(int a) { return fx_sin(a + ANG_TURN / 4); }
/* 毫秒 × 角速度：phase(ms, period_ms) = 每 period 一圈 */
static inline int fx_phase(uint32_t ms, uint32_t period) { return (int)((uint64_t)ms * ANG_TURN / period); }
