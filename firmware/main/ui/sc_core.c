/* 开机、主菜单、设置（原型 SPLASH / MENU / SETTINGS） */
#include "sl_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 开机 ---------------- */
static int out1000(int p) { return ease_out(fx_clamp(p, 0, FX_ONE)); }
static bool boot_voice_started;
static void boot_enter(void) { boot_voice_started = false; sl_boot_voice_stop(); }
static void boot_update(int dt) { (void)dt; if (!boot_voice_started && EL() >= 1400) { boot_voice_started = true; sl_boot_voice_start(); } }
static void boot_leave(void) { sl_boot_voice_stop(); }
static void boot_draw(void) {
  int t = EL(), mid = 150;
  if (t < 300) { int w = 240 * out1000(t * 1000 / 300) / 1000; fc_rect(120 - w / 2, mid, w, 2, C_CYAN, 255); }
  else if (t < 900) {      /* 平线振动，振幅越来越大：y = a·sin(πx/240)·(0.7 sin(0.09x + t/60) + 0.3 sin(0.23x − t/35)) */
    int a = 34 * out1000((t - 300) * 1000 / 600) / 1000, px = 0, py = mid;
    for (int x = 0; x <= 240; x += 4) {
      int env = fx_sin(x * 512 / 240);                                                       /* Q14 */
      int w = (fx_sin(x * 1467 / 100 + t * 2716 / 1000) * 7 + fx_sin(x * 3750 / 100 - t * 4656 / 1000) * 3) / 10;   /* Q14，1 弧度 ≈ 163 */
      int y = mid + (int)((int64_t)a * env * w >> 28);
      if (x) fc_line(px, py, x, y, 2, C_CYAN);
      px = x; py = y;
    }
  } else {                 /* 炸开成 32 根频谱柱，之后一直轻轻跳：0.35 + 0.65·|sin(0.7i + t/180)·sin(0.31i + t/420)| */
    int k = out1000((t - 900) * 1000 / 400);
    for (int i = 0; i < 32; i++) {
      int v;
      if (SET.calm) v = 500 + 300 * fx_sin(i * 98) / 16384;
      else { int pr = (int)((int64_t)fx_sin(i * 114 + (int)(VT * 905 / 1000)) * fx_sin(i * 50 + (int)(VT * 388 / 1000)) >> 14); if (pr < 0) pr = -pr; v = 350 + 650 * pr / 16384; }
      int h = v * k / 1000 * 70 / 1000; if (h < 2) h = 2;
      fc_rect(i * 15 / 2 + 1, 184 - h, 6, h, BARC[i], 255);
    }
  }
  /* 标题逐字出现，每个字带 2 帧红青错位 */
  static const char *title = "NABO 声音实验室";
  int total = 10, shown = fx_clamp((t - 600) / 90, 0, total);
  if (shown > 0) {
    const char *end = title; for (int i = 0; i < shown; i++) fc_utf8_next(&end);
    int full = fc_text_width(&F_DISP24, title), x0 = 120 - full / 2, nb = (int)(end - title);
    bool gl = !SET.calm && shown < total && (t - 600) % 90 < 40;
    if (gl) { fc_text_n(&F_DISP24, x0 - 2, 214, title, nb, C_MAG, AL_L); fc_text_n(&F_DISP24, x0 + 2, 214, title, nb, C_CYAN, AL_L); }
    const fc_img_t *logo = shown == total && t > 1500 ? prop("title") : NULL;   /* 字出齐后换成霓虹标题字 */
    if (logo) fc_img(logo, 0, 120 - logo->w / 2, 214 - logo->h / 2, 255);
    else fc_text_n(&F_DISP24, x0, 214, title, nb, C_WHITE, AL_L);
  }
  if (t > 1300) T(&F_MONO14, "NABO SOUND LAB", 120, 240, C_CYAN, AL_C);
  if (t > 1400) {          /* NABO 从底部升起：戴耳机、打碟、挥手，然后待机 */
    int p = out1000((t - 1400) * 1000 / 500), y = 380 + (124 - 380) * p / 1000, tt = t - 1400, wm = nabo_ms("S-WELCOME");
    if (tt < wm) nabo_once("S-WELCOME", 120, 120, y, tt); else nabo("S-IDLE", 120, 120, y, tt - wm);
  }
  if (t > 1900 && (SET.calm || (VT / 500) % 2)) T(&F_SANS16, "按 OK 开始", 120, 290, C_CYAN, AL_C);
}
static void boot_ok(void) { if (EL() > 500) { extern const sl_scene_t SC_MENU; sl_ui_go(&SC_MENU); } }
static void boot_any(void) { boot_ok(); }
const sl_scene_t SC_BOOT = {.id = "boot", .name = "开机", .up_now = true, .enter = boot_enter, .leave = boot_leave, .up = boot_any, .down = boot_any, .ok = boot_ok, .update = boot_update, .draw = boot_draw};

