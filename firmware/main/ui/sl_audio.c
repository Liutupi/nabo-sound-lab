#include "sl_audio.h"
#include <string.h>
#include "../voice/nabo_voice.h"
#include "../gfx/fc_res.h"
#include "sl_sampler.h"

#define NV 12                    /* 同时响的拨弦数 */
#define KS_MAX 400               /* 最低到 40 Hz */

typedef struct {
  int16_t b[KS_MAX];
  uint16_t n, p;
  int32_t wait, left, fade, amp;   /* amp：Q15 */
} ks_t;

static ks_t V[NV];
static struct { uint32_t at; int16_t len, pos; uint8_t accent, on; } CK[4];
static uint32_t g_time, g_busy_until, g_metro_next, g_metro_count;
static int g_metro_bpm, g_metro_beats;
static nv_rec_t g_rec;
static nv_player_t g_pl;
static bool g_rec_on, g_play_on;
enum { VOICE_STOP_FADE = SL_SR / 200 }; /* 5 ms; no recording/heap buffer. */
static int32_t g_voice_last, g_voice_tail;
static unsigned g_voice_tail_pos = VOICE_STOP_FADE;
static uint32_t g_rec16;
static int g_level;
static const uint8_t *g_boot_pcm;
static uint32_t g_boot_samples, g_boot_pos;

__attribute__((weak)) void sl_audio_lock(void) {}
__attribute__((weak)) void sl_audio_unlock(void) {}

void sl_audio_init(void) { nv_rec_start(&g_rec); /* Build voice tables before the real-time audio task starts. */ memset(V, 0, sizeof V); memset(CK, 0, sizeof CK); g_time = g_busy_until = 0; g_metro_bpm = 0; g_rec_on = g_play_on = false; g_voice_last = g_voice_tail = 0; g_voice_tail_pos = VOICE_STOP_FADE; g_boot_pcm = NULL; g_boot_samples = g_boot_pos = 0; }
uint32_t sl_audio_time(void) { return g_time; }
bool sl_audio_busy(void) { return (int32_t)(g_busy_until - g_time) > 0; }

static uint32_t mulberry32(uint32_t *a) {
  *a += 0x6D2B79F5u; uint32_t t = *a;
  t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}

/* 440·2^((m−69)/12) 的采样周期：用半音表，不用浮点 */
static int period(int midi) {
  static const uint16_t P69[12] = {36364, 34323, 32396, 30578, 28862, 27242, 25713, 24270, 22908, 21622, 20408, 19263};  /* 16000/f ×1000，A4 往上 0..11 半音 */
  int d = midi - 69, oct = 0;
  while (d < 0) { d += 12; oct--; }
  while (d >= 12) { d -= 12; oct++; }
  int p = P69[d];
  p = oct >= 0 ? p >> oct : p << (-oct);
  p = (p + 500) / 1000;
  return p < 2 ? 2 : p > KS_MAX ? KS_MAX : p;
}

void sl_pluck(int midi, int delay_ms, int amp, int dur_ms) {
  sl_audio_lock();
  int k = 0; for (int i = 1; i < NV; i++) if (V[i].left + V[i].wait < V[k].left + V[k].wait) k = i;   /* 抢最快结束的那一路 */
  ks_t *v = &V[k];
  v->n = (uint16_t)period(midi); v->p = 0;
  uint32_t seed = (uint32_t)(midi * 7919 + delay_ms * 16);
  for (int i = 0; i < v->n; i++) v->b[i] = (int16_t)((int32_t)(mulberry32(&seed) >> 16) - 32768);
  v->wait = delay_ms * 16; v->left = dur_ms * 16; v->fade = 250 * 16; v->amp = amp * 32768 / 100;
  sl_audio_unlock();
}
void sl_strum(const int8_t *frets, const uint8_t *open, int n, bool down, int amp) {
  int k = 0;
  for (int j = 0; j < n; j++) {
    int s = down ? j : n - 1 - j;
    if (frets[s] < 0) continue;
    sl_pluck(open[s] + frets[s], k * 16, amp, 1800 - k * 16);
    k++;
  }
}
void sl_click(bool accent, uint32_t at) {
  sl_audio_lock();
  for (int i = 0; i < 4; i++) if (!CK[i].on) { CK[i].on = 1; CK[i].accent = accent; CK[i].at = at ? at : g_time; CK[i].pos = 0; CK[i].len = 25 * 16; break; }
  sl_audio_unlock();
}
void sl_metro(int bpm, int beats) {
  sl_audio_lock();
  g_metro_bpm = bpm; g_metro_beats = beats > 0 ? beats : 4; g_metro_count = 0; g_metro_next = g_time + 160;
  sl_audio_unlock();
}
int sl_metro_bpm(void) { return g_metro_bpm; }
uint32_t sl_metro_count(void) { return g_metro_count; }

