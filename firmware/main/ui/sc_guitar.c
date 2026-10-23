/* 吉他工具箱（原型 GUITAR）：调音器 / 和弦谱 / 听和弦 / 扫弦节奏 / 变调夹 / 指板音阶 / 练耳挑战 */
#include "sl_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t OPEN_G[6] = {40, 45, 50, 55, 59, 64}, OPEN_U[4] = {67, 60, 64, 69};
typedef struct { const char *n, *s; } shape_t;
static const shape_t SH_G[] = {{"C", "x32010"}, {"Cm", "x35543"}, {"C7", "x32310"}, {"Cmaj7", "x32000"}, {"Cadd9", "x32030"}, {"D", "xx0232"}, {"Dm", "xx0231"},
  {"D7", "xx0212"}, {"Dsus4", "xx0233"}, {"E", "022100"}, {"Em", "022000"}, {"E7", "020100"}, {"Em7", "022030"}, {"F", "133211"}, {"Fm", "133111"},
  {"Fmaj7", "xx3210"}, {"G", "320003"}, {"Gm", "355333"}, {"G7", "320001"}, {"A", "x02220"}, {"Am", "x02210"}, {"A7", "x02020"}, {"Am7", "x02010"},
  {"Asus2", "x02200"}, {"B", "x24442"}, {"Bm", "x24432"}, {"B7", "x21202"}};
static const shape_t SH_U[] = {{"C", "0003"}, {"Cm", "0333"}, {"C7", "0001"}, {"Cmaj7", "0002"}, {"Cadd9", "0203"}, {"D", "2220"}, {"Dm", "2210"}, {"D7", "2223"},
  {"Dsus4", "0233"}, {"E", "4442"}, {"Em", "0432"}, {"E7", "1202"}, {"Em7", "0202"}, {"F", "2010"}, {"Fm", "1013"}, {"G", "0232"}, {"Gm", "0231"},
  {"G7", "0212"}, {"A", "2100"}, {"Am", "2000"}, {"A7", "0100"}, {"Am7", "0000"}, {"Asus2", "2200"}, {"B", "4322"}, {"Bm", "4222"}, {"B7", "2322"}};
#define NG ((int)(sizeof SH_G / sizeof SH_G[0]))
#define NU ((int)(sizeof SH_U / sizeof SH_U[0]))
static const char *shape_of(const char *n, bool uke) { const shape_t *T_ = uke ? SH_U : SH_G; int c = uke ? NU : NG; for (int i = 0; i < c; i++) if (!strcmp(T_[i].n, n)) return T_[i].s; return NULL; }
static int parse(const char *s, int8_t *fr) { int n = 0; for (; s[n]; n++) fr[n] = s[n] == 'x' ? -1 : (int8_t)(s[n] - '0'); return n; }
static void play_shape(const char *n, bool uke, int capo, bool down) {
  const char *s = shape_of(n, uke); if (!s) return; int8_t fr[6]; int k = parse(s, fr);
  for (int i = 0; i < k; i++) if (fr[i] >= 0) fr[i] = (int8_t)(fr[i] + capo);
  sl_strum(fr, uke ? OPEN_U : OPEN_G, k, down, uke ? 28 : 22);
}
static int root_of(const char *n, const char **q) { int k = n[1] == '#' ? 2 : 1; for (int i = 0; i < 12; i++) if (!strncmp(NN[i], n, (size_t)k) && (int)strlen(NN[i]) == k) { *q = n + k; return i; } *q = n + 1; return 0; }
static void trans_name(const char *n, int d, char *o) { const char *q; int r = root_of(n, &q); sprintf(o, "%s%s", NN[(r + d + 120) % 12], q); }

