/* 练 · 音乐工具：调音器、节拍检测、音准曲线（原型 TUNER / BEAT / PITCH） */
#include "sl_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 调音器 ================= */
int tu_m;                                     /* 吉他工具箱会把它设成吉他模式 */
const sl_scene_t *tu_ret;                     /* 从吉他工具箱进来时返回那里 */
static int tu_cs, tu_ph, tu_tgt = -1, tu_hz10; static uint32_t tu_seen, tu_ok_since; static char tu_lab[4], tu_oct[12];
static const char *TU_MODES[] = {"半音", "吉他", "尤克里里"};
static const int8_t TU_STR[3][6] = {{0}, {40, 45, 50, 55, 59, 64}, {67, 60, 64, 69}};
static const char *TU_SN[3][6] = {{0}, {"E", "A", "D", "G", "B", "E"}, {"G", "C", "E", "A"}};
static const int TU_N[3] = {0, 6, 4};
static void tu_up(void) {
  static const int16_t L[] = {440, 442, 443, 432, 415};
  int i = 0; while (i < 5 && L[i] != SET.a4) i++;
  SET.a4 = L[(i + 1) % 5]; sl_settings_save();
  char b[24]; snprintf(b, sizeof b, "A4 = %d Hz", SET.a4); toast(b);
}
static void tu_down(void) { tu_m = (tu_m + 1) % 3; }
static bool tu_back(void) { if (tu_ret) { const sl_scene_t *r = tu_ret; tu_ret = NULL; sl_ui_go(r); return true; } return false; }
static void tu_draw(void) {
  header("调音器", TU_MODES[tu_m]);
  if (MIC.f0_x10 && MIC.f0_conf_x1000 > 800) {
    int m100 = hz10_to_midi100(MIC.f0_x10), ref;
    if (tu_m == 0) { ref = (m100 + 50) / 100; snprintf(tu_lab, sizeof tu_lab, "%s", NN[ref % 12]); snprintf(tu_oct, sizeof tu_oct, "%d", ref / 12 - 1); tu_tgt = -1; }
    else {
      int bi = 0; for (int i = 1; i < TU_N[tu_m]; i++) if (abs(m100 - TU_STR[tu_m][i] * 100) < abs(m100 - TU_STR[tu_m][bi] * 100)) bi = i;
      ref = TU_STR[tu_m][bi]; snprintf(tu_lab, sizeof tu_lab, "%s", TU_SN[tu_m][bi]); snprintf(tu_oct, sizeof tu_oct, "%d", ref / 12 - 1); tu_tgt = bi;
    }
    int cents = m100 - ref * 100;
    tu_cs = VT - tu_seen > 400 ? cents * 10 : tu_cs + (cents * 10 - tu_cs) * 35 / 100;   /* ×10 */
    tu_seen = VT; tu_hz10 = MIC.f0_x10;
  }
  bool live = VT - tu_seen < 600; int cs = fx_clamp(tu_cs / 10, -50, 50); bool in = live && tu_cs > -50 && tu_cs < 50;
  if (in) { if (!tu_ok_since) tu_ok_since = VT; } else tu_ok_since = 0;
  int cx = 120, cy = 196, R = 100;
  for (int k = -50; k <= 50; k += 5) {
    int a = -256 + k * 1024 / 300; bool big = k % 10 == 0; int r1 = R - (big ? 12 : 6);
    fc_line(cx + (fx_cos(a) * R >> 14), cy + (fx_sin(a) * R >> 14), cx + (fx_cos(a) * r1 >> 14), cy + (fx_sin(a) * r1 >> 14), big ? 2 : 1, abs(k) <= 5 ? C_GREEN : big ? C_DIM : C_DARK);
  }
  T(&F_SANS16, "♭", cx - R + 8, cy - 62, C_DIM, AL_C); T(&F_SANS16, "♯", cx + R - 8, cy - 62, C_DIM, AL_C);
  if (live) {
    int a = -256 + cs * 1024 / 300; uint16_t col = in ? C_GREEN : abs(cs) < 15 ? C_GOLD : C_MAG;
    fc_line(cx + (fx_cos(a) * 30 >> 14), cy + (fx_sin(a) * 30 >> 14), cx + (fx_cos(a) * (R - 2) >> 14), cy + (fx_sin(a) * (R - 2) >> 14), 3, col);
    T(&F_DISP56, tu_lab, cx - 6, cy - 26, col, AL_C);
    T(&F_MONO22, tu_oct, cx + 24, cy - 12, col, AL_L);
    TF(&F_MONO22, cx, cy + 18, C_WHITE, AL_C, "%d.%dHz %+dc", tu_hz10 / 10, tu_hz10 % 10, (tu_cs + (tu_cs >= 0 ? 5 : -5)) / 10);
  } else { T(&F_DISP56, "—", cx, cy - 26, C_DARK, AL_C); T(&F_SANS14, "弹一个音或哼一声", cx, cy + 14, C_DIM, AL_C); }
  /* 频闪条：偏低往左流、偏高往右流、准了就停；减弱闪烁时改成文字箭头 */
  if (live && !SET.calm) tu_ph += cs * 6 / 10;
  for (int i = 0; i < 20; i++) { int x = ((i * 12 + tu_ph / 10) % 240 + 240) % 240; fc_rect(x, 232, 6, 8, in ? C_GREEN : live ? C_PURPLE : C_LINE, 255); }
  if (SET.calm && live && !in) T(&F_SANS14, cs < 0 ? "◀ 偏低，往上调" : "偏高，往下调 ▶", 120, 252, C_GOLD, AL_C);
  if (tu_m) {
    int n = TU_N[tu_m];
    for (int i = 0; i < n; i++) {
      int x = 150 + i * (84 / n) + (84 / n) / 2; bool on = i == tu_tgt && live;
      rr(x - 8, 262, 16, 20, 4, on ? (in ? C_GREEN : C_CYAN) : C_PANEL);
      T(&F_MONO14, TU_SN[tu_m][i], x, 272, on ? C_BG : C_DIM, AL_C);
    }
  }
  if (tu_ok_since && VT - tu_ok_since > 500) { nabo_once("S-OK-SIGN", 76, 46, 258, (int)(VT - tu_ok_since - 500)); T(&F_DISP18, "准了！", tu_m ? 128 : 150, 250, C_GREEN, AL_L); }
  else nabo(tu_m ? "S-GUITAR-TUNE" : "S-LISTEN", 76, 46, 258, (int)VT);
  char a4[16]; snprintf(a4, sizeof a4, "A4 %d", SET.a4);
  hint_t H[] = {{"▲", a4}, {"▼", "模式"}};
  hintK(H, 2, true);
}
const sl_scene_t SC_TUNER = {.id = "tuner", .name = "调音器", .en = "TUNER", .ic = "♮", .mic = NM_F_PITCH, .up = tu_up, .down = tu_down, .back = tu_back, .draw = tu_draw};

