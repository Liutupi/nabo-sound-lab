/* 声音挑战赛：吹蜡烛 → 走钢丝 → 哼歌飞行 → 拍手节奏，四关连玩，一局约两分钟，合计 400 分。
 * 规则全在 circus_model.c；这里只负责取麦克风输入、发声和画面。只用已有素材和已有字形，不改资源包。 */
#include "sc_circus.h"
#include "circus_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t CIRCUS_BEST;
static cr_t cr;
static uint32_t cr_origin, cr_feedback_at, cr_final_at;
static int cr_feedback;
static bool cr_new_best;

static const char *const STAGE_NAME[CR_STAGES] = {"吹蜡烛", "走钢丝", "哼歌飞行", "拍手节奏"};
static const char *const STAGE_TIP[CR_STAGES] = {"对着工牌用力吹", "唱一个长音，保持稳定", "哼高向上，哼低向下", "先听四拍，再跟光圈拍"};
static const char *const STAGE_ROLE[CR_STAGES] = {"S-CAKE-WAIT", "S-ROPE-READY", "S-FLOAT", "S-DRUM"};
static const char *const RANK_NAME[4] = {"再练一次", "稳定发挥", "节奏高手", "金牌音乐人"};

static bool voiced(void) { return MIC.f0_x10 && MIC.f0_conf_x1000 >= 800 && MIC.db_x100 > -6000; }
static int blow(void) {            /* 和吹蜡烛玩具同一判定：音量够大、没有音高、低频能量高 */
  int low = (MIC.bands[0] + MIC.bands[1] + MIC.bands[2] + MIC.bands[3] + MIC.bands[4]) / 5;
  bool pitched = MIC.f0_x10 && MIC.f0_conf_x1000 > 850;
  return MIC.db_x100 > -3600 && !pitched && low > 115 ? fx_clamp((MIC.db_x100 + 3600) * 1000 / 1800, 150, 1000) : 0;
}
static int clap_ms(void) { return (int32_t)(sl_audio_time() - cr_origin) / 16 - 45; }

static void on_onset(uint32_t sample) {
  if (cr.phase != CR_PLAY || cr.stage != CR_CLAP || sl_onset_db_x100() < -5000) return;
  int r = cr_clap_hit(&cr, (int32_t)(sample - cr_origin) / 16 - 45);
  if (r) { cr_feedback = r; cr_feedback_at = VT; }
}

static void events(int ev) {
  if (ev & CR_EV_PLAY) {
    sl_audio_stop_all(); cr_feedback = 0;
    if (cr.stage == CR_CLAP) {
      cr_origin = sl_audio_time();
      for (int i = 0; i < 4; i++) sl_click(i == 0, cr_origin + (uint32_t)(i * cr.clap.period * 16));
    }
  }
  if (ev & CR_EV_SCORE) {          /* 过关提示音：分数越高升得越高 */
    sl_audio_stop_all();
    int s = cr.score[cr.stage];
    sl_pluck(60, 0, 35, 500); if (s >= 40) sl_pluck(64, 140, 35, 500); if (s >= 70) sl_pluck(67, 280, 35, 700);
  }
  if (ev & CR_EV_FINAL) {
    cr_final_at = VT; cr_new_best = cr.total > CIRCUS_BEST;
    if (cr_new_best) { CIRCUS_BEST = cr.total; sl_settings_save(); }
    if (cr_rank(cr.total) >= 2) confetti(60);
    static const uint8_t up[] = {60, 64, 67, 72};
    for (int i = 0; i < 1 + cr_rank(cr.total); i++) sl_pluck(up[i], i * 150, 35, 900);
  }
}

static void ci_enter(void) { cr_init(&cr, rng_u32()); g_onset_hook = on_onset; cr_feedback = 0; cr_new_best = false; particles_clear(); }
static void ci_leave(void) { g_onset_hook = NULL; sl_audio_stop_all(); particles_clear(); }
static void ci_ok(void) { if (cr.phase == CR_FINAL) particles_clear(); events(cr_ok(&cr)); }
static void ci_down(void) { events(cr_skip(&cr)); }
static void ci_update(int dt) {
  bool v = voiced();
  cr_in_t in = {.blow = (uint16_t)blow(), .midi100 = (int16_t)(v ? hz10_to_midi100(MIC.f0_x10) : 0), .voiced = v,
                .clap_ms = cr.phase == CR_PLAY && cr.stage == CR_CLAP ? clap_ms() : 0};
  events(cr_step(&cr, dt, &in));
}