static void voice_stop_locked(void) {
  if (g_play_on) { g_voice_tail = g_voice_last; g_voice_tail_pos = 0; }
  g_play_on = false;
}
void sl_audio_stop_all(void) {
  sl_audio_lock();
  voice_stop_locked();
  memset(V, 0, sizeof V); memset(CK, 0, sizeof CK); g_metro_bpm = 0; g_rec_on = false; g_boot_pcm = NULL; g_boot_samples = g_boot_pos = 0;
  sl_audio_unlock();
}

bool sl_boot_voice_start(void) {
  uint32_t bytes = 0;
  const uint8_t *pcm = fc_res("snd/boot-welcome", &bytes);
  if (!pcm || bytes < 2 || (bytes & 1)) return false;
  sl_audio_lock();
  g_boot_pcm = pcm; g_boot_samples = bytes / 2; g_boot_pos = 0;
  sl_audio_unlock();
  return true;
}
void sl_boot_voice_stop(void) { sl_audio_lock(); g_boot_pcm = NULL; g_boot_samples = g_boot_pos = 0; sl_audio_unlock(); }
bool sl_boot_voice_playing(void) { return g_boot_pcm && g_boot_pos < g_boot_samples; }

/* ---------- 复读 ---------- */
void sl_rec_begin(const int16_t *pre, int n) {
  sl_audio_lock();
  voice_stop_locked(); nv_rec_start(&g_rec); g_rec16 = 0;
  if (pre && n > 0) { nv_rec_push(&g_rec, pre, n); g_rec16 = (uint32_t)n; }
  g_rec_on = true;
  sl_audio_unlock();
}
void sl_rec_push(const int16_t *x, int n) {
  if (!g_rec_on) return;
  int k = nv_rec_push(&g_rec, x, n); g_rec16 += (uint32_t)k;
}
int sl_rec_end(void) { sl_audio_lock(); g_rec_on = false; int n = g_rec.n8; sl_audio_unlock(); return n; }
bool sl_rec_on(void) { return g_rec_on; }
uint32_t sl_rec_samples16(void) { return g_rec16; }
int sl_rec_bytes(void) { return nv_rec_bytes(&g_rec); }
void sl_voice_play(int voice) {
  sl_audio_lock();
  voice_stop_locked(); nv_play_start(&g_pl, &g_rec, voice); g_voice_last = 0; g_play_on = true;
  sl_audio_unlock();
}
bool sl_voice_playing(void) { return g_play_on; }
int sl_voice_level(void) { return g_level; }