/* 和弦图：(x,y) 左上；w×h 网格；amp 弦抖动幅度 */
static void draw_chord(const char *name, bool uke, int x, int y, int w, int h, uint16_t col, int amp, bool names) {
  const char *s = shape_of(name, uke); if (!s) return;
  int8_t fr[6]; int ns = parse(s, fr), maxf = 0, mn = 99; for (int i = 0; i < ns; i++) { if (fr[i] > maxf) maxf = fr[i]; if (fr[i] > 0 && fr[i] < mn) mn = fr[i]; }
  int base = maxf > 4 ? mn : 1, sx = w / (ns - 1), fy = h / 5;
  if (base == 1) fc_rect(x - 1, y - 3, w + 2, 3, C_WHITE, 255); else TF(&F_MONO14, x - 8, y + fy / 2, C_DIM, AL_R, "%dfr", base);
  for (int i = 0; i <= 5; i++) fc_rect(x, y + i * fy, w, 1, C_DARK, 255);
  for (int s_ = 0; s_ < ns; s_++) {
    int dx = fr[s_] < 0 ? 0 : amp * fx_sin((int)(VT * 1024 / 113) + s_ * 277) / 16384;
    fc_line(x + s_ * sx + dx / 2, y, x + s_ * sx + dx, y + h / 2, uke ? 1 : 1 + (5 - s_) / 3, fr[s_] < 0 ? C_LINE : C_DIM);
    fc_line(x + s_ * sx + dx, y + h / 2, x + s_ * sx + dx / 2, y + h, uke ? 1 : 1 + (5 - s_) / 3, fr[s_] < 0 ? C_LINE : C_DIM);
  }
  int ba = -1, bb = -1, bf = 0;
  if (mn < 99) {
    int a = -1, b = -1, cnt = 0; for (int i = 0; i < ns; i++) if (fr[i] == mn) { if (a < 0) a = i; b = i; cnt++; }
    bool ok = ns == 4 ? cnt >= 3 && b - a >= 2 : cnt >= 2 && b - a >= 3;
    for (int i = a; ok && i <= b; i++) if (fr[i] < mn) ok = false;
    if (ok) { ba = a; bb = b; bf = mn; }
  }
  int r = (sx < fy ? sx : fy) * 33 / 100;
#define FYC(f) (y + ((f) - base) * fy + fy / 2)
  if (ba >= 0) rr(x + ba * sx - r, FYC(bf) - r, (bb - ba) * sx + 2 * r, 2 * r, r, col);
  for (int i = 0; i < ns; i++) {
    int cx = x + i * sx;
    if (fr[i] < 0) T(&F_SANS14, "×", cx, y - 11, C_DIM, AL_C);
    else if (fr[i] == 0) fc_ring(cx, y - 11, 4, 1, C_DIM);
    else if (!(ba >= 0 && fr[i] == bf && i >= ba && i <= bb)) fc_disc(cx, FYC(fr[i]), r, col, 255);
    if (names && fr[i] >= 0) T(&F_MONO14, NN[((uke ? OPEN_U : OPEN_G)[i] + fr[i]) % 12], cx, y + h + 11, C_DIM, AL_C);
  }
#undef FYC
}

/* ---------------- 页面 ---------------- */
enum { P_MENU, P_CHORDS, P_LISTEN, P_STRUM, P_CAPO, P_SCALE, P_EAR };
static int g_page, g_sel, g_cm, g_ci, g_ui, g_pi, g_capo = 2, g_cs, g_root = 9, g_sc;
static uint32_t g_play_t, g_scale_t;
static int g_listen_chord = -1;
static void guitar_actor(int x, int y) {
  int dt = (int)(VT - g_play_t);
  if (g_play_t && dt < nabo_ms("S-GUITAR-STRUM")) nabo_once("S-GUITAR-STRUM", 56, x, y, dt);
  else nabo("S-GUITAR-IDLE", 56, x, y, (int)VT);
}
static const struct { const char *n, *ic, *d; } PAGES[7] = {{"调音器", "♮", "EADGBE 标准调弦"}, {"和弦谱", "▦", "指法与和弦进行"}, {"听和弦", "◎", "弹一下，显示指法"},
  {"扫弦节奏", "⇅", "跟箭头扫，练节奏"}, {"变调夹", "⊐", "指法与实际和弦"}, {"指板音阶", "⋮", "五声、大小调、布鲁斯"}, {"练耳挑战", "?", "听和弦猜名字"}};
static const struct { const char *n; const char *c[12]; int k; } PROGS[5] = {{"卡农", {"C", "G", "Am", "Em", "F", "C", "F", "G"}, 8}, {"万能 1-5-6-4", {"C", "G", "Am", "F"}, 4},
  {"小情歌 6-4-1-5", {"Am", "F", "C", "G"}, 4}, {"五十年代 1-6-4-5", {"C", "Am", "F", "G"}, 4},
  {"12 小节布鲁斯", {"A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"}, 12}};