/* 顶部四个关卡点：已完成金色，当前青色 */
static void dots(void) {
  for (int i = 0; i < CR_STAGES; i++) {
    bool done = i < cr.stage || (i == cr.stage && cr.phase >= CR_SCORE);
    fc_disc(84 + i * 24, 40, 5, done ? C_GOLD : i == cr.stage ? C_CYAN : C_LINE, 255);
  }
}
static void bar(int y, int v, int max, uint16_t c) { fc_rect(20, y, 200, 6, C_PANEL, 255); fc_rect(20, y, 200 * fx_clamp(v, 0, max) / max, 6, c, 255); }

static void draw_candle(void) {
  int bs = blow();
  const fc_img_t *cake = prop("cake");
  if (cake) fc_img(cake, 0, 140 - cake->w / 2, 296 - cake->h, 255);
  else { fc_rect(40, 236, 200, 56, RGB(0xFF, 0x7E, 0xB6), 255); fc_rect(66, 198, 148, 40, RGB(0xFF, 0xE7, 0xC2), 255); }
  for (int i = CR_CANDLES - 1; i >= 0; i--) {
    int x = 140 + (2 * i - (CR_CANDLES - 1)) * 9, y = 198;
    const fc_img_t *cn = prop(i % 2 ? "candle-b" : "candle-a");
    if (cn) fc_img(cn, 0, x - cn->w / 2, y - cn->h, 255); else fc_rect(x - 2, y - 20, 5, 20, i % 2 ? C_CYAN : C_WHITE, 255);
    if (cr.life[i] > 0) {
      int lean = bs * 8 / 1000, fl = SET.calm ? 0 : fx_sin((int)(VT * 1024 / 345) + i * 212) * 14 / 16384, h = 9 + fl / 10 - bs * 3 / 1000;
      fc_ellipse(x + lean, y - 26, 3, h * 10 / 16 + 1, C_GOLD, 255); fc_ellipse(x + lean * 7 / 10, y - 25, 1, h * 10 / 32, C_WHITE, 255);
    }
  }
  nabo(bs ? "S-BLOW" : "S-CAKE-WAIT", 76, 40, 100, (int)VT);
  if (cr.blow_t0 >= 0) TF(&F_SANS16, 150, 76, C_WHITE, AL_C, "还剩 %d 根", cr.lit); else T(&F_SANS16, STAGE_TIP[CR_CANDLE], 150, 76, C_WHITE, AL_C);
  fc_rect(98, 100, 104, 5, C_PANEL, 255); fc_rect(98, 100, 104 * bs / 1000, 5, bs ? C_CYAN : C_DARK, 255);
  bar(56, CR_CANDLE_LIMIT_MS - cr.t, CR_CANDLE_LIMIT_MS, C_PURPLE);
}

static void draw_rope(void) {
  const fc_img_t *post = prop("rope-post");
  if (post) { fc_img(post, 0, 14 - post->w / 2, 170, 255); fc_img(post, 0, 226 - post->w / 2, 170, 255); } else { fc_rect(12, 170, 4, 74, C_DIM, 255); fc_rect(224, 170, 4, 74, C_DIM, 255); }
  int px = 14, py = 172;
  for (int k = 1; k <= 24; k++) { int x = 14 + 212 * k / 24, u = k * 1000 / 24, y = 172 + 2 * 22 * u / 1000 * (1000 - u) / 1000; fc_line(px, py, x, y, 2, C_GOLD); px = x; py = y; }
  int p = cr.prog * 1000 / CR_ROPE_WALK_MS, rx = 45 + p * 153 / 1000, u = (rx - 14) * 1000 / 212, ry = 172 + 2 * 22 * u / 1000 * (1000 - u) / 1000;
  int tilt = fx_clamp(cr.cents * 2, -110, 110);
  const char *id = !cr.target ? "S-ROPE-READY" : abs(tilt) > 40 ? "S-ROPE-WOBBLE" : "S-ROPE-WALK";
  fc_gfx_clip(0, 62, 240, 236);
  nabo_sq(id, 76, rx, ry - 38, (int)VT, 800, 800, tilt);
  fc_gfx_clip(0, 0, 0, 0);
  if (!cr.target) T(&F_SANS16, STAGE_TIP[CR_ROPE], 120, 82, C_WHITE, AL_C);
  else if (voiced()) TF(&F_SANS16, 120, 82, C_WHITE, AL_C, "走了 %d%%", p / 10);
  else T(&F_SANS16, "继续哼唱", 120, 82, C_GOLD, AL_C);
  int by = 262; fc_rect(30, by, 180, 10, C_PANEL, 255); fc_rect(120 - 27, by, 54, 10, RGB(0x17, 0x3D, 0x2A), 255); fc_rect(119, by - 2, 2, 14, C_WHITE, 255);
  if (cr.target) { int ax = 120 + fx_clamp(cr.cents, -100, 100) * 9 / 10; fc_rect(ax - 2, by - 3, 5, 16, abs(cr.cents) < 30 ? C_GREEN : C_MAG, 255); }
  T(&F_SANS14, "偏低", 30, by + 20, C_DIM, AL_L); T(&F_SANS14, "偏高", 210, by + 20, C_DIM, AL_R);
  bar(56, CR_ROPE_LIMIT_MS - cr.t, CR_ROPE_LIMIT_MS, C_PURPLE);
}

