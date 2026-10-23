/* 玩 · 声音玩具：喊叫 NABO、吹蜡烛、走钢丝、声部卡（原型 SHOUT / CANDLE / ROPE / RANGE） */
#include "sl_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 喊叫 NABO：小声走，大声跳 =================
 * 位置、速度都用 Q8 像素；速度单位 像素/秒。规则与原型 SHOUT.step 一致。 */
typedef struct { int32_t x0, x1, h; } seg_t;
static seg_t sh_seg[24]; static int sh_n;
static int32_t sh_x, sh_y, sh_vy; static bool sh_ground; static int sh_state, sh_m, sh_sv, sh_dist; static bool sh_new;
static uint32_t sh_rng;
static uint32_t sh_jump_at, sh_apex_at, sh_land_at, sh_fall_at, sh_over_at; static bool sh_in_jump;
static const int SH_SENS[4] = {0, 6, 12, -6};
enum { SH_READY, SH_RUN, SH_OVER };
static uint32_t sh_rand(void) { sh_rng += 0x6D2B79F5u; uint32_t t = sh_rng; t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return t ^ (t >> 14); }
static int sh_r1000(void) { return (int)(sh_rand() % 1000); }
static void sh_gen(void) {
  bool hard = sh_m == 1;
  while (sh_seg[sh_n - 1].x1 < (sh_x >> 8) + 420 && sh_n < 24) {
    seg_t L = sh_seg[sh_n - 1];
    int g = (hard ? 46 : 28) + sh_r1000() * (hard ? 48 : 36) / 1000, len = (hard ? 50 : 80) + sh_r1000() * (hard ? 90 : 110) / 1000;
    static const int dh[4] = {-14, 0, 0, 14};
    int h = fx_clamp(L.h + dh[sh_rand() % 4], 218, 256);
    sh_seg[sh_n].x0 = L.x1 + g; sh_seg[sh_n].x1 = L.x1 + g + len; sh_seg[sh_n].h = h; sh_n++;
  }
  while (sh_n > 2 && sh_seg[1].x1 < (sh_x >> 8) - 200) { memmove(sh_seg, sh_seg + 1, sizeof(seg_t) * (size_t)(sh_n - 1)); sh_n--; }
}
static const seg_t *sh_at(int x) { for (int i = 0; i < sh_n; i++) if (x >= sh_seg[i].x0 && x <= sh_seg[i].x1) return &sh_seg[i]; return NULL; }
static void sh_start(void) {
  sh_rng = VT * 2654435761u + 1; sh_x = 40 << 8; sh_y = 240 << 8; sh_vy = 0; sh_ground = true;
  sh_seg[0] = (seg_t){-200, 300, 240}; sh_n = 1; sh_state = SH_RUN; sh_gen();
  sh_jump_at = sh_apex_at = sh_land_at = sh_fall_at = sh_over_at = 0; sh_in_jump = false;
}
static int sh_lv(void) { return MIC.db_s_x100 / 100 + SH_SENS[sh_sv]; }
static void sh_update(int dt) {
  if (sh_state != SH_RUN) return;
  if (dt > 50) dt = 50;
  int lv = sh_lv(), sp = lv > -48 ? fx_clamp((lv + 48) * 1000 / 22, 0, 1000) * 95 / 1000 : 0;
  int32_t nx = sh_x + sp * 256 * dt / 1000; const seg_t *ah = sh_at((nx >> 8) + 8);
  if (!(ah && ah->h < (sh_y >> 8) - 16)) sh_x = nx;      /* 前面高出 16 像素以上就挡住，要跳 */
  if (sh_ground && lv > -24) { sh_vy = -(230 + fx_clamp(lv + 24, 0, 20) * 9) * 256; sh_ground = false; sh_in_jump = true; sh_jump_at = VT; sh_apex_at = sh_land_at = 0; }
  const seg_t *s = sh_at(sh_x >> 8);
  if (sh_ground) { if (!s || s->h > (sh_y >> 8)) { sh_ground = false; sh_in_jump = false; sh_fall_at = VT; sh_land_at = 0; } else sh_y = s->h << 8; }
  if (!sh_ground) {
    int32_t py = sh_y, vy = sh_vy; sh_vy += 640 * 256 * dt / 1000; sh_y += sh_vy * dt / 1000;
    if (sh_in_jump && vy < 0 && sh_vy >= 0) sh_apex_at = VT;
    const seg_t *s2 = sh_at(sh_x >> 8);
    if (s2 && sh_vy > 0 && py <= (s2->h + 2) << 8 && sh_y >= s2->h << 8) { sh_y = s2->h << 8; sh_vy = 0; sh_ground = true; sh_land_at = VT; }
    if (!sh_ground && sh_in_jump && sh_vy > 0 && sh_y > (264 << 8)) { sh_in_jump = false; sh_fall_at = VT; }
  }
  if (sh_y > 380 << 8) {
    sh_state = SH_OVER; sh_over_at = VT; sh_dist = (sh_x >> 8) / 10; sh_new = sh_dist > REC.shout_best;
    if (sh_new) { REC.shout_best = (int16_t)sh_dist; sl_settings_save(); }
  }
  sh_gen();
}
static void sh_up(void) { sh_sv = (sh_sv + 1) % 4; char b[24]; snprintf(b, sizeof b, "灵敏度 %+d dB", SH_SENS[sh_sv]); toast(b); }
static void sh_down(void) { if (sh_state == SH_RUN) return; sh_m ^= 1; toast(sh_m ? "困难" : "普通"); }
static void sh_ok(void) { if (sh_state != SH_RUN) sh_start(); }
static void sh_leave(void) { if (sh_state == SH_RUN) sh_state = SH_READY; }
static void sh_draw(void) {
  header("喊叫 NABO", sh_m ? "困难" : "普通");
  int cam = (sh_x >> 8) - 70;
  for (int i = 0; i < 30; i++) { int sx = ((i * 53 - cam * 3 / 10) % 260 + 260) % 260 - 10, sy = 36 + (i * 37) % 130; fc_rect(sx, sy, 2, 2, C_LINE, 255); }
  if (sh_state != SH_READY) for (int i = 0; i < sh_n; i++) {
    int x0 = sh_seg[i].x0 - cam, x1 = sh_seg[i].x1 - cam; if (x1 < 0 || x0 > 240) continue;
    const fc_img_t *top = prop("ground-top");
    fc_rect(x0, sh_seg[i].h, x1 - x0, 300 - sh_seg[i].h, RGB(0x24, 0x19, 0x3F), 255);
    if (top) for (int x = x0; x < x1; x += top->w) fc_img_part(top, 0, 0, 0, x + top->w > x1 ? x1 - x : top->w, top->h, x, sh_seg[i].h);
    else fc_rect(x0, sh_seg[i].h, x1 - x0, 3, C_PURPLE, 255);
  }
  /* 左边的音量计：两条线分别是“走”和“跳” */
  int lv = sh_lv(), my = 40, mh = 200, mx = 6;
#define FY(v) (my + mh - fx_clamp(((v) + 70) * mh / 70, 0, mh))
  fc_rect(mx, my, 8, mh, C_PANEL, 255); fc_rect(mx, FY(lv), 8, my + mh - FY(lv), lv > -24 ? C_MAG : lv > -48 ? C_CYAN : C_DARK, 255);
  fc_rect(mx - 2, FY(-48), 12, 1, C_CYAN, 255); fc_rect(mx - 2, FY(-24), 12, 1, C_MAG, 255);
  T(&F_SANS14, "跳", mx + 15, FY(-24), C_MAG, AL_L); T(&F_SANS14, "走", mx + 15, FY(-48), C_CYAN, AL_L);
#undef FY
  if (sh_state == SH_READY) {
    nabo("S-SHOUT", 76, 128, 196, (int)VT);
    T(&F_DISP24, "小声走 · 大声跳", 128, 100, C_WHITE, AL_C);
    T(&F_SANS14, "OK 开始，然后对着工牌出声", 128, 128, C_DIM, AL_C);
    TF(&F_MONO14, 128, 262, C_GOLD, AL_C, "BEST %d m", REC.shout_best);
  } else {
    int px = (sh_x >> 8) - cam, py = sh_y >> 8;
    if (!sh_ground && !sh_in_jump) nabo_once("S-FALL-R", 56, px, py - 28, (int)(VT - sh_fall_at));
    else if (!sh_ground) {
      int ms = nabo_ms("S-JUMP-R"), phase;
      if (sh_vy < 0) phase = fx_clamp((int)(VT - sh_jump_at), 0, ms / 2);
      else phase = ms / 2 + fx_clamp((int)(VT - sh_apex_at), 0, ms / 4);
      nabo_once("S-JUMP-R", 56, px, py - 28, phase);
    }
    else if (sh_land_at && VT - sh_land_at < (uint32_t)nabo_ms("S-JUMP-R") / 4) nabo_once("S-JUMP-R", 56, px, py - 28, nabo_ms("S-JUMP-R") * 3 / 4 + (int)(VT - sh_land_at));
    else { int sp = lv > -48 ? fx_clamp((lv + 48) * 1000 / 22, 0, 1000) : 0; static uint32_t run_t; run_t += (uint32_t)(sp * 33 / 1000); nabo("S-RUN-R", 56, px, py - 28, sp ? (int)run_t : 0); }
    TF(&F_MONO22, 232, 40, C_WHITE, AL_R, "%dm", (sh_x >> 8) / 10);
    if (sh_state == SH_OVER) {
      rr(40, 92, 160, 96, 10, RGB(0, 0, 0));
      T(&F_DISP24, "掉下去了！", 120, 114, C_MAG, AL_C);
      TF(&F_MONO22, 120, 144, C_WHITE, AL_C, "%d m", sh_dist);
      if (sh_new) T(&F_SANS14, "新纪录！", 120, 164, C_GOLD, AL_C);
      nabo_once("S-OOPS", 56, 186, 160, (int)(VT - sh_over_at));
      T(&F_SANS14, "OK 再来一局", 120, 180, C_DIM, AL_C);
    }
  }
  hint_t H[] = {{"▲", "灵敏度"}, {"▼", "难度"}, {sh_state == SH_RUN ? NULL : "OK", "开始"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_SHOUT = {.id = "shout", .name = "喊叫 NABO", .en = "SHOUT", .ic = "!", .mic = 0, .up = sh_up, .down = sh_down, .ok = sh_ok, .leave = sh_leave, .update = sh_update, .draw = sh_draw};

/* ================= 吹蜡烛 ================= */
static int cd_n = 5, cd_m; static int16_t cd_life[30]; static bool cd_lit[30]; static uint32_t cd_t0, cd_done; static int cd_sec10; static bool cd_new;
static struct { int16_t x, y; uint32_t t; } cd_smoke[30]; static int cd_ns;
static void cd_init(void) { for (int i = 0; i < cd_n; i++) { cd_lit[i] = true; cd_life[i] = (int16_t)(500 + ((i * 7) % 5) * 150); } cd_t0 = cd_done = 0; cd_ns = 0; particles_clear(); }
static void cd_pos(int i, int *x, int *y) { int per = 10, row = i / per, inr = cd_n - row * per < per ? cd_n - row * per : per, k = i % per; *x = 140 + (2 * k - (inr - 1)) * 7; *y = 198 - row * 9; }
static int cd_blow(void) {     /* 音量够大 + 没有音高 + 低频能量高：返回风力 0..1000 */
  int low = (MIC.bands[0] + MIC.bands[1] + MIC.bands[2] + MIC.bands[3] + MIC.bands[4]) / 5;
  bool voiced = MIC.f0_x10 && MIC.f0_conf_x1000 > 850;
  return MIC.db_x100 > -3600 && !voiced && low > 115 ? fx_clamp((MIC.db_x100 + 3600) * 1000 / 1800, 150, 1000) : 0;
}
static void cd_song(void) {    /* 生日歌（旋律已进入公有领域），拨弦合成 */
  static const uint8_t M[] = {67, 67, 69, 67, 72, 71, 67, 67, 69, 67, 74, 72, 67, 67, 79, 76, 72, 71, 69, 77, 77, 76, 72, 74, 72};
  static const uint8_t D4[] = {3, 1, 4, 4, 4, 8, 3, 1, 4, 4, 4, 8, 3, 1, 4, 4, 4, 4, 8, 3, 1, 4, 4, 4, 8};   /* 四分之一拍 */
  int at = 0; for (unsigned i = 0; i < sizeof M; i++) { sl_pluck(M[i], at, 35, 1200); at += D4[i] * 100; }
}
static void cd_up(void) { cd_n = cd_n >= 30 ? 1 : cd_n + 1; cd_init(); char b[16]; snprintf(b, sizeof b, "%d 根蜡烛", cd_n); toast(b); }
static void cd_down(void) { cd_m ^= 1; cd_init(); toast(cd_m ? "计时挑战" : "生日"); }
static void cd_ok(void) { cd_init(); toast("重新点上"); }
static void cd_enter(void) { cd_init(); }
static void cd_update(int dt) {
  int bs = cd_done ? 0 : cd_blow();
  if (bs) {
    if (!cd_t0) cd_t0 = VT;
    for (int i = 0; i < cd_n; i++) if (cd_lit[i]) {
      cd_life[i] = (int16_t)(cd_life[i] - bs * dt * 15 / 10000);
      if (cd_life[i] <= 0) { cd_lit[i] = false; int x, y; cd_pos(i, &x, &y); if (cd_ns < 30) { cd_smoke[cd_ns].x = (int16_t)x; cd_smoke[cd_ns].y = (int16_t)(y - 24); cd_smoke[cd_ns].t = VT; cd_ns++; } }
    }
  }
  bool all = true; for (int i = 0; i < cd_n; i++) all &= !cd_lit[i];
  if (!cd_done && all && cd_t0) {
    cd_done = VT; cd_sec10 = (int)(VT - cd_t0) / 100; confetti(60);
    if (cd_m == 1) { cd_new = !REC.candle_best_ms || (int32_t)(VT - cd_t0) < REC.candle_best_ms; if (cd_new) { REC.candle_best_ms = (int32_t)(VT - cd_t0); sl_settings_save(); } }
    else cd_song();
  }
}
static void cd_draw(void) {
  header("吹蜡烛", cd_m ? "计时挑战" : "生日");
  int bs = cd_done ? 0 : cd_blow();
  const fc_img_t *cake = prop("cake");
  if (cake) fc_img(cake, 0, 140 - cake->w / 2, 296 - cake->h, 255);
  else {
    fc_rect(40, 236, 200, 56, RGB(0xFF, 0x7E, 0xB6), 255); fc_rect(66, 198, 148, 40, RGB(0xFF, 0xE7, 0xC2), 255);
    for (int x = 66; x < 214; x += 12) fc_disc(x + 6, 198, 6, RGB(0xFF, 0xF5, 0xE6), 255);
    for (int x = 40; x < 240; x += 14) fc_disc(x + 7, 236, 7, RGB(0xFF, 0xE7, 0xC2), 255);
    for (int x = 52; x < 236; x += 20) fc_disc(x, 268, 3, C_MAG, 255);
  }
  for (int i = cd_n - 1; i >= 0; i--) {
    int x, y; cd_pos(i, &x, &y);
    const fc_img_t *cn = prop(i % 2 ? "candle-b" : "candle-a");
    if (cn) fc_img(cn, 0, x - cn->w / 2, y - cn->h, 255);
    else { fc_rect(x - 2, y - 20, 5, 20, i % 2 ? C_CYAN : C_WHITE, 255); fc_rect(x - 2, y - 14, 5, 2, C_MAG, 255); fc_rect(x - 2, y - 7, 5, 2, C_MAG, 255); }
    if (cd_lit[i]) {
      int lean = bs * 8 / 1000 * (SET.calm ? 1 : 2) / 2, fl = SET.calm ? 0 : fx_sin((int)(VT * 1024 / 345) + i * 212) * 14 / 16384, h = 9 + fl / 10 - bs * 3 / 1000;
      fc_ellipse(x + lean, y - 26, 3, h * 10 / 16 + 1, C_GOLD, 255); fc_ellipse(x + lean * 7 / 10, y - 25, 1, h * 10 / 32, C_WHITE, 255);
    }
  }
  for (int i = 0; i < cd_ns; i++) { int d = (int)(VT - cd_smoke[i].t); if (d >= 1000) continue; fc_disc(cd_smoke[i].x, cd_smoke[i].y - d * 30 / 1000, 2 + d * 4 / 1000, mix565(C_DIM, C_BG, d), 255); }
  int left = 0; for (int i = 0; i < cd_n; i++) left += cd_lit[i];
  if (cd_done) {
    int dt = (int)(VT - cd_done);
    if (cd_m == 0) { int ch = nabo_ms("S-CHEER"); if (dt < ch) nabo_once("S-CHEER", 76, 40, 72, dt); else nabo("S-SING-BDAY", 76, 40, 72, dt - ch); T(&F_DISP24, "生日快乐！", 146, 70, C_GOLD, AL_C); }
    else { nabo_once("S-THUMB", 76, 40, 72, dt); TF(&F_MONO22, 146, 62, C_WHITE, AL_C, "%d.%d s", cd_sec10 / 10, cd_sec10 % 10); if (cd_new) T(&F_SANS14, "新纪录！", 146, 88, C_GOLD, AL_C); else TF(&F_SANS14, 146, 88, C_GOLD, AL_C, "最佳 %d.%d 秒", (int)(REC.candle_best_ms / 1000), (int)(REC.candle_best_ms / 100 % 10)); }
  } else {
    if (bs) nabo("S-BLOW", 76, 40, 72, (int)VT); else nabo("S-CAKE-WAIT", 76, 40, 72, (int)VT);
    if (cd_t0) TF(&F_SANS16, 146, 60, C_WHITE, AL_C, "还剩 %d 根", left); else T(&F_SANS16, "对着工牌用力吹", 146, 60, C_WHITE, AL_C);
    if (cd_m == 1) { if (cd_t0) TF(&F_MONO14, 146, 84, C_GOLD, AL_C, "%d.%d s", (int)(VT - cd_t0) / 1000, (int)(VT - cd_t0) / 100 % 10); else T(&F_SANS14, "计时挑战", 146, 84, C_GOLD, AL_C); }
    fc_rect(118, 104, 104, 5, C_PANEL, 255); fc_rect(118, 104, 104 * bs / 1000, 5, bs ? C_CYAN : C_DARK, 255); T(&F_SANS14, "风力", 112, 106, C_DIM, AL_R);
  }
  char nb[16]; snprintf(nb, sizeof nb, "%d 根", cd_n);
  hint_t H[] = {{"▲", nb}, {"▼", "模式"}, {"OK", "重新点"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_CANDLE = {.id = "candle", .name = "吹蜡烛", .en = "BLOW", .ic = "♨", .mic = NM_F_PITCH, .enter = cd_enter, .up = cd_up, .down = cd_down, .ok = cd_ok, .update = cd_update, .draw = cd_draw};

/* ================= 走钢丝：唱长音，跑调就歪，抖了就晃 ================= */
enum { RP_READY, RP_REF, RP_WAIT, RP_WALK, RP_DONE };
static int rp_state, rp_m, rp_ti = 1, rp_target = -1, rp_cs, rp_tilt, rp_bad, rp_score, rp_mA, rp_sd, rp_result, rp_prog;  /* prog 千分比；cs 音分×10 */
static int16_t rp_cents[600]; static int rp_nc; static int16_t rp_acc[8]; static int rp_na; static uint32_t rp_t, rp_seen;
static const int RP_TOL[3] = {50, 30, 15};
static const char *RP_TN[3] = {"容易", "普通", "困难"};
static void rp_reset(void) { rp_target = -1; rp_cs = rp_tilt = rp_bad = rp_prog = 0; rp_nc = rp_na = 0; }
static void rp_finish(int kind) {
  rp_state = RP_DONE; rp_result = kind; rp_t = VT;
  int n = rp_nc ? rp_nc : 1, sa = 0, s = 0; for (int i = 0; i < rp_nc; i++) { sa += abs(rp_cents[i]); s += rp_cents[i]; }
  rp_mA = sa / n / 10; int mu = s / n; long long v = 0; for (int i = 0; i < rp_nc; i++) v += (long long)(rp_cents[i] - mu) * (rp_cents[i] - mu);
  int64_t var = v / n; int sd = 0; while ((int64_t)(sd + 1) * (sd + 1) <= var) sd++; rp_sd = sd / 10;
  int tol = RP_TOL[rp_ti], q = fx_clamp(1000 - rp_mA * 1000 * 2 / (tol * 3), 0, 1000);
  rp_score = rp_prog * q / 10000 * (kind == 1 ? 7 : 10) / 10;
  memmove(REC.rope_hist + 1, REC.rope_hist, 11); REC.rope_hist[0] = (int8_t)rp_score; if (REC.rope_n < 12) REC.rope_n++; sl_settings_save();
}
static void rp_up(void) { if (rp_state == RP_WALK) return; rp_ti = (rp_ti + 1) % 3; char b[32]; snprintf(b, sizeof b, "难度：%s ±%d¢", RP_TN[rp_ti], RP_TOL[rp_ti]); toast(b); }
static void rp_down(void) { if (rp_state == RP_WALK) return; rp_m ^= 1; toast(rp_m ? "跟 NABO 唱" : "自由起音"); }
static void rp_ok(void) {
  if (rp_state == RP_WALK || rp_state == RP_REF) return;
  rp_reset();
  if (rp_m) { rp_target = 55 + RI(13); rp_state = RP_REF; rp_t = VT; sl_pluck(rp_target, 0, 50, 1300); } else rp_state = RP_WAIT;
}
static void rp_update(int dt) {
  int tol = RP_TOL[rp_ti]; bool has = MIC.f0_x10 && MIC.f0_conf_x1000 > 800; int m100 = has ? hz10_to_midi100(MIC.f0_x10) : 0;
  if (rp_state == RP_REF && VT - rp_t > 1400) rp_state = RP_WAIT;
  if (rp_state == RP_WAIT && has) {
    if (rp_target < 0) {
      rp_acc[rp_na % 8] = (int16_t)m100; rp_na++;
      if (rp_na >= 8) { int mn = 99999, mx = -99999, s = 0; for (int i = 0; i < 8; i++) { mn = rp_acc[i] < mn ? rp_acc[i] : mn; mx = rp_acc[i] > mx ? rp_acc[i] : mx; s += rp_acc[i]; } if (mx - mn < 60) rp_target = (s / 8 + 50) / 100; }
    }
    if (rp_target >= 0) { rp_state = RP_WALK; rp_seen = VT; }
  }
  if (rp_state == RP_WALK) {
    if (has) {
      rp_seen = VT; rp_cs += ((m100 - rp_target * 100) * 10 - rp_cs) * 4 / 10;
      if (rp_nc < 600) rp_cents[rp_nc++] = (int16_t)rp_cs;
      rp_prog = rp_prog + dt / 10 > 1000 ? 1000 : rp_prog + dt / 10;
      if (abs(rp_cs) > tol * 16) { rp_bad += dt; if (rp_bad > 350) rp_finish(1); } else rp_bad = rp_bad > dt ? rp_bad - dt : 0;
      if (rp_prog >= 1000 && rp_state == RP_WALK) rp_finish(0);
    } else if (VT - rp_seen > 500) rp_finish(2);
  }
  /* 倾斜：0.45 rad × 音分/容差 + 抖动带来的摆动；1 rad ≈ 163 */
  int jit = 0; if (rp_nc > 3) { int n = rp_nc < 12 ? rp_nc : 12; long long v = 0; for (int i = 0; i < n; i++) { int d = rp_cents[rp_nc - 1 - i] - rp_cs; v += (long long)d * d; } int64_t vv = v / n; while ((int64_t)(jit + 1) * (jit + 1) <= vv) jit++; }
  int tt = rp_state == RP_WALK ? fx_clamp(rp_cs * 100 / (tol * 10), -160, 160) * 73 / 100 + (SET.calm ? 0 : fx_sin((int)(VT * 1024 / 565)) * fx_clamp(jit * 100 / (tol * 10), 0, 100) * 41 / 100 / 16384) : 0;
  rp_tilt += (tt - rp_tilt) * 2 / 10;
}
static int rp_bow_hop(uint32_t elapsed) {
  /* body-root-registration-v1.json: remove source-360 hip motion from
   * BOW f4/f5/f6, then restore only its short hop here. JUMP uses physics. */
  static const uint8_t up360[] = {0, 0, 0, 0, 18, 26, 13, 0};
  if (elapsed >= 584) return 0;
  uint32_t phase = elapsed * 12, f = phase / 1000, frac = phase % 1000;
  int up = (int)up360[f] * 1000 + ((int)up360[f + 1] - up360[f]) * (int)frac;
  /* Interpolate before target-76, scene-0.8 scaling; round just once. */
  return -(int)(((int64_t)up * 76 * 800 + 180000000) / 360000000);
}
static void rp_draw(void) {
  char md[32]; snprintf(md, sizeof md, "%s · %s", rp_m ? "跟 NABO 唱" : "自由起音", RP_TN[rp_ti]);
  header("走钢丝", md);
  int tol = RP_TOL[rp_ti];
  if (rp_target >= 0) { char nm[8]; note_name(rp_target, nm); T(&F_MONO22, nm, 120, 44, C_WHITE, AL_C); } else T(&F_MONO22, "?", 120, 44, C_WHITE, AL_C);
  if (rp_state == RP_WALK || rp_state == RP_DONE) TF(&F_MONO14, 120, 64, C_DIM, AL_C, "%+dc", rp_cs / 10);
  const fc_img_t *post = prop("rope-post");
  if (post) { fc_img(post, 0, 14 - post->w / 2, 150, 255); fc_img(post, 0, 226 - post->w / 2, 150, 255); } else { fc_rect(12, 150, 4, 74, C_DIM, 255); fc_rect(224, 150, 4, 74, C_DIM, 255); }
  int px = 14, py = 152;
  for (int k = 1; k <= 24; k++) { int x = 14 + 212 * k / 24, u = k * 1000 / 24, y = 152 + 2 * 22 * u / 1000 * (1000 - u) / 1000; fc_line(px, py, x, y, 2, C_GOLD); px = x; py = y; }
  int rx = 45 + rp_prog * 153 / 1000, u = (rx - 14) * 1000 / 212, ry = 152 + 2 * 22 * u / 1000 * (1000 - u) / 1000, dy = 0, rot = rp_tilt;
  if (rp_state == RP_DONE && rp_result == 1) { int p = (int)(VT - rp_t); dy = p * p / 2000 > 200 ? 200 : p * p / 2000; rot += (rp_tilt >= 0 ? 1 : -1) * (p * 3 > 1500 ? 245 : p * 163 * 3 / 1000); }
  else if (rp_state == RP_DONE && rp_result == 0) dy = rp_bow_hop(VT - rp_t);
  const char *id = rp_state == RP_DONE ? (rp_result == 1 ? "S-ROPE-FALL" : rp_result == 0 ? "S-ROPE-BOW" : "S-ROPE-READY") : rp_state == RP_WALK ? (abs(rp_tilt) > 40 ? "S-ROPE-WOBBLE" : "S-ROPE-WALK") : "S-ROPE-READY";
  fc_gfx_clip(0, 26, 240, 274);
  nabo_sq(id, 76, rx, ry + dy - 38, (int)(rp_state == RP_DONE ? VT - rp_t : VT), 800, 800, rot);
  fc_gfx_clip(0, 0, 0, 0);
  int by = 236; fc_rect(30, by, 180, 10, C_PANEL, 255); int zw = tol * 9 / 10; fc_rect(120 - zw, by, zw * 2, 10, RGB(0x17, 0x3D, 0x2A), 255); fc_rect(119, by - 2, 2, 14, C_WHITE, 255);
  if (rp_state == RP_WALK) { int ax = 120 + fx_clamp(rp_cs / 10, -100, 100) * 9 / 10; fc_rect(ax - 2, by - 3, 5, 16, abs(rp_cs) < tol * 10 ? C_GREEN : C_MAG, 255); }
  T(&F_SANS14, "偏低", 30, by + 20, C_DIM, AL_L); T(&F_SANS14, "偏高", 210, by + 20, C_DIM, AL_R);
  static const char *MSG[] = {"OK 开始，然后唱一个长音", "先听 NABO 唱这个音…", "开口唱吧，NABO 就开始走"};
  if (rp_state <= RP_WAIT) T(&F_SANS16, MSG[rp_state], 120, 96, C_WHITE, AL_C);
  else if (rp_state == RP_WALK) TF(&F_SANS16, 120, 96, C_WHITE, AL_C, "走了 %d%%", rp_prog / 10);
  if (rp_state == RP_DONE) {
    static const char *R[] = {"走到头了！", "掉下去了！", "停下了"}; static const uint16_t RC[] = {C_GREEN, C_MAG, C_GOLD};
    T(&F_DISP24, R[rp_result], 120, 92, RC[rp_result], AL_C);
    TF(&F_MONO22, 120, 274, C_WHITE, AL_C, "%d", rp_score);
    TF(&F_SANS14, 120, 292, C_DIM, AL_C, "%d.%d 秒 · 偏差 %d¢ · 晃动 %d¢", rp_prog / 100, rp_prog / 10 % 10, rp_mA, rp_sd);
  } else {
    T(&F_SANS14, "最近 12 次", 22, 286, C_DIM, AL_L);
    for (int i = 0; i < REC.rope_n; i++) { int h = REC.rope_hist[i] * 3 / 10; if (h < 1) h = 1; fc_rect(220 - i * 14, 296 - h, 10, h, i ? C_PURPLE : C_CYAN, 255); }
  }
  hint_t H[] = {{"▲", "难度"}, {"▼", "模式"}, {rp_state == RP_WALK ? NULL : "OK", "开始"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_ROPE = {.id = "rope", .name = "走钢丝", .en = "HOLD", .ic = "⏤", .mic = NM_F_PITCH, .up = rp_up, .down = rp_down, .ok = rp_ok, .update = rp_update, .draw = rp_draw};

/* ================= 声部卡 ================= */
static int rg_step, rg_low = -1, rg_high = -1, rg_cur = -1; static int16_t rg_acc[15]; static int rg_n; static uint32_t rg_t;
static const struct { const char *n, *prop; int lo, hi; } VT_[6] = {{"男低音", "card-bass", 40, 64}, {"男中音", "card-baritone", 45, 69}, {"男高音", "card-tenor", 48, 72},
                                                                    {"女低音", "card-alto", 53, 77}, {"女中音", "card-mezzo", 57, 81}, {"女高音", "card-soprano", 60, 84}};
static void rg_reset(void) { rg_step = 0; rg_low = rg_high = rg_cur = -1; rg_n = 0; }
static void rg_up(void) { rg_reset(); toast("重新测"); }
static void rg_ok(void) {
  if (rg_step == 0) { if (rg_low >= 0) { rg_step = 1; rg_n = 0; rg_cur = -1; } else toast("先唱一个最低音"); }
  else if (rg_step == 1) { if (rg_high >= 0) { rg_step = 2; rg_t = VT; } else toast("再唱一个最高音"); }
  else rg_reset();
}
static void rg_update(int dt) {
  (void)dt; if (rg_step == 2) return;
  if (MIC.f0_x10 && MIC.f0_conf_x1000 > 850) {
    if (rg_n == 15) memmove(rg_acc, rg_acc + 1, 14 * 2); else rg_n++;
    rg_acc[rg_n - 1] = (int16_t)hz10_to_midi100(MIC.f0_x10);
    if (rg_n == 15) {
      int16_t s[15]; memcpy(s, rg_acc, sizeof s); for (int i = 1; i < 15; i++) { int16_t t = s[i]; int j = i; while (j > 0 && s[j - 1] > t) { s[j] = s[j - 1]; j--; } s[j] = t; }
      if (s[14] - s[0] < 70) { int n = (s[7] + 50) / 100; rg_cur = n; if (rg_step == 0) rg_low = rg_low < 0 ? n : (n < rg_low ? n : rg_low); else rg_high = rg_high < 0 ? n : (n > rg_high ? n : rg_high); }
    }
  } else rg_n = 0;
}
static void rg_keys(int x0, int x1, int y, int lo, int hi, int cur) {   /* 键高 22，音名在 y+32 */
  int a = 36, b = 84, n = b - a + 1;
  for (int m = a; m <= b; m++) {
    int pc = m % 12; bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10, in = lo >= 0 && hi >= 0 && m >= lo && m <= hi;
    int x = x0 + (m - a) * (x1 - x0) / n, w = (x1 - x0) / n;
    fc_rect(x, y, w > 1 ? w - 1 : 1, blk ? 14 : 22, in ? (blk ? RGB(0x1F, 0x8F, 0x84) : C_CYAN) : blk ? RGB(0x1A, 0x14, 0x33) : RGB(0x3A, 0x32, 0x60), 255);
  }
  static const int L[] = {36, 48, 60, 72, 84};
  for (int i = 0; i < 5; i++) { char nm[8]; note_name(L[i], nm); T(&F_MONO14, nm, x0 + (L[i] - a) * (x1 - x0) / n + 2, y + 32, C_DIM, AL_C); }
  if (cur >= 0) { int x = x0 + (fx_clamp(cur, a, b) - a) * (x1 - x0) / n + 2; fc_tri(x - 4, y - 7, x + 4, y - 7, x, y - 1, C_GOLD); }
}
static void rg_draw(void) {
  if (rg_step < 2) {
    bool lowS = rg_step == 0;
    header("声部卡", lowS ? "第 1 步 · 最低音" : "第 2 步 · 最高音");
    T(&F_SANS14, lowS ? "慢慢往下唱，唱到最低还能稳住的音" : "慢慢往上唱，唱到最高还能稳住的音", 120, 42, C_DIM, AL_C);
    char nm[8] = "--"; if (rg_cur >= 0) note_name(rg_cur, nm);
    T(&F_MONO48, nm, 120, 96, rg_cur >= 0 ? C_WHITE : C_DARK, AL_C);
    T(&F_SANS14, MIC.f0_x10 ? "稳住 0.5 秒就记下" : "唱一个音", 120, 130, C_DIM, AL_C);
    int rec = lowS ? rg_low : rg_high; char rs[8] = "--"; if (rec >= 0) note_name(rec, rs);
    TF(&F_SANS16, 120, 160, lowS ? C_CYAN : C_MAG, AL_C, "%s  %s", lowS ? "目前最低" : "目前最高", rs);
    if (!lowS) { char lo[8]; note_name(rg_low, lo); TF(&F_SANS14, 120, 182, C_DIM, AL_C, "最低 %s", lo); }
    rg_keys(12, 228, 206, rg_low, lowS ? rg_low : (rg_high >= 0 ? rg_high : rg_low), rg_cur);
    nabo(lowS ? "S-SING-LOW" : "S-SING-HIGH", 76, 196, 258, (int)VT);
    hint_t H[] = {{"▲", "重测"}, {"OK", lowS ? "下一步" : "出卡片"}};
    hintK(H, 2, true);
    return;
  }
  int lo = rg_low, hi = rg_high > lo ? rg_high : lo, span = hi - lo, mid2 = lo + hi, vi = 0;
  for (int i = 1; i < 6; i++) if (abs(VT_[i].lo + VT_[i].hi - mid2) < abs(VT_[vi].lo + VT_[vi].hi - mid2)) vi = i;
  static const char *TITLE[] = {"精准小钢炮", "稳稳当当", "收放自如", "宽广大音域", "音域大魔王"};
  int ti = span < 12 ? 0 : span < 19 ? 1 : span < 26 ? 2 : span < 32 ? 3 : 4;
  const fc_img_t *frame = prop("card-frame");
  /* 卡面分区（card-frame 220×292 放在 (10,8)）：标题区 y 22–50，主区 53–170，下区 173–270，底饰 271 以下 */
  rr(13, 11, 214, 286, 10, C_PANEL);
  if (frame) fc_img(frame, 0, 10, 8, 255); else rr_frame(10, 8, 220, 292, 14, 2, C_GOLD, C_PANEL);
  T(&F_DISP18, "NABO 声部卡", 120, 31, C_GOLD, AL_C);
  const fc_img_t *por = prop(VT_[vi].prop);
  if (por) fc_img(por, 0, 124, 64, 255); else nabo_once("S-WOW", 76, 176, 110, (int)(VT - rg_t));
  char a[8], b[8]; note_name(lo, a); note_name(hi, b);
  TF(&F_MONO22, 24, 70, C_WHITE, AL_L, "%s-%s", a, b);          /* 左栏文字不超过 x=124，右边是肖像 */
  TF(&F_SANS14, 24, 94, C_DIM, AL_L, "跨度 %d 个半音", span);
  TF(&F_SANS14, 24, 111, C_DIM, AL_L, "%d.%d 个八度", span / 12, span % 12 * 10 / 12);
  T(&F_SANS14, "最接近", 24, 134, C_DIM, AL_L); T(&F_DISP24, VT_[vi].n, 24, 156, C_CYAN, AL_L);
  T(&F_SANS14, "仅供娱乐", 120, 184, C_DARK, AL_C);
  T(&F_SANS14, "NABO 认证", 24, 208, C_DIM, AL_L);
  char st[12]; snprintf(st, sizeof st, "stamp-%d", ti + 1); const fc_img_t *sp = prop(st);
  if (sp) { fc_img_aff(sp, 0, 156, 210, sp->w / 2, sp->h / 2, 0, 320, 320, 255); T(&F_DISP18, TITLE[ti], 156, 210, C_WHITE, AL_C); }   /* 印章底放大 1.25 倍 */
  else TF(&F_DISP18, 156, 208, C_MAG, AL_C, "「%s」", TITLE[ti]);
  rg_keys(32, 208, 232, lo, hi, -1);
  static const hint_t H[] = {{"OK", "重新测"}};
  hintK(H, 1, true);
}
const sl_scene_t SC_RANGE = {.id = "range", .name = "声部卡", .en = "RANGE", .ic = "↕", .mic = NM_F_PITCH, .enter = rg_reset, .up = rg_up, .ok = rg_ok, .update = rg_update, .draw = rg_draw};