/* ---------------- 主菜单 ---------------- */
extern const sl_scene_t SC_ECHO, SC_SHOUT, SC_CANDLE, SC_ROPE, SC_RANGE, SC_SPEC, SC_WATER, SC_SCOPE, SC_CHROMA, SC_DB, SC_TUNER, SC_GUITAR, SC_BEAT, SC_PITCH, SC_SETTINGS,
    SC_FLIGHT, SC_CLAP, SC_SAMPLER;
typedef struct { const char *sep; const sl_scene_t *sc; const char *soon, *soon_ic; } item_t;
static const item_t ITEMS[] = {
  {"玩 · 声音玩具", 0, 0, 0}, {0, &SC_ECHO, 0, 0}, {0, &SC_SHOUT, 0, 0}, {0, &SC_CANDLE, 0, 0}, {0, &SC_ROPE, 0, 0}, {0, &SC_RANGE, 0, 0},
  {0, &SC_FLIGHT, 0, 0}, {0, &SC_CLAP, 0, 0}, {0, &SC_SAMPLER, 0, 0},
  {"看 · 声音可视化", 0, 0, 0}, {0, &SC_SPEC, 0, 0}, {0, &SC_WATER, 0, 0}, {0, &SC_SCOPE, 0, 0}, {0, &SC_CHROMA, 0, 0}, {0, &SC_DB, 0, 0},
  {"练 · 音乐工具", 0, 0, 0}, {0, &SC_TUNER, 0, 0}, {0, &SC_GUITAR, 0, 0}, {0, &SC_BEAT, 0, 0}, {0, &SC_PITCH, 0, 0},
  {"其他", 0, 0, 0}, {0, &SC_SETTINGS, 0, 0},
};
#define NITEMS ((int)(sizeof ITEMS / sizeof ITEMS[0]))
static int m_sel = 1, m_scroll;
static void m_move(int d) { int s = m_sel; do { s = (s + d + NITEMS) % NITEMS; } while (ITEMS[s].sep); m_sel = s; }
static void m_up(void) { m_move(-1); }
static void m_down(void) { m_move(1); }
static void m_ok(void) {
  const item_t *it = &ITEMS[m_sel];
  if (it->soon) { char b[48]; snprintf(b, sizeof b, "%s 还在设计中", it->soon); toast(b); return; }
  sl_ui_go(it->sc);
}
static void menu_draw(void) {
  for (int i = 0; i < 32; i++) { int h = MIC.bands[i] * 70 / 255; if (h) fc_rect(i * 15 / 2 + 1, 100 - h, 6, h, mix565(BARC[i], C_BG, 550), 255); }
  const char *role = m_sel < 9 ? "S-MIC-IDLE" : m_sel >= 16 && m_sel <= 19 ? "S-GUITAR-IDLE" : "S-IDLE";
  nabo(role, 76, 120, 54, (int)VT);
  T(&F_DISP24, "声音实验室", 120, 108, C_WHITE, AL_C);
  T(&F_MONO14, "NABO SOUND LAB", 120, 128, C_CYAN, AL_C);
  const int rowH = 24, top = 140, vis = 6;
  if (m_sel < m_scroll) m_scroll = m_sel;
  if (m_sel >= m_scroll + vis) m_scroll = m_sel - vis + 1;
  for (int k = 0; k < vis; k++) {
    int i = m_scroll + k; if (i >= NITEMS) break;
    const item_t *it = &ITEMS[i]; int y = top + k * rowH;
    if (it->sep) {
      const fc_img_t *gi = prop(!strncmp(it->sep, "玩", 3) ? "group-play" : !strncmp(it->sep, "看", 3) ? "group-see" : !strncmp(it->sep, "练", 3) ? "group-practice" : "-");
      int tx = 14; if (gi) { fc_img(gi, 0, 14, y + 11 - gi->h / 2, 255); tx = 14 + gi->w + 6; }
      T(&F_SANS14, it->sep, tx, y + 11, C_GOLD, AL_L); fc_rect(tx + fc_text_width(&F_SANS14, it->sep) + 8, y + 11, 100, 1, C_LINE, 255); continue;
    }
    bool on = i == m_sel;
    if (on) rr(8, y, 222, rowH - 2, 6, C_CYAN);
    const char *ic = it->sc ? it->sc->ic : it->soon_ic, *nm = it->sc ? it->sc->name : it->soon, *en = it->sc ? it->sc->en : "SOON";
    icon(it->sc, ic, 24, y + 11, on ? C_BG : it->soon ? C_DARK : C_MAG);
    T(&F_SANS16, nm, 40, y + 10, on ? C_BG : it->soon ? C_DIM : C_WHITE, AL_L);
    T(&F_MONO14, en, 224, y + 11, on ? C_BG : C_DIM, AL_R);
  }
  int tot = NITEMS; fc_rect(234, top + m_scroll * vis * rowH / tot, 2, vis * vis * rowH / tot, C_DARK, 255);
  static const hint_t H[] = {{"▲▼", "选择"}, {"OK", "进入"}};
  hintK(H, 2, false);
}
const sl_scene_t SC_MENU = {.id = "menu", .name = "菜单", .up_now = true, .up = m_up, .down = m_down, .ok = m_ok, .draw = menu_draw};