static void draw_flight(void) {
  const ng_flight_t *f = &cr.flight;
  TF(&F_SANS16, 10, 58, C_GOLD, AL_L, "穿门 %d / 12", f->cleared);
  TF(&F_SANS16, 230, 58, C_MAG, AL_R, "生命 %d", f->lives);
  fc_gfx_clip(0, 72, 240, 222);
  if (f->state != NG_READY) for (int i = 0; i < 4; i++) {
    int x = f->gate[i].xq / 256, gap = ng_flight_gap(f), a = f->gate[i].y - gap / 2 + 26, b = a + gap;
    fc_rect(x, 72, 16, a - 72, C_PURPLE, 255); fc_rect(x, b, 16, 294 - b, C_PURPLE, 255);
    fc_rect(x, a - 3, 16, 3, C_CYAN, 255); fc_rect(x, b, 16, 3, C_CYAN, 255);
  }
  nabo("S-FLOAT", 76, 54, f->yq / 256 + 26, (int)VT);
  if (f->flash_ms) fc_frame(2, 74, 236, 218, 2, C_RED);
  fc_gfx_clip(0, 0, 0, 0);
  if (f->state == NG_READY) {
    rr(8, 128, 224, 84, 10, C_PANEL);
    T(&F_SANS16, "先哼一个音", 120, 152, C_WHITE, AL_C);
    T(&F_SANS16, STAGE_TIP[CR_FLIGHT], 120, 184, C_CYAN, AL_C);
  } else if (!voiced()) T(&F_SANS16, "继续哼唱", 120, 280, C_GOLD, AL_C);
}

static void draw_clap(void) {
  const ng_clap_t *c = &cr.clap;
  int t = clap_ms() + 45, first = 4 * c->period;
  if (t < first) {
    T(&F_SANS16, "准备，先听四拍", 120, 76, C_WHITE, AL_C);
    TF(&F_MONO48, 120, 140, C_GOLD, AL_C, "%d", fx_clamp(4 - t / c->period, 1, 4));
  } else {
    TF(&F_SANS16, 120, 70, C_CYAN, AL_C, "第 %d / 16 拍", c->judged < 16 ? c->judged + 1 : 16);
    int phase = (t - first) % c->period, dist = phase < c->period / 2 ? phase : c->period - phase;
    fc_ring(120, 150, 20 + dist * 40 / (c->period / 2), 3, C_PURPLE); fc_ring(120, 150, 20, 3, dist <= 120 ? C_CYAN : C_DIM);
    if (cr_feedback && VT - cr_feedback_at < 420) T(&F_DISP18, cr_feedback == 2 ? "精准" : cr_feedback == 1 ? "命中" : "多拍了", 120, 150, cr_feedback > 0 ? C_GREEN : C_MAG, AL_C);
    TF(&F_SANS16, 157, 236, C_GOLD, AL_C, "命中 %d / 16", c->hits);
    TF(&F_SANS16, 157, 264, C_CYAN, AL_C, "连击 %d", c->combo);
  }
  int cx = t >= first ? 40 : 120, hit = cr_feedback > 0 && VT - cr_feedback_at < (uint32_t)nabo_ms("S-DRUM");
  nabo_once("S-DRUM", 76, cx, 250, hit ? (int)(VT - cr_feedback_at) : 0);
}