static const struct { const char *n; uint8_t iv[7]; int k; } SCALES[5] = {{"小调五声", {0, 3, 5, 7, 10}, 5}, {"大调五声", {0, 2, 4, 7, 9}, 5},
  {"自然大调", {0, 2, 4, 5, 7, 9, 11}, 7}, {"自然小调", {0, 2, 3, 5, 7, 8, 10}, 7}, {"布鲁斯", {0, 3, 5, 6, 7, 10}, 6}};
static const char *CAPO_SH[8] = {"C", "G", "D", "A", "E", "Am", "Em", "Dm"};
static const char *EAR_POOL[12] = {"C", "G", "Am", "F", "Em", "Dm", "D", "E", "A", "E7", "A7", "G7"};
static const struct { const char *n, *p; } STRUMS[5] = {{"民谣基础", "D-DU-UDU"}, {"四下扫", "D-D-D-D-"}, {"八分扫", "DUDUDUDU"}, {"切分", "D-DU-U-U"}, {"华尔兹 3/4", "D-DUDU"}};

/* 和弦进行：每个和弦下扫、上扫各一下，每下 750 ms */
static bool pr_run; static int pr_idx, pr_half; static uint32_t pr_t;
static void pr_stop(void) { pr_run = false; }
static void pr_start(void) { pr_run = true; pr_idx = pr_half = 0; pr_t = VT; play_shape(PROGS[g_pi].c[0], false, 0, true); g_play_t = VT; }
/* 扫弦节奏：时间都按音频采样计（sl_audio_time），判定以起音时刻为准，补偿 45 ms */
static bool st_run; static int st_pi, st_bpm = 80; static uint32_t st_t0, st_next; static int st_beat;
static uint32_t st_anim_t, st_nod_t; static int st_anim_slot = -1;
static int8_t st_j[512]; static struct { int r; uint32_t t; } st_flash;   /* 0 未判 1 完美 2 不错 3 早了 4 晚了 5 错过 */
static uint32_t st_e(void) { return (uint32_t)(SL_SR * 60 / st_bpm / 2); }
static void st_stop(void) { st_run = false; }
static void st_start(void) { st_run = true; memset(st_j, 0, sizeof st_j); st_flash.r = 0; st_anim_t = st_nod_t = 0; st_anim_slot = -1; st_t0 = sl_audio_time() + SL_SR * 6 / 10; st_next = st_t0; st_beat = 0; }
static void st_onset(uint32_t t) {
  if (g_page != P_STRUM || !st_run) return;
  const char *p = STRUMS[st_pi].p; int L = (int)strlen(p); uint32_t e = st_e(); int32_t tt = (int32_t)(t - 720 - st_t0);
  int i0 = tt >= 0 ? (int)((tt + (int32_t)e / 2) / (int32_t)e) : 0, best = -1; int32_t bd = 0;
  for (int i = i0 - 1; i <= i0 + 1; i++) { if (i < 0 || i >= 512 || p[i % L] == '-' || st_j[i]) continue; int32_t d = tt - i * (int32_t)e; if (abs(d) < 2400 && (best < 0 || abs(d) < abs(bd))) { best = i; bd = d; } }
  if (best < 0) return;
  int a = abs(bd), r = a < 800 ? 1 : a < 1600 ? 2 : bd < 0 ? 3 : 4; st_j[best] = (int8_t)r; st_flash.r = r; st_flash.t = VT;
  if (r == 1 && (!st_nod_t || VT - st_nod_t >= (uint32_t)nabo_ms("S-NOD"))) st_nod_t = VT;
}
/* 练耳 */
static int ear_q = -1, ear_opts[4], ear_sel = 1, ear_ans = -1, ear_streak; static uint32_t ear_at;
static void ear_new(void) {
  ear_q = RI(12); ear_opts[0] = ear_q; int n = 1;
  while (n < 4) { int x = RI(12); bool dup = false; for (int i = 0; i < n; i++) dup |= ear_opts[i] == x; if (!dup) ear_opts[n++] = x; }
  for (int i = 3; i > 0; i--) { int j = RI(i + 1), t = ear_opts[i]; ear_opts[i] = ear_opts[j]; ear_opts[j] = t; }
  ear_sel = 1; ear_ans = -1; play_shape(EAR_POOL[ear_q], false, 0, true);
}
static void ear_ok(void) {
  if (ear_ans >= 0) { ear_new(); return; }
  if (ear_sel == 0) { play_shape(EAR_POOL[ear_q], false, 0, true); return; }
  ear_ans = ear_opts[ear_sel - 1]; ear_at = VT;
  if (ear_ans == ear_q) { ear_streak++; if (ear_streak > REC.ear_best) { REC.ear_best = (int16_t)ear_streak; sl_settings_save(); } } else ear_streak = 0;
}