/* ---------------- 设置 ---------------- */
static const int16_t A4L[] = {440, 441, 442, 443, 444, 445, 446, 415, 430, 432, 435, 438};
static int s_sel;
static uint32_t s_blink_at;
static bool s_blink;
static void s_up(void) { s_sel = (s_sel + 5) % 6; }
static void s_down(void) { s_sel = (s_sel + 1) % 6; }
static void cycle_sens(bool announce) {
  static const int16_t L[] = {0, 6, 12, -12, -6};
  int i = 0; while (i < 5 && L[i] != SET.sens) i++;
  SET.sens = L[(i + 1) % 5]; sl_settings_save();
  if (!announce) return;    /* 设置页已就地显示新值，保留其帮助文字和NABO。 */
  char b[32]; snprintf(b, sizeof b, "灵敏度 %+d dB", SET.sens); toast(b);
}
void sl_cycle_sens(void) { cycle_sens(true); }
static void s_ok(void) {
  if (s_sel == 0) { SET.volume = (uint8_t)(((SET.volume / 10 + 1) % 11) * 10); sl_volume_apply(SET.volume); }
  else if (s_sel == 1) { int i = 0; while (i < 12 && A4L[i] != SET.a4) i++; SET.a4 = A4L[(i + 1) % 12]; }
  else if (s_sel == 2) { SET.calm = !SET.calm; s_blink_at = VT; s_blink = true; }
  else if (s_sel == 3) SET.quiet = SET.quiet >= 80 ? 40 : SET.quiet + 5;
  else if (s_sel == 4) SET.cal = SET.cal >= 130 ? 90 : SET.cal + 5;
  else { cycle_sens(false); return; }
  sl_settings_save();
}
static void settings_draw(void) {
  header("设置", "SETTINGS");
  char v[6][24];
  snprintf(v[0], 24, "%u%%", (unsigned)SET.volume);
  snprintf(v[1], 24, "%d Hz", SET.a4); snprintf(v[2], 24, "%s", SET.calm ? "开" : "关");
  snprintf(v[3], 24, "%d dB", SET.quiet); snprintf(v[4], 24, "+%d", SET.cal); snprintf(v[5], 24, "%+d dB", SET.sens);
  static const char *K[6] = {"音量", "A4 基准", "减弱闪烁", "安静阈值", "dB 校准", "频谱灵敏度"};
  for (int i = 0; i < 6; i++) {
    int y = 34 + i * 31; bool on = i == s_sel;
    rr(10, y, 220, 29, 7, on ? C_CYAN : C_PANEL);
    T(&F_SANS16, K[i], 22, y + 14, on ? C_BG : C_WHITE, AL_L);
    T(&F_MONO22, v[i], 218, y + 14, on ? C_BG : C_CYAN, AL_R);
  }
  static const char *HELP[6][2] = {
    {"0-100% 音量", "0% 为静音"}, {"A4 415-446 Hz", "影响音高基准"},
    {"可关闭抖动闪光", "减少抖动闪烁"}, {"安静阈值 40-80", "影响声音提醒"},
    {"环境音量校准", "dBFS + 校准值"}, {"0/+6/+12/-12/-6", "调整声音灵敏度"},
  };
  T(&F_SANS16, HELP[s_sel][0], 12, 239, C_DIM, AL_L);
  T(&F_SANS16, HELP[s_sel][1], 12, 264, C_DIM, AL_L);
  /* S-IDLE第8帧闭眼，前后7/9帧睁眼；切换时播一次原片眨眼，不改眼睛像素。 */
  int nt = (int)VT;
  if (s_blink) { int d = (int)(VT - s_blink_at); if (d < 300) nt = (7 * 1000 + 11) / 12 + d; else s_blink = false; }
  nabo("S-IDLE", 56, 200, 255, nt);
  static const hint_t H[] = {{"▲▼", "选择"}, {"OK", "修改"}};
  hintK(H, 2, true);
}
const sl_scene_t SC_SETTINGS = {.id = "settings", .name = "设置", .en = "SETTINGS", .ic = "⚙", .up = s_up, .down = s_down, .ok = s_ok, .draw = settings_draw};