/* ================= 节拍检测 ================= */
static int bt_m, bt_dots; static uint32_t bt_rings[8], bt_bob, bt_mc;
static const char *BT_MODES[] = {"自动检测", "拍手测速"};
static void bt_hit(void) { for (int i = 7; i > 0; i--) bt_rings[i] = bt_rings[i - 1]; bt_rings[0] = VT; bt_dots = (bt_dots + 1) % 8; bt_bob = VT; }
static void bt_onset(uint32_t t) { (void)t; if (!sl_metro_bpm()) bt_hit(); }
static int bt_bpm10(void) {
  if (bt_m == 0) return MIC.bpm_x10;
  int n = ONSET_CNT < 16 ? ONSET_CNT : 16, cnt = 0; uint32_t sum = 0;
  for (int k = 1; k < n && cnt < 6; k++) {
    uint32_t a = ONSET_T[(ONSET_CNT - k) % 16], b = ONSET_T[(ONSET_CNT - k - 1) % 16], d = a - b;
    if (d > 4000 && d < 32000) { sum += d; cnt++; } else break;     /* 0.25–2 秒 */
  }
  return cnt >= 3 ? (int)(16000ULL * 600 * cnt / sum) : 0;
}
static void bt_enter(void) { g_onset_hook = bt_onset; bt_mc = sl_metro_count(); }
static void bt_leave(void) { sl_metro(0, 4); }
static void bt_ok(void) {
  if (sl_metro_bpm()) { sl_metro(0, 4); toast("节拍器关"); return; }
  int b = (bt_bpm10() + 5) / 10; if (b < 40 || b > 220) b = 100;
  sl_metro(b, 4); bt_mc = 0; char s[32]; snprintf(s, sizeof s, "节拍器 %d BPM", b); toast(s);
}
static void bt_up(void) {
  if (!sl_metro_bpm()) { sl_cycle_sens(); return; }
  int b = sl_metro_bpm() >= 220 ? 40 : sl_metro_bpm() + 5; sl_metro(b, 4); bt_mc = 0;
  char s[32]; snprintf(s, sizeof s, "节拍器 %d BPM", b); toast(s);
}
static void bt_down(void) { bt_m ^= 1; }
static void bt_update(int dt) { (void)dt; uint32_t c = sl_metro_count(); while (bt_mc < c) { bt_mc++; bt_hit(); } }
static void bt_draw(void) {
  char md[24]; if (sl_metro_bpm()) snprintf(md, sizeof md, "节拍器 %d", sl_metro_bpm()); else snprintf(md, sizeof md, "%s", BT_MODES[bt_m]);
  header("节拍检测", md);
  int cx = 120, cy = 136;
  if (!SET.calm) for (int i = 0; i < 8; i++) { int d = (int)(VT - bt_rings[i]); if (!bt_rings[i] || d >= 700) continue; fc_ring(cx, cy, 40 + d * 80 / 700, 3, mix565(C_MAG, C_BG, d * 1000 / 700)); }
  int b10 = sl_metro_bpm() ? sl_metro_bpm() * 10 : bt_bpm10();
  if (b10) TF(&F_MONO48, cx, cy - 4, C_WHITE, AL_C, "%d", (b10 + 5) / 10); else T(&F_MONO48, "---", cx, cy - 4, C_WHITE, AL_C);
  T(&F_MONO14, "BPM", cx, cy + 30, C_CYAN, AL_C);
  if (bt_m == 0) { int w = fx_clamp(MIC.bpm_conf_x1000 * 3, 0, 1000); fc_rect(70, cy + 46, 100, 4, C_PANEL, 255); fc_rect(70, cy + 46, w / 10, 4, C_CYAN, 255); T(&F_SANS14, "把握度", cx, cy + 60, C_DIM, AL_C); }
  else T(&F_SANS14, ONSET_CNT < 4 ? "对着工牌拍几下手" : "继续拍，越多越准", cx, cy + 56, C_DIM, AL_C);
  for (int i = 0; i < 8; i++) { bool on = i == (bt_dots + 7) % 8; fc_disc(36 + i * 24, 222, on ? 6 : 4, on ? (i % 4 == 0 ? C_GOLD : C_MAG) : C_LINE, 255); }
  int k = fx_clamp(1000 - (int)(VT - bt_bob) * 1000 / 220, 0, 1000);
  nabo_sq("S-DRUM", 76, 120, 262, bt_bob ? (int)(VT - bt_bob) : 100000, 1000 + k / 10, 1000 - k * 12 / 100, 0);
  hint_t H[] = {{"▲", sl_metro_bpm() ? "速度" : "灵敏度"}, {"▼", "模式"}, {"OK", "节拍"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_BEAT = {.id = "beat", .name = "节拍检测", .en = "BPM", .ic = "♩", .mic = NM_F_BPM, .enter = bt_enter, .leave = bt_leave, .up = bt_up, .down = bt_down, .ok = bt_ok, .update = bt_update, .draw = bt_draw};

/* ================= 音准曲线 ================= */
static int pt_m, pt_span = 20, pt_center = 6000; static bool pt_frozen; static int16_t pt_snap[PH_N]; static uint16_t pt_head;
static trace_t pt_tr;
static const char *PT_MODES[] = {"音名", "简谱", "唱名"};
static const char *JIANPU[12] = {"1", "#1", "2", "#2", "3", "4", "#4", "5", "#5", "6", "#6", "7"};
static const char *SOLF[12] = {"do", "", "re", "", "mi", "fa", "", "sol", "", "la", "", "si"};
static void pt_lab(int n, char *o) { int pc = ((n % 12) + 12) % 12; if (pt_m == 0) note_name(n, o); else strcpy(o, pt_m == 1 ? JIANPU[pc] : SOLF[pc]); }
static void pt_up(void) { static const int L[] = {20, 12, 32}; int i = 0; while (L[i] != pt_span) i++; pt_span = L[(i + 1) % 3]; char b[24]; snprintf(b, sizeof b, "显示 %d 个半音", pt_span); toast(b); }
static void pt_down(void) { pt_m = (pt_m + 1) % 3; }
static void pt_ok(void) { pt_frozen = !pt_frozen; if (pt_frozen) { memcpy(pt_snap, PITCH_HIST, sizeof pt_snap); pt_head = PITCH_HEAD; } toast(pt_frozen ? "已冻结" : "继续"); }
static void pt_draw(void) {
  header("音准曲线", PT_MODES[pt_m]);
  const int16_t *ph = pt_frozen ? pt_snap : PITCH_HIST; int p0 = pt_frozen ? pt_head : PITCH_HEAD;
  if (!pt_frozen) {   /* 纵轴跟着最近 40 个音的中位数走 */
    int16_t r[40]; int n = 0;
    for (int i = 0; i < 40; i++) { int v = ph[(p0 - 1 - i + 2 * PH_N) % PH_N]; if (v) r[n++] = (int16_t)v; }
    if (n > 5) { for (int i = 1; i < n; i++) { int16_t t = r[i]; int j = i; while (j > 0 && r[j - 1] > t) { r[j] = r[j - 1]; j--; } r[j] = t; } pt_center += (r[n / 2] - pt_center) * 5 / 100; }
  }
  int top = 32, bot = 294, lo = (pt_center + 50) / 100 - pt_span / 2;
#define PY(m100) (bot - ((m100) - lo * 100) * (bot - top) / (pt_span * 100))
  for (int n = lo; n <= lo + pt_span; n++) {
    int pc = ((n % 12) + 12) % 12; bool white = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11; int y = PY(n * 100);
    fc_rect(30, y, 210, 1, pc == 0 ? C_DIM : white ? C_LINE : RGB(0x1A, 0x14, 0x33), 255);
    char lb[8]; pt_lab(n, lb);
    if (white && lb[0] && (pt_span <= 20 || pc == 0 || pc == 7)) T(&F_MONO14, lb, 15, y, pc == 0 ? C_GOLD : C_DIM, AL_C);
  }
  pt_tr.x0 = 30; pt_tr.n = 210; pt_tr.th = 1;
  int lastx = -1, lasty = 0;
  for (int i = 0; i < 210; i++) {
    int v = ph[(p0 + i * PH_N / 210) % PH_N];
    if (!v) { pt_tr.y[i] = -1; continue; }
    int y = PY(v); if (y < top || y > bot) { pt_tr.y[i] = -1; continue; }
    int dev = v % 100; if (dev > 50) dev = 100 - dev;
    pt_tr.y[i] = (int16_t)y; pt_tr.c[i] = dev < 15 ? C_GREEN : dev < 30 ? C_CYAN : C_MAG; lastx = 30 + i; lasty = y;
  }
  trace_draw(&pt_tr, top - 2, bot - top + 4);
  if (lastx >= 0) { const fc_img_t *hd = prop("head-24"); int hx = fx_clamp(lastx, 12, 228), hy = fx_clamp(lasty, top + 12, bot - 12); if (hd) fc_img(hd, 0, hx - 12, hy - 12, 255); else { fc_disc(hx, hy, 6, C_WHITE, 255); fc_disc(hx, hy, 3, C_MAG, 255); } }
  int cur = ph[(p0 - 1 + PH_N) % PH_N];
  if (cur) { char lb[8]; pt_lab((cur + 50) / 100, lb); if (!lb[0]) note_name((cur + 50) / 100, lb); rr(166, 32, 70, 30, 6, C_PANEL); T(&F_MONO22, lb, 201, 47, C_WHITE, AL_C); }
#undef PY
  static const hint_t H[] = {{"▲", "缩放"}, {"▼", "记谱"}, {"OK", "冻结"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_PITCH = {.id = "pitch", .name = "音准曲线", .en = "PITCH TRACE", .ic = "〽", .mic = NM_F_PITCH, .up = pt_up, .down = pt_down, .ok = pt_ok, .draw = pt_draw};
