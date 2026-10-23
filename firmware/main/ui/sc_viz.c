/* 看 · 声音可视化：频谱仪、声纹瀑布、示波器、和弦环、分贝计（原型 SPEC / WATER / SCOPE / CHROMA / DBM） */
#include "sl_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void frozen_tag(bool on) { if (on) T(&F_SANS16, "冻结", 8, 40, C_GOLD, AL_L); }

/* ================= 频谱仪 ================= */
static int sp_m; static bool sp_frozen; static uint8_t sp_fb[32], sp_fp[32];
static const char *SP_MODES[] = {"LED 柱", "镜像", "NABO 环"};
typedef struct { const uint8_t *b, *p; } led_ctx_t;
static led_ctx_t g_led;
#define LED_BASE 296
#define LED_MH 216   /* 顶部留 40px，让最高柱上的 NABO 也完整可见 */
static void led_row(int y, uint16_t *row, int x0, int x1, const void *ctx) {
  const led_ctx_t *c = (const led_ctx_t *)ctx;
  int yy = LED_BASE - y; if (yy <= 0) return;
  int seg = (yy - 1) / 5, inseg = (yy - 1) % 5;
  int fr = seg * 5 * 1000 / LED_MH;
  uint16_t col = fr > 820 ? C_GOLD : fr > 580 ? C_MAG : fr > 320 ? C_PURPLE : C_CYAN;
  for (int i = 0; i < 32; i++) {
    int bx = i * 15 / 2 + 1; if (bx + 6 <= x0 || bx >= x1) continue;
    int segs = c->b[i] * LED_MH / 255 / 5, py = c->p[i] * LED_MH / 255;
    uint16_t pc = 0; bool on = false;
    if (seg < segs && inseg != 4) { on = true; pc = col; }
    if (yy > py && yy <= py + 2) { on = true; pc = C_WHITE; }
    if (!on) continue;
    for (int x = bx; x < bx + 6; x++) if (x >= x0 && x < x1) row[x] = pc;
  }
}
static void sp_up(void) { sl_cycle_sens(); }
static void sp_down(void) { sp_m = (sp_m + 1) % 3; }
static void sp_ok(void) { sp_frozen = !sp_frozen; if (sp_frozen) { memcpy(sp_fb, MIC.bands, 32); memcpy(sp_fp, MIC.peaks, 32); } toast(sp_frozen ? "已冻结" : "继续"); }
static void sp_draw(void) {
  header("频谱仪", SP_MODES[sp_m]);
  const uint8_t *B = sp_frozen ? sp_fb : MIC.bands, *P = sp_frozen ? sp_fp : MIC.peaks;
  if (sp_m == 0) {
    g_led.b = B; g_led.p = P; fc_cb(0, LED_BASE - LED_MH - 4, 240, LED_MH + 4, led_row, &g_led, fnv32(B, 32) ^ fnv32(P, 32) * 31);
    int peak = 0; for (int i = 1; i < 32; i++) if (P[i] > P[peak]) peak = i;
    /* 引擎把站姿带到最高柱；低频脉冲只改变位置，不改素材朝向。 */
    int column_x = peak * 15 / 2 + 4, foot_y = LED_BASE - P[peak] * LED_MH / 255;
    int x = fx_clamp(column_x, 28, 212), y = foot_y - 20;  /* S-IDLE/56 固定像素脚线为第47行 */
    if (x != column_x) fc_line(column_x, foot_y, x, foot_y, 1, C_GOLD);
    nabo("S-IDLE", 56, x, y, (int)VT);
  }
  else if (sp_m == 1) {
    int mid = 160;
    for (int i = 0; i < 32; i++) {
      int x = i * 15 / 2 + 1, h = B[i] * 128 / 255; if (h < 1) h = 1;
      fc_rect(x, mid - h, 6, h, BARC[i], 255);
      fc_rect(x, mid + 3, 6, h * 3 / 4, mix565(BARC[i], C_BG, 600), 255);
    }
    fc_rect(0, mid, 240, 1, C_WHITE, 255);
    int bass = (B[0] + B[1] + B[2] + B[3]) / 4;
    int bob = SET.calm ? 0 : fx_sin((int)(VT * 1024 / 1500)) * bass * 6 / 16384 / 255;
    nabo("S-FLOAT", 76, 120, mid + bob, (int)VT);
  } else {
    int bass = (B[0] + B[1] + B[2] + B[3]) / 4, cx = 120, cy = 166, r0 = 50 + bass * 8 / 255;
    for (int j = 0; j < 64; j++) {
      int b = j < 32 ? j : 63 - j, a = -256 + (2 * j + 1) * 1024 / 128, r1 = r0 + 5 + B[b] * 62 / 255;
      fc_line(cx + (fx_cos(a) * r0 >> 14), cy + (fx_sin(a) * r0 >> 14), cx + (fx_cos(a) * r1 >> 14), cy + (fx_sin(a) * r1 >> 14), 4, BARC[b]);
    }
    int k = 1000 + bass * 140 / 255;
    nabo_sq("S-DJ", 76, cx, cy, (int)VT, k, k, 0);
  }
  frozen_tag(sp_frozen);
  static const hint_t H[] = {{"▲", "灵敏度"}, {"▼", "样式"}, {"OK", "冻结"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_SPEC = {.id = "spec", .name = "频谱仪", .en = "SPECTRUM", .ic = "▮", .mic = 0, .up = sp_up, .down = sp_down, .ok = sp_ok, .draw = sp_draw};

/* ================= 声纹瀑布 ================= */
static int wf_m;
static const char *WF_MODES[] = {"霓虹", "矩阵", "热成像"};
static uint16_t WF_LUT[3][32];
static void wf_lut(void) {
  static bool done; if (done) return; done = true;
  static const uint8_t S0[6][3] = {{0x0a, 0x07, 0x16}, {0x2b, 0x1a, 0x6b}, {0x8a, 0x2b, 0xe2}, {0xff, 0x3d, 0xa6}, {0xff, 0xc9, 0x4a}, {0xff, 0xff, 0xff}};
  static const uint8_t S1[5][3] = {{0, 0, 0}, {0x00, 0x3b, 0x1a}, {0x00, 0xa0, 0x40}, {0x5c, 0xff, 0x8a}, {0xe0, 0xff, 0xe8}};
  static const uint8_t S2[5][3] = {{0, 0, 0x20}, {0x3d, 0x1a, 0xa0}, {0xff, 0x2d, 0x55}, {0xff, 0x9a, 0x3d}, {0xff, 0xf0, 0x4a}};
  const uint8_t (*st[3])[3] = {S0, S1, S2}; int ns[3] = {6, 5, 5};
  for (int m = 0; m < 3; m++)
    for (int i = 0; i < 32; i++) {
      int p = i * (ns[m] - 1) * 1000 / 31, k = p / 1000; if (k > ns[m] - 2) k = ns[m] - 2;
      int t = p - k * 1000; const uint8_t *a = st[m][k], *b = st[m][k + 1];
      WF_LUT[m][i] = RGB(a[0] + (b[0] - a[0]) * t / 1000, a[1] + (b[1] - a[1]) * t / 1000, a[2] + (b[2] - a[2]) * t / 1000);
    }
}
static void wf_row(int y, uint16_t *row, int x0, int x1, const void *ctx) {
  (void)ctx; int yy = y - 28; if (yy < 0 || yy >= WF_ROWS) return;
  const uint8_t *r = WF_BUF[(WF_HEAD - yy + WF_ROWS) % WF_ROWS]; const uint16_t *L = WF_LUT[wf_m];
  for (int x = x0; x < x1; x++) row[x] = L[r[x >> 1] & 31];
}
static void wf_up(void) { sl_cycle_sens(); }
static void wf_down(void) { wf_m = (wf_m + 1) % 3; }
static void wf_ok(void) { WF_FROZEN = !WF_FROZEN; toast(WF_FROZEN ? "已冻结" : "继续"); }
static void wf_leave(void) { WF_FROZEN = false; }
static void wf_draw(void) {
  wf_lut();
  header("声纹瀑布", WF_MODES[wf_m]);
  fc_cb(0, 28, 240, WF_ROWS, wf_row, NULL, (uint32_t)WF_HEAD * 7u + (uint32_t)wf_m);
  fc_rect(0, 284, 240, 16, C_PANEL, 255);
  static const int FX[] = {25, 70, 104, 138, 172, 206, 240};   /* 240·ln(f/60)/ln(8000/60)，由脚本算好 */
  static const char *FL[] = {"100", "250", "500", "1k", "2k", "4k", "8k"};
  for (int i = 0; i < 7; i++) { fc_rect(FX[i] > 239 ? 239 : FX[i], 284, 1, 4, C_DIM, 255); T(&F_MONO14, FL[i], fx_clamp(FX[i], 12, 226), 292, C_DIM, AL_C); }
  int loud = fx_clamp((MIC.db_s_x100 + 3000) / 15, 0, 200);   /* 声音大时伞被“冲”得往下压 */
  nabo_sq("S-UMBRELLA", 56, 26, 252, (int)VT, 1000 + loud / 2, 1000 - loud, 0);
  frozen_tag(WF_FROZEN);
  static const hint_t H[] = {{"▲", "灵敏度"}, {"▼", "配色"}, {"OK", "冻结"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_WATER = {.id = "water", .name = "声纹瀑布", .en = "WATERFALL", .ic = "≋", .mic = NM_F_WF, .up = wf_up, .down = wf_down, .ok = wf_ok, .leave = wf_leave, .draw = wf_draw};

/* ================= 示波器 ================= */
static int sc_m, sc_z = 1, sc_g = 4 * 256; static bool sc_frozen;
static int16_t sc_buf[1024];
static trace_t sc_tr[4];   /* 0 = 当前，1..3 = 余辉 */
static const char *SC_MODES[] = {"波形", "圆形", "李萨如"};
static void sc_up(void) { sc_z = sc_z == 4 ? 1 : sc_z * 2; char b[24]; snprintf(b, sizeof b, "时间轴 ×%d", sc_z); toast(b); }
static void sc_down(void) { sc_m = (sc_m + 1) % 3; memset(sc_tr, 0, sizeof sc_tr); }
static void sc_ok(void) { sc_frozen = !sc_frozen; toast(sc_frozen ? "已冻结" : "继续"); }
static void sc_grid(int y, uint16_t *row, int x0, int x1, const void *ctx) {
  (void)ctx;
  if ((y - 40) % 31 == 0) { for (int x = (x0 + 5) / 6 * 6; x < x1; x += 6) row[x] = C_LINE; }
  else if ((y - 40) % 6 == 0) { for (int x = (x0 + 29) / 30 * 30; x < x1; x += 30) row[x] = C_LINE; }
}
static void sc_draw(void) {
  static const uint16_t COL[3] = {C_CYAN, C_GREEN, C_MAG};
  header("示波器", SC_MODES[sc_m]);
  if (!sc_frozen) sl_mic_last(sc_buf, 1024);
  int pk = 1; for (int i = 0; i < 1024; i++) { int a = sc_buf[i] < 0 ? -sc_buf[i] : sc_buf[i]; if (a > pk) pk = a; }
  int target = (int)(29491LL * 256 / (pk > 491 ? pk : 491));   /* 0.9 / max(峰值, 0.015)，×256 */
  sc_g += (target - sc_g) * 15 / 100;
  fc_cb(0, 40, 240, 251, sc_grid, NULL, 1);
  uint16_t col = COL[sc_m];
#define V(i) fx_clamp((int)((int64_t)sc_buf[(i) & 1023] * sc_g >> 8), -32767, 32767)
  if (sc_m == 0) {
    int s0 = 1; for (int i = 1; i < 500; i++) if (sc_buf[i - 1] < 0 && sc_buf[i] >= 0) { s0 = i; break; }
    if (!sc_frozen) { sc_tr[3] = sc_tr[2]; sc_tr[2] = sc_tr[1]; sc_tr[1] = sc_tr[0]; }
    trace_t *t = &sc_tr[0]; t->x0 = 0; t->n = 240; t->th = 1;
    for (int k = 0; k < 240; k++) { t->y[k] = (int16_t)(164 - V(s0 + k * 2 / sc_z) * 118 / 32768); t->c[k] = col; }
    for (int i = 3; i >= 1; i--) if (sc_tr[i].n) { trace_t *o = &sc_tr[i]; o->th = 0; uint16_t c = mix565(col, C_BG, 450 + i * 130); for (int k = 0; k < 240; k++) o->c[k] = c; trace_draw(o, 40, 251); }
    trace_draw(t, 40, 251);
  } else if (sc_m == 1) {
    int px = 0, py = 0;
    for (int k = 0; k <= 72; k++) {
      int a = k * 1024 / 72 - 256, r = 72 + V(k * 10) * 40 / 32768, x = 120 + (fx_cos(a) * r >> 14), y = 166 + (fx_sin(a) * r >> 14);
      if (k) fc_line(px, py, x, y, 2, col);
      px = x; py = y;
    }
    nabo("S-HEADPHONE", 76, 120, 166, (int)VT);
  } else {
    int px = 0, py = 0;
    for (int k = 0; k < 150; k++) {
      int x = 120 + V(k * 4) * 104 / 32768, y = 166 - V(k * 4 + 9) * 104 / 32768;
      if (k) fc_line(px, py, x, y, 1, col);
      px = x; py = y;
    }
  }
#undef V
  TF(&F_MONO14, 232, 38, C_DIM, AL_R, "x%d.%d", sc_g / 256, sc_g % 256 * 10 / 256);
  frozen_tag(sc_frozen);
  static const hint_t H[] = {{"▲", "时间轴"}, {"▼", "显示"}, {"OK", "冻结"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_SCOPE = {.id = "scope", .name = "示波器", .en = "SCOPE", .ic = "∿", .mic = 0, .up = sc_up, .down = sc_down, .ok = sc_ok, .draw = sc_draw};

/* ================= 和弦环 ================= */
static int ch_m, ch_last = -1; static uint32_t ch_change;
static const uint8_t FIFTHS[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};
static void ch_down(void) { ch_m ^= 1; }
static void ch_draw(void) {
  header("和弦环", ch_m ? "半音" : "五度圈");
  int cx = 120, cy = 150;
  for (int i = 0; i < 12; i++) {
    int pc = ch_m ? i : FIFTHS[i], v = MIC.chroma[pc];
    int a0 = -256 + (2 * i - 1) * 1024 / 24 + 5, a1 = a0 + 1024 / 12 - 10, am = (a0 + a1) / 2, r0 = 54, r1 = 58 + v * 46 / 255;
    int16_t P[12]; int n = 0;
    for (int k = 0; k <= 3; k++) { int a = a0 + (a1 - a0) * k / 3; P[n++] = (int16_t)((cx * 16) + (fx_cos(a) * r1 >> 10)); P[n++] = (int16_t)((cy * 16) + (fx_sin(a) * r1 >> 10)); }
    P[n++] = (int16_t)((cx * 16) + (fx_cos(a1) * r0 >> 10)); P[n++] = (int16_t)((cy * 16) + (fx_sin(a1) * r0 >> 10));
    P[n++] = (int16_t)((cx * 16) + (fx_cos(a0) * r0 >> 10)); P[n++] = (int16_t)((cy * 16) + (fx_sin(a0) * r0 >> 10));
    fc_poly(P, 6, mix565(PC12[pc], C_BG, 1000 - (250 + v * 750 / 255)), 255);
    T(&F_SANS14, NN[pc], cx + (fx_cos(am) * 112 >> 14), cy + (fx_sin(am) * 112 >> 14), v > 150 ? C_WHITE : C_DIM, AL_C);
  }
  if (MIC.chord >= 0) {
    char nm[8]; snprintf(nm, sizeof nm, "%s%s", NN[MIC.chord / 2], (MIC.chord & 1) ? "m" : "");
    T(&F_MONO22, nm, cx, cy - 6, C_WHITE, AL_C);
    T(&F_SANS14, (MIC.chord & 1) ? "小三和弦" : "大三和弦", cx, cy + 20, C_CYAN, AL_C);
  } else T(&F_MONO22, "...", cx, cy - 6, C_DARK, AL_C);
  if (MIC.chord >= 0 && MIC.chord != ch_last) ch_change = VT;
  ch_last = MIC.chord;
  /* 前三个音 */
  int top[3] = {-1, -1, -1};
  for (int k = 0; k < 3; k++) { int b = -1; for (int i = 0; i < 12; i++) { if (i == top[0] || i == top[1]) continue; if (b < 0 || MIC.chroma[i] > MIC.chroma[b]) b = i; } if (MIC.chroma[b] > 102) top[k] = b; }
  char s[32] = ""; for (int k = 0; k < 3; k++) if (top[k] >= 0) { if (s[0]) strcat(s, " · "); strcat(s, NN[top[k]]); }
  T(s[0] ? &F_MONO14 : &F_SANS14, s[0] ? s : "弹个和弦试试", 100, 284, C_DIM, AL_C);
  int dt = (int)(VT - ch_change);
  if (ch_change && dt < nabo_ms("S-GUITAR-STRUM")) nabo_once("S-GUITAR-STRUM", 56, 206, 262, dt); else nabo("S-GUITAR-IDLE", 56, 206, 262, (int)VT);
  static const hint_t H[] = {{"▲", "灵敏度"}, {"▼", "排列"}};
  hintK(H, 2, true);
}
const sl_scene_t SC_CHROMA = {.id = "chroma", .name = "和弦环", .en = "CHORD RING", .ic = "✲", .mic = NM_F_CHROMA, .up = sl_cycle_sens, .down = ch_down, .draw = ch_draw};

/* ================= 分贝计 ================= */
static int db_m, db_peak = -20000; static uint32_t db_loud_t, db_shush_t; static bool db_was_alarm;
static trace_t db_tr;
static const char *DB_MODES[] = {"实时", "峰值保持", "安静提醒"};
static void db_up(void) { SET.quiet = SET.quiet >= 80 ? 40 : SET.quiet + 5; sl_settings_save(); char b[32]; snprintf(b, sizeof b, "安静阈值 %d dB", SET.quiet); toast(b); }
static void db_down(void) { db_m = (db_m + 1) % 3; }
static void db_ok(void) { db_peak = -20000; toast("峰值已清零"); }
static void db_enter(void) { db_was_alarm = false; }
static void db_draw(void) {
  int v = MIC.db_s_x100 + SET.cal * 100; if (v > db_peak) db_peak = v;
  int show = db_m == 1 ? db_peak : v; bool alarm = db_m == 2 && v > SET.quiet * 100;
  if (alarm && !db_was_alarm) db_shush_t = VT;
  db_was_alarm = alarm;
  if (alarm) db_loud_t = VT;
  header("分贝计", DB_MODES[db_m]);
  if (db_m == 2 && VT - db_loud_t < 800 && (SET.calm || (VT / 200) % 2)) fc_frame(3, 29, 234, 270, 5, C_RED);
  int cx = 120, cy = 128, R = 80, lit = fx_clamp((show - 3000) * 1000 / 8000, 0, 1000);
  for (int i = 0; i < 40; i++) {
    int f = i * 1000 / 39, a = 384 + f * 768 / 1000, a2 = a + 768 / 40 * 7 / 10, db = 30 + f * 80 / 1000;
    uint16_t c = f <= lit ? (db < 55 ? C_GREEN : db < 75 ? C_GOLD : C_RED) : C_LINE;
    fc_line(cx + (fx_cos(a) * R >> 14), cy + (fx_sin(a) * R >> 14), cx + (fx_cos(a2) * R >> 14), cy + (fx_sin(a2) * R >> 14), 6, c);
  }
  if (db_m == 2) { int a = 384 + fx_clamp((SET.quiet - 30) * 1000 / 80, 0, 1000) * 768 / 1000; fc_line(cx + (fx_cos(a) * (R - 12) >> 14), cy + (fx_sin(a) * (R - 12) >> 14), cx + (fx_cos(a) * (R + 10) >> 14), cy + (fx_sin(a) * (R + 10) >> 14), 2, C_WHITE); }
  if (MIC_LIVE) TF(&F_MONO48, cx, cy - 4, C_WHITE, AL_C, "%d", (show + 50) / 100); else T(&F_MONO48, "--", cx, cy - 4, C_WHITE, AL_C);
  T(&F_SANS14, db_m == 1 ? "dB 峰值" : "dB（估算）", cx, cy + 30, C_DIM, AL_C);
  const char *lv; uint16_t lc;
  if (v < 4500) { lv = "很安静"; lc = C_GREEN; } else if (v < 6000) { lv = "正常"; lc = C_GREEN; } else if (v < 7500) { lv = "有点吵"; lc = C_GOLD; } else if (v < 9000) { lv = "很吵"; lc = C_RED; } else { lv = "太吵了！"; lc = C_RED; }
  if (alarm) { lv = "嘘——小声点"; lc = C_RED; }
  T(&F_DISP18, lv, cx, cy + 68, lc, AL_C);
  /* 10 秒曲线 */
  int gx = 10, gy = 226, gw = 148, gh = 64;
  fc_rect(gx, gy, gw, gh, C_PANEL, 255);
  db_tr.x0 = (int16_t)gx; db_tr.n = (int16_t)gw; db_tr.th = 0;
  for (int i = 0; i < gw; i++) {
    int k = (DB_HEAD + i * DBH_N / gw) % DBH_N, d = DB_HIST[k] + SET.cal * 100;
    db_tr.y[i] = (int16_t)(gy + gh - 1 - fx_clamp((d - 3000) * (gh - 1) / 8000, 0, gh - 1)); db_tr.c[i] = C_CYAN;
  }
  trace_draw(&db_tr, gy, gh);
  if (db_m == 2) { int y = gy + gh - fx_clamp((SET.quiet - 30) * gh / 80, 0, gh); for (int x = gx; x < gx + gw; x += 4) fc_rect(x, y, 2, 1, C_RED, 255); }
  T(&F_SANS14, "10 秒", gx + 4, gy + 9, C_DIM, AL_L);
  const char *id = alarm ? "S-SHUSH" : v > 8500 ? "S-EARS" : v < 4500 ? "S-SLEEP" : "S-IDLE";
  if (alarm) {
    /* 源片结尾收手：越阈值时从抬手开始，持续报警保持中段“嘘”。 */
    int t = (int)(VT - db_shush_t), hold = nabo_ms("S-SHUSH") / 3;
    nabo_once(id, 76, 198, 260, t < hold ? t : hold);
  } else nabo(id, 76, 198, 260, (int)VT);
  char qb[16]; snprintf(qb, sizeof qb, "阈值 %d", SET.quiet);
  hint_t H[] = {{"▲", qb}, {"▼", "模式"}, {db_m == 1 ? "OK" : NULL, "清零"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_DB = {.id = "db", .name = "分贝计", .en = "dB METER", .ic = "◔", .mic = 0, .enter = db_enter, .up = db_up, .down = db_down, .ok = db_ok, .draw = db_draw};