/* ---------- 混音 ---------- */
void sl_audio_fill(int16_t *out, int n) {
  static int32_t acc[320];
  if (n > 320) n = 320;
  memset(acc, 0, sizeof(int32_t) * (size_t)n);
  sl_audio_lock();
  if (g_play_on) {
    int16_t tmp[320];
    int w = nv_play_fill(&g_pl, tmp, n);
    for (int i = 0; i < w; i++) acc[i] += tmp[i];
    g_voice_last = w ? tmp[w - 1] : 0;
    if (w < n) g_play_on = false;
  }
  for (int i = 0; i < n && g_voice_tail_pos < VOICE_STOP_FADE; i++, g_voice_tail_pos++)
    acc[i] += (int32_t)((int64_t)g_voice_tail * (VOICE_STOP_FADE - 1 - g_voice_tail_pos) / VOICE_STOP_FADE);
  for (int k = 0; k < NV; k++) {
    ks_t *v = &V[k];
    if (v->left <= 0) continue;
    for (int i = 0; i < n && v->left > 0; i++) {
      if (v->wait > 0) { v->wait--; continue; }
      int q = v->p + 1 == v->n ? 0 : v->p + 1;
      int32_t s = v->b[v->p];
      v->b[v->p] = (int16_t)(((int32_t)s + v->b[q]) * 16334 >> 15);   /* ×0.4985：两点平均再略衰减 */
      v->p = (uint16_t)q;
      int32_t a = v->amp;
      if (v->left < v->fade) a = (int32_t)((int64_t)a * v->left / v->fade);
      acc[i] += (int32_t)((int64_t)s * a >> 15) * 22 / 100;   /* 每根弦 0.22，同原型 */
      v->left--;
    }
  }
  /* 节拍器 */
  if (g_metro_bpm > 0) {
    uint32_t step = (uint32_t)(SL_SR * 60 / g_metro_bpm);
    while ((int32_t)(g_metro_next - (g_time + (uint32_t)n)) < 0) {
      for (int i = 0; i < 4; i++) if (!CK[i].on) { CK[i].on = 1; CK[i].accent = g_metro_count % (uint32_t)g_metro_beats == 0; CK[i].at = g_metro_next; CK[i].pos = 0; CK[i].len = 25 * 16; break; }
      g_metro_count++; g_metro_next += step;
    }
  }
  for (int c = 0; c < 4; c++) {
    if (!CK[c].on) continue;
    uint32_t inc = (uint32_t)((CK[c].accent ? 1760ULL : 1320ULL) * 65536 / SL_SR);
    for (int i = 0; i < n; i++) {
      if ((int32_t)(g_time + (uint32_t)i - CK[c].at) < 0) continue;
      if (CK[c].pos >= CK[c].len) { CK[c].on = 0; break; }
      uint32_t ph = (uint32_t)CK[c].pos * inc;
      int32_t tri = (int32_t)((ph >> 1) & 0x7FFF); tri = (ph & 0x8000) ? 32767 - tri * 2 + 1 : tri * 2 - 32767;   /* 三角波近似正弦 */
      int32_t env = 32768 - CK[c].pos * 32768 / CK[c].len;
      acc[i] += (int32_t)((int64_t)tri * env >> 15) / 2;
      CK[c].pos++;
    }
  }
  sl_sampler_mix(acc, n);
  /* 开机语音直接读Flash；其他音效在语音期间降至1/4，给小喇叭保留余量。 */
  if (g_boot_pcm) {
    for (int i = 0; i < n && g_boot_pos < g_boot_samples; i++, g_boot_pos++) {
      const uint8_t *p = g_boot_pcm + g_boot_pos * 2;
      acc[i] = acc[i] / 4 + (int16_t)(p[0] | ((uint16_t)p[1] << 8));
    }
    if (g_boot_pos >= g_boot_samples) g_boot_pcm = NULL;
  }
  sl_audio_unlock();
  int64_t ss = 0; bool any = false;
  for (int i = 0; i < n; i++) {
    int32_t v = acc[i] < -32768 ? -32768 : acc[i] > 32767 ? 32767 : acc[i];
    out[i] = (int16_t)v; ss += (int64_t)v * v; any |= v != 0;
  }
  /* 电平：RMS 映射到 0..255 */
  int32_t rms = 0; { uint64_t m = (uint64_t)(ss / n); uint32_t r = 0, bit = 1u << 30; while (bit > m) bit >>= 2; while (bit) { if (m >= r + bit) { m -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; } rms = (int32_t)r; }
  g_level = rms > 8000 ? 255 : rms * 255 / 8000;
  g_time += (uint32_t)n;
  if (any) g_busy_until = g_time + 10 * 16;   /* 这一帧有声音：麦克风分析跳过这一帧再多 10 ms */
}
