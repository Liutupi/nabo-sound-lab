/* 宿主机驱动：把固件 UI（含 nabo_mic、nabo_voice、混音）编成共享库，Python 用 ctypes 送麦克风信号、按键、推进时间并取帧缓冲。
 * 每次 host_frame 先按脏条带增量合成到 fb，再强制整屏合成到 fb_full，两者必须逐像素相同。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/main/ui/sl_ui.h"

static uint8_t *g_blob;
static uint16_t fb[FC_W * FC_H], fb_full[FC_W * FC_H];
static int g_strips;
static nm_state_t g_nm;
static int16_t g_ring[4096]; static uint32_t g_wp, g_t;
static int16_t g_pend[NM_HOP]; static int g_np;
static void to_fb(int y0, int rows, const uint16_t *pix, void *ctx) { memcpy((uint16_t *)ctx + y0 * FC_W, pix, (size_t)rows * FC_W * 2); if (ctx == fb) g_strips++; }
void sl_mic_last(int16_t *dst, int n) { for (int i = 0; i < n; i++) dst[i] = g_ring[(g_wp - (uint32_t)n + (uint32_t)i) & 4095]; }
uint32_t sl_mic_samples(void) { return g_wp; }

int host_init(const char *path, unsigned seed) {
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  free(g_blob); g_blob = malloc((size_t)n);
  if (fread(g_blob, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -2; }
  fclose(f);
  if (!sl_ui_init(g_blob, (uint32_t)n, seed)) return -3;
  nm_init(&g_nm, 0); g_wp = g_t = 0; g_np = 0;
  sl_ui_go_id("boot");
  return 0;
}
void host_go(const char *id) { sl_ui_go_id(id); }
void host_key(int k, int down) { sl_ui_key(k, down != 0); }
void host_step(int dt) { sl_ui_step(dt); }
const char *host_scene(void) { return sl_ui_scene()->id; }
/* 送麦克风采样：和设备音频任务同一套流程（先混音输出，在发声就跳过分析） */
int host_feed(const int16_t *x, int n, int16_t *out_or_null) {
  int16_t out[NM_HOP]; int written = 0;
  for (int i = 0; i < n; i++) {
    g_pend[g_np++] = x[i];
    if (g_np < NM_HOP) continue;
    g_np = 0;
    sl_audio_fill(out, NM_HOP);
    if (out_or_null) memcpy(out_or_null + written, out, sizeof out);
    written += NM_HOP;
    for (int k = 0; k < NM_HOP; k++) g_ring[(g_wp++) & 4095] = g_pend[k];
    if (sl_rec_on()) sl_rec_push(g_pend, NM_HOP);
    g_t += NM_HOP;
    if (sl_audio_busy()) { sl_mic_skip(); continue; }
    nm_set_features(&g_nm, sl_ui_mic_features()); nm_set_sens(&g_nm, SET.sens);
    sl_mic_hop(nm_process(&g_nm, g_pend), sl_audio_time());
  }
  return written;
}
int host_frame(int force, int *mismatch, int *ops) {
  g_strips = 0;
  sl_ui_render(to_fb, fb, force != 0);
  fc_gfx_end(to_fb, fb_full, true);
  int bad = 0; for (int i = 0; i < FC_W * FC_H; i++) bad += fb[i] != fb_full[i];
  if (mismatch) *mismatch = bad;
  if (ops) *ops = fc_gfx_ops() | (fc_gfx_overflow() ? 1 << 30 : 0);
  return g_strips;
}
const uint16_t *host_fb(void) { return fb; }
void host_set_calm(int v) { SET.calm = (uint8_t)v; }