extern int tu_m; extern const sl_scene_t *tu_ret; extern const sl_scene_t SC_TUNER, SC_GUITAR;
static void gt_go(int p) { g_page = p; if (p == P_EAR && ear_q < 0) ear_new(); if (p == P_LISTEN) { g_listen_chord = -1; g_play_t = 0; } }
static void gt_leave(void) { st_stop(); pr_stop(); }
static bool gt_back(void) { if (g_page == P_MENU) return false; gt_leave(); g_page = P_MENU; return true; }
static void gt_enter(void) { g_onset_hook = st_onset; g_page = P_MENU; g_play_t = g_scale_t = st_anim_t = st_nod_t = 0; st_anim_slot = g_listen_chord = -1; }   /* 每次进来（包括从调音器返回）都落在工具箱菜单 */
static void gt_up(void) {
  switch (g_page) {
    case P_MENU: g_sel = (g_sel + 6) % 7; break;
    case P_CHORDS: if (g_cm == 0) g_ci = (g_ci + 1) % NG; else if (g_cm == 1) g_ui = (g_ui + 1) % NU; else { pr_stop(); g_pi = (g_pi + 1) % 5; } break;
    case P_STRUM: if (!st_run) { static const int L[] = {60, 70, 80, 90, 100, 110, 120}; int i = 0; while (L[i] != st_bpm) i++; st_bpm = L[(i + 1) % 7]; } break;
    case P_CAPO: g_capo = (g_capo + 1) % 10; break;
    case P_SCALE: g_root = (g_root + 1) % 12; break;
    case P_EAR: if (ear_ans < 0) ear_sel = (ear_sel + 4) % 5; break;
  }
}
static void gt_down(void) {
  switch (g_page) {
    case P_MENU: g_sel = (g_sel + 1) % 7; break;
    case P_CHORDS: pr_stop(); g_cm = (g_cm + 1) % 3; toast(g_cm == 0 ? "吉他" : g_cm == 1 ? "尤克里里" : "和弦进行"); break;
    case P_STRUM: if (!st_run) st_pi = (st_pi + 1) % 5; break;
    case P_CAPO: g_cs = (g_cs + 1) % 8; break;
    case P_SCALE: g_sc = (g_sc + 1) % 5; break;
    case P_EAR: if (ear_ans < 0) ear_sel = (ear_sel + 1) % 5; break;
  }
}
static void gt_ok(void) {
  switch (g_page) {
    case P_MENU: if (g_sel == 0) { tu_m = 1; tu_ret = &SC_GUITAR; sl_ui_go(&SC_TUNER); } else gt_go(g_sel); break;
    case P_CHORDS: if (g_cm == 0) play_shape(SH_G[g_ci].n, false, 0, true); else if (g_cm == 1) play_shape(SH_U[g_ui].n, true, 0, true); else { if (pr_run) pr_stop(); else pr_start(); } g_play_t = VT; break;
    case P_STRUM: if (st_run) st_stop(); else st_start(); break;
    case P_CAPO: play_shape(CAPO_SH[g_cs], false, g_capo, true); g_play_t = VT; break;
    case P_SCALE: { int r = 52 + (g_root - 4 + 12) % 12; for (int i = 0; i <= SCALES[g_sc].k; i++) sl_pluck(r + (i < SCALES[g_sc].k ? SCALES[g_sc].iv[i] : 12), i * 260, 40, 1200); g_scale_t = g_play_t = VT; } break;
    case P_EAR: ear_ok(); break;
  }
}
static void gt_update(int dt) {
  (void)dt;
  if (pr_run && VT - pr_t >= 750) {
    pr_t += 750; pr_half++; if (pr_half % 2 == 0) pr_idx = (pr_idx + 1) % PROGS[g_pi].k;
    play_shape(PROGS[g_pi].c[pr_idx], false, 0, pr_half % 2 == 0); g_play_t = VT;
  }
  if (st_run) {   /* 节拍器：提前 120 ms 排好咔嗒 */
    int L = (int)strlen(STRUMS[st_pi].p); uint32_t e = st_e();
    while ((int32_t)(st_next - (sl_audio_time() + SL_SR * 12 / 100)) < 0) { sl_click(st_beat % (L / 2) == 0, st_next); st_next += 2 * e; st_beat++; }
    int32_t elapsed = (int32_t)(sl_audio_time() - st_t0);
    if (elapsed >= 0) { int slot = elapsed / (int32_t)e; if (slot != st_anim_slot && STRUMS[st_pi].p[slot % L] != '-') { st_anim_slot = slot; st_anim_t = VT; } }
  }
}