static void ci_draw(void) {
  char mode[16] = "";
  if (cr.phase != CR_TITLE && cr.phase != CR_FINAL) snprintf(mode, sizeof mode, "%d / %d", cr.stage + 1, CR_STAGES);
  header(cr.phase == CR_PLAY ? STAGE_NAME[cr.stage] : "声音挑战赛", mode);
  hint_t H[2] = {{"OK", "开始"}, {NULL, NULL}};
  switch (cr.phase) {
  case CR_TITLE:
    nabo("S-CHEER", 76, 120, 84, (int)VT);
    T(&F_DISP24, "四关连玩", 120, 146, C_WHITE, AL_C);
    T(&F_SANS16, "一局约两分钟", 120, 176, C_CYAN, AL_C);
    T(&F_SANS16, "吹蜡烛 · 走钢丝", 120, 212, C_DIM, AL_C);
    T(&F_SANS16, "哼歌飞行 · 拍手节奏", 120, 238, C_DIM, AL_C);
    if (CIRCUS_BEST) TF(&F_SANS16, 120, 272, C_GOLD, AL_C, "最佳 %d 分", CIRCUS_BEST);
    break;
  case CR_INTRO:
    dots();
    TF(&F_SANS16, 120, 74, C_CYAN, AL_C, "第 %d 关", cr.stage + 1);
    T(&F_DISP24, STAGE_NAME[cr.stage], 120, 106, C_WHITE, AL_C);
    nabo(STAGE_ROLE[cr.stage], 76, 120, 176, (int)VT);
    T(&F_SANS16, STAGE_TIP[cr.stage], 120, 244, C_GOLD, AL_C);
    bar(270, cr.t, CR_INTRO_MS, C_CYAN);
    H[0].label = "跳过";
    break;
  case CR_PLAY:
    dots();
    if (cr.stage == CR_CANDLE) draw_candle(); else if (cr.stage == CR_ROPE) draw_rope(); else if (cr.stage == CR_FLIGHT) draw_flight(); else draw_clap();
    H[0] = (hint_t){"▼", "跳过"};
    break;
  case CR_SCORE: {
    int s = cr.score[cr.stage];
    dots();
    T(&F_DISP24, STAGE_NAME[cr.stage], 120, 76, C_WHITE, AL_C);
    nabo_once(s >= 60 ? "S-THUMB" : "S-OOPS", 76, 120, 132, cr.t);
    TF(&F_MONO48, 120, 204, s >= 60 ? C_GOLD : C_WHITE, AL_C, "%d", s);
    T(&F_SANS16, "本关得分", 120, 240, C_DIM, AL_C);
    TF(&F_SANS16, 120, 270, C_CYAN, AL_C, "合计 %d 分", cr.total);
    H[0].label = cr.stage + 1 < CR_STAGES ? "下一关" : "看成绩";
    break;
  }
  default: {
    int el = (int)(VT - cr_final_at), n = cr_notes(cr.total), ch = nabo_ms("S-CHEER");
    if (cr_rank(cr.total) >= 1) { if (el < ch) nabo_once("S-CHEER", 76, 44, 72, el); else nabo("S-NOD", 76, 44, 72, el - ch); }
    else nabo_once("S-OOPS", 76, 44, 72, el);
    TF(&F_MONO48, 158, 62, C_WHITE, AL_C, "%d", cr.total);
    T(&F_SANS14, "合计 · 满分 400", 158, 96, C_DIM, AL_C);
    for (int i = 0; i < 5; i++) T(&F_DISP24, "♪", 60 + i * 30, 132, i < n ? C_GOLD : C_DARK, AL_C);
    T(&F_DISP24, RANK_NAME[cr_rank(cr.total)], 120, 168, C_CYAN, AL_C);
    for (int i = 0; i < CR_STAGES; i++) {
      int y = 198 + i * 18;
      T(&F_SANS14, STAGE_NAME[i], 44, y, C_DIM, AL_L);
      fc_rect(112, y - 3, 80, 6, C_PANEL, 255); fc_rect(112, y - 3, 80 * cr.score[i] / 100, 6, cr.score[i] >= 60 ? C_GOLD : C_PURPLE, 255);
      TF(&F_MONO14, 226, y, C_WHITE, AL_R, "%d", cr.score[i]);
    }
    if (cr_new_best) T(&F_SANS16, "新纪录！拍照记录成绩", 120, 282, C_GOLD, AL_C);
    else TF(&F_SANS16, 120, 282, C_DIM, AL_C, "最佳 %d 分", CIRCUS_BEST);
    H[0].label = "再来一局";
    break;
  }
  }
  hintK(H, 1, true);
}

const sl_scene_t SC_CIRCUS = {.id = "circus", .name = "声音挑战赛", .en = "4 STAGES", .ic = "♫", .mic = NM_F_PITCH,
  .enter = ci_enter, .leave = ci_leave, .down = ci_down, .ok = ci_ok, .update = ci_update, .draw = ci_draw};

void sl_circus_status(char *out, size_t n) {
  if (sl_ui_scene() != &SC_CIRCUS) { if (n) out[0] = 0; return; }
  snprintf(out, n, "circus phase=%u stage=%u t=%d scores=%u/%u/%u/%u total=%u best=%u", cr.phase, cr.stage, (int)cr.t,
           cr.score[0], cr.score[1], cr.score[2], cr.score[3], cr.total, CIRCUS_BEST);
}