static void gt_draw(void) {
  int amp = fx_clamp(3 * (900 - (int)(VT - g_play_t)) / 900, 0, 3);
  switch (g_page) {
  case P_MENU: {
    header("吉他工具箱", "GUITAR KIT");
    nabo("S-GUITAR-IDLE", 56, 205, 58, (int)VT);
    T(&F_SANS16, PAGES[g_sel].n, 14, 47, C_GOLD, AL_L);
    T(&F_SANS14, PAGES[g_sel].d, 14, 69, C_DIM, AL_L);
    for (int i = 0; i < 7; i++) {
      int y = 92 + i * 28; bool on = i == g_sel;
      rr(8, y, 224, 25, 7, on ? C_CYAN : C_PANEL);
      icon(NULL, PAGES[i].ic, 24, y + 13, on ? C_BG : C_GOLD);
      T(&F_SANS16, PAGES[i].n, 42, y + 13, on ? C_BG : C_WHITE, AL_L);
    }
    static const hint_t H[] = {{"▲▼", "选择"}, {"OK", "进入"}}; hintK(H, 2, true); break; }
  case P_CHORDS:
    if (g_cm < 2) {
      bool uke = g_cm == 1; const char *n = uke ? SH_U[g_ui].n : SH_G[g_ci].n; char md[40]; snprintf(md, sizeof md, "%s %d/%d", uke ? "尤克里里" : "吉他", (uke ? g_ui : g_ci) + 1, uke ? NU : NG);
      header("和弦谱", md); T(&F_MONO22, n, 120, 50, C_WHITE, AL_C);
      if (uke) draw_chord(n, true, 75, 92, 90, 140, C_CYAN, amp, true); else draw_chord(n, false, 62, 92, 115, 140, C_CYAN, amp, true);
      const char *s = shape_of(n, uke); int8_t fr[6]; int k = parse(s, fr); char tones[40] = "组成音 "; bool seen[12] = {0};
      for (int i = 0; i < k; i++) if (fr[i] >= 0) { int pc = ((uke ? OPEN_U : OPEN_G)[i] + fr[i]) % 12; if (!seen[pc]) { seen[pc] = true; strcat(tones, " "); strcat(tones, NN[pc]); } }
      T(&F_SANS14, tones, 120, 274, C_DIM, AL_C);
      guitar_actor(30, 268);
      hint_t H[] = {{"▲", "下一个"}, {"▼", uke ? "和弦进行" : "尤克里里"}, {"OK", "扫弦"}}; hintK(H, 3, true);
    } else {
      const char *cur = PROGS[g_pi].c[pr_run ? pr_idx : 0]; int k = PROGS[g_pi].k;
      header("和弦进行", PROGS[g_pi].n);
      for (int i = 0; i < k; i++) { int x = 12 + (i % 6) * 37, y = 32 + (i / 6) * 24; bool on = pr_run && i == pr_idx; rr(x, y, 34, 20, 5, on ? C_CYAN : C_PANEL); T(&F_MONO14, PROGS[g_pi].c[i], x + 17, y + 10, on ? C_BG : C_WHITE, AL_C); }
      int top = k > 6 ? 96 : 76; T(&F_MONO22, cur, 120, top, C_WHITE, AL_C);
      draw_chord(cur, false, 72, top + 34, 96, 112, C_GOLD, amp, false);
      T(&F_SANS14, pr_run ? "播放中 · 下扫 / 上扫" : "OK 播放这条和弦进行", 120, 286, C_DIM, AL_C);
      guitar_actor(30, 268);
      hint_t H[] = {{"▲", "换进行"}, {"▼", "吉他"}, {"OK", pr_run ? "停止" : "播放"}}; hintK(H, 3, true);
    }
    break;
  case P_LISTEN: {
    header("听和弦", "LISTEN");
    if (MIC.chord != g_listen_chord) { g_listen_chord = MIC.chord; if (MIC.chord >= 0) g_play_t = VT; }
    guitar_actor(30, 60);
    if (MIC.chord >= 0) {
      char nm[8]; snprintf(nm, sizeof nm, "%s%s", NN[MIC.chord / 2], (MIC.chord & 1) ? "m" : "");
      T(&F_MONO22, nm, 120, 48, C_WHITE, AL_C);
      if (shape_of(nm, false)) draw_chord(nm, false, 72, 90, 96, 116, C_GREEN, 0, false); else T(&F_SANS14, "这个和弦还没收录指法", 120, 150, C_DIM, AL_C);
    } else { T(&F_MONO22, "...", 120, 48, C_DARK, AL_C); T(&F_SANS16, "弹一个和弦，让它响着", 120, 150, C_DIM, AL_C); }
    for (int i = 0; i < 12; i++) { int v = MIC.chroma[i] * 44 / 255, x = 12 + i * 37 / 2; fc_rect(x, 284 - (v > 1 ? v : 1), 13, v > 1 ? v : 1, PC12[i], 255); T(&F_MONO14, NN[i], x + 6, 292, C_DIM, AL_C); }
    hintK(NULL, 0, true); break; }
  case P_STRUM: {
    const char *p = STRUMS[st_pi].p; int L = (int)strlen(p); uint32_t e = st_e(); char md[16]; snprintf(md, sizeof md, "%d BPM", st_bpm);
    header("扫弦节奏", md);
    T(&F_SANS16, STRUMS[st_pi].n, 12, 40, C_GOLD, AL_L);
    char ar[40] = ""; for (int i = 0; i < L; i++) { strcat(ar, p[i] == 'D' ? "↓" : p[i] == 'U' ? "↑" : "·"); if (i < L - 1) strcat(ar, " "); }
    T(&F_SANS14, ar, 228, 40, C_WHITE, AL_R);
    int hy = 236; fc_rect(40, hy, 160, 2, C_DIM, 255);
    if (!st_run) { T(&F_DISP24, "OK 开始", 120, 150, C_WHITE, AL_C); T(&F_SANS14, "▲ 换速度   ▼ 换节奏型", 120, 176, C_DIM, AL_C); T(&F_SANS14, "拿起吉他，跟着箭头扫弦", 120, 196, C_DIM, AL_C); }
    else {
      fc_gfx_clip(0, 50, 240, 200);
      int32_t tn = (int32_t)(sl_audio_time() - st_t0); int i0 = tn > 5600 ? (tn - 5600) / (int32_t)e : 0, i1 = (tn + 22400) / (int32_t)e + 1;
      for (int i = i0; i <= i1 && i < 512; i++) {
        char ch = p[i % L]; if (ch == '-') continue;
        int32_t te = i * (int32_t)e, y = hy - (te - tn) * 150 / SL_SR;
        if (tn - te > 2400 && !st_j[i]) st_j[i] = 5;
        int r = st_j[i]; uint16_t col = r == 1 ? C_GREEN : r == 2 ? C_CYAN : r ? C_DARK : ch == 'D' ? C_GOLD : C_MAG;
        if (ch == 'D') fc_tri(106, y - 12, 134, y - 12, 120, y + 10, col); else fc_tri(106, y + 12, 134, y + 12, 120, y - 10, col);
        if (i % L == 0) fc_rect(60, y, 120, 1, C_LINE, 255);
      }
      fc_gfx_clip(0, 0, 0, 0);
      if (st_flash.r && VT - st_flash.t < 450) { static const char *RS[] = {"", "完美", "不错", "早了", "晚了"}; T(&F_DISP24, RS[st_flash.r], 190, hy - 30, st_flash.r == 1 ? C_GREEN : st_flash.r == 2 ? C_CYAN : C_MAG, AL_C); }
    }
    int cnt[6] = {0}; for (int i = 0; i < 512; i++) cnt[st_j[i]]++;
    TF(&F_SANS14, 140, 262, C_DIM, AL_C, "完美 %d 不错 %d 偏 %d 错过 %d", cnt[1], cnt[2], cnt[3] + cnt[4], cnt[5]);
    if (st_nod_t && VT - st_nod_t < (uint32_t)nabo_ms("S-NOD")) nabo_once("S-NOD", 56, 30, 268, (int)(VT - st_nod_t));
    else if (st_run && st_anim_t) {
      int ms = nabo_ms("S-GUITAR-STRUM"), beat_ms = (int)(e * 1000 / SL_SR), period = beat_ms < ms ? beat_ms : ms;
      int dt = (int)(VT - st_anim_t);
      if (dt < period) nabo_once("S-GUITAR-STRUM", 56, 30, 268, dt * ms / period);
      else nabo("S-GUITAR-IDLE", 56, 30, 268, (int)VT);
    } else nabo("S-GUITAR-IDLE", 56, 30, 268, (int)VT);
    if (st_run) { static const hint_t H[] = {{"OK", "停止"}}; hintK(H, 1, true); } else { static const hint_t H[] = {{"▲", "速度"}, {"▼", "节奏型"}, {"OK", "开始"}}; hintK(H, 3, true); }
    break; }
  case P_CAPO: {
    char md[16]; if (g_capo) snprintf(md, sizeof md, "夹 %d 品", g_capo); else snprintf(md, sizeof md, "不夹");
    header("变调夹", md); T(&F_DISP24, g_capo ? md : "不夹变调夹", 120, 46, C_WHITE, AL_C);
    for (int i = 0; i < 8; i++) {
      int x = 14 + (i % 2) * 110, y = 68 + (i / 2) * 42; bool on = i == g_cs; char tn[8], lab[16]; trans_name(CAPO_SH[i], g_capo, tn); snprintf(lab, sizeof lab, "-> %s", tn);
      if (on) rr_frame(x, y, 102, 38, 8, 1, C_CYAN, C_PANEL); else rr(x, y, 102, 38, 8, RGB(0x10, 0x0C, 0x22));
      TF(&F_SANS14, x + 10, y + 12, C_DIM, AL_L, "%s 型", CAPO_SH[i]); T(&F_MONO22, lab, x + 10, y + 28, on ? C_CYAN : C_WHITE, AL_L);
    }
    guitar_actor(30, 268);
    T(&F_SANS16, "左边是指法", 150, 256, C_WHITE, AL_C);
    T(&F_SANS16, "右边是实际和弦", 150, 280, C_WHITE, AL_C);
    static const hint_t H[] = {{"▲", "品数"}, {"▼", "选指法"}, {"OK", "试听"}}; hintK(H, 3, true); break; }
  case P_SCALE: {
    char md[24]; snprintf(md, sizeof md, "%s %s", NN[g_root], SCALES[g_sc].n); header("指板音阶", md);
    int x0 = 72, dx = 28, y0 = 46, fy = 185;   /* fy ×10；左上留出 56px 演出区 */
    for (int f = 3; f <= 9; f += 2) fc_disc(x0 + dx * 5 / 2, y0 + (f * 10 - 5) * fy / 100, 3, C_LINE, 255);
    fc_disc(x0 + dx * 2, y0 + 115 * fy / 100, 3, C_LINE, 255); fc_disc(x0 + dx * 3, y0 + 115 * fy / 100, 3, C_LINE, 255);
    fc_rect(x0 - 2, y0 - 2, dx * 5 + 4, 3, C_WHITE, 255);
    for (int f = 1; f <= 12; f++) fc_rect(x0, y0 + f * fy / 10, dx * 5, 1, C_DARK, 255);
    for (int s = 0; s < 6; s++) fc_rect(x0 + s * dx, y0, 1, 12 * fy / 10, C_DIM, 255);
    static const int FM[] = {3, 5, 7, 9, 12}; for (int i = 0; i < 5; i++) TF(&F_MONO14, 18, y0 + (FM[i] * 10 - 5) * fy / 100, C_DIM, AL_C, "%d", FM[i]);
    int pl = g_scale_t && VT - g_scale_t < 2400 ? (int)(VT - g_scale_t) / 260 : -1, plpc = pl >= 0 && pl <= SCALES[g_sc].k ? ((pl < SCALES[g_sc].k ? SCALES[g_sc].iv[pl] : 12) + g_root) % 12 : -1;
    for (int s = 0; s < 6; s++) for (int f = 0; f <= 12; f++) {
      int pc = (OPEN_G[s] + f) % 12, deg = (pc - g_root + 12) % 12; bool in = false; for (int i = 0; i < SCALES[g_sc].k; i++) in |= SCALES[g_sc].iv[i] == deg;
      if (!in) continue;
      int x = x0 + s * dx, y = f ? y0 + (f * 10 - 5) * fy / 100 : y0 - 11; bool hot = pc == plpc;
      rr(x - 12, y - 9, 24, 18, 7, hot ? C_WHITE : deg == 0 ? C_GOLD : (g_sc == 4 && deg == 6) ? C_MAG : C_CYAN);
      T(&F_SANS16, NN[pc], x, y, C_BG, AL_C);
    }
    T(&F_SANS14, "金色是主音", 200, y0 + 126 * fy / 100, C_GOLD, AL_R);
    guitar_actor(30, 58);
    static const hint_t H[] = {{"▲", "换调"}, {"▼", "音阶"}, {"OK", "播放"}}; hintK(H, 3, true); break; }
  case P_EAR: {
    char md[32]; snprintf(md, sizeof md, "连对 %d · 最佳 %d", ear_streak, REC.ear_best); header("练耳挑战", md);
    bool good = ear_ans >= 0 && ear_ans == ear_q;
    if (ear_ans >= 0) nabo_once(good ? "S-WOW" : "S-OOPS", 76, 62, 72, (int)(VT - ear_at)); else nabo("S-THINK", 76, 62, 72, (int)VT);
    if (ear_ans >= 0) draw_chord(EAR_POOL[ear_q], false, 140, 52, 70, 62, good ? C_GREEN : C_RED, 0, false); else T(&F_SANS16, "这是哪个和弦？", 172, 72, C_WHITE, AL_C);
    for (int i = 0; i < 5; i++) {
      int y = 132 + i * 32; bool on = i == ear_sel; uint16_t bg = on ? C_CYAN : C_PANEL, fg = on ? C_BG : C_WHITE;
      if (ear_ans >= 0 && i > 0) { int o = ear_opts[i - 1]; if (o == ear_q) { bg = C_GREEN; fg = C_BG; } else if (o == ear_ans) { bg = C_RED; fg = C_BG; } else { bg = C_PANEL; fg = C_DIM; } }
      rr(30, y, 180, 28, 7, bg);
      if (i == 0) T(&F_SANS16, "再听一次", 120, y + 14, fg, AL_C); else T(&F_MONO22, EAR_POOL[ear_opts[i - 1]], 120, y + 14, fg, AL_C);
    }
    if (ear_ans >= 0) { hint_t H[] = {{"OK", good ? "答对了！下一题" : "下一题"}}; hintK(H, 1, true); } else { static const hint_t H[] = {{"▲▼", "选择"}, {"OK", "确定"}}; hintK(H, 2, true); }
    break; }
  }
}
const sl_scene_t SC_GUITAR = {.id = "guitar", .name = "吉他工具箱", .en = "GUITAR KIT", .ic = "♪", .mic = NM_F_CHROMA, .enter = gt_enter, .leave = gt_leave,
                              .up = gt_up, .down = gt_down, .ok = gt_ok, .back = gt_back, .update = gt_update, .draw = gt_draw};
