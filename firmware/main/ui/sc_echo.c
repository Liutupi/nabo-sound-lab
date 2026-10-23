/* 复读 NABO（原型 ECHO）：按住说 / 自动复读，12 种声音；NABO 的表演跟着声音变 */
#include "sl_ui.h"
#include "../voice/nabo_voice.h"
#include <stdio.h>
#include <stdlib.h>

static const char *VN[NV_COUNT] = {"花栗鼠", "大叔", "机器人", "倒放", "山谷回声", "没电了", "颤音", "外星人", "对讲机", "合唱团", "卡带结巴", "原声"};
static const char *VD[NV_COUNT] = {"快 1.5 倍，音调升高", "慢 0.72 倍，音调降低", "70 Hz 环形调制 + 降位", "把你的话倒过来说", "180 ms 回声，反馈 0.45",
                                   "越说越慢、越说越低，最后没电", "6 Hz 颤音，像在发抖", "升调 + 快速颤音", "窄带 + 失真 + 呲啦声", "三个 NABO 一起说",
                                   "随机重复片段，像卡带", "只经过 8 kHz ADPCM，听听工牌的录音音质"};
enum { E_IDLE, E_REC, E_PLAY };
static int e_v, e_m, e_state, e_bytes; static uint32_t e_t0, e_loud, e_end;
static void e_up(void) { e_v = (e_v + 1) % NV_COUNT; toast(VN[e_v]); }
static void e_down(void) { e_m ^= 1; toast(e_m ? "自动复读" : "按住说"); }
static void e_start(void) {
  static int16_t pre[2400];
  sl_mic_last(pre, 2400);   /* 0.15 秒预卷 */
  sl_rec_begin(pre, 2400); e_state = E_REC; e_t0 = e_loud = VT;
}
static void e_stop(void) {
  if (e_state != E_REC) return;
  sl_rec_end();
  if (sl_rec_samples16() < 4000) { e_state = E_IDLE; if (!e_m) toast("太短了，按住说话"); return; }
  e_bytes = sl_rec_bytes(); sl_voice_play(e_v); e_state = E_PLAY; e_t0 = VT;
}
static void e_ok_down(void) { if (e_m) { toast("自动模式：直接说话就行"); return; } if (e_state == E_IDLE) e_start(); }
static void e_ok_up(void) { if (!e_m) e_stop(); }
static void e_leave(void) { if (e_state == E_REC) sl_rec_end(); sl_audio_stop_all(); e_state = E_IDLE; }
static void e_update(int dt) {
  (void)dt;
  if (e_state == E_REC && VT - e_t0 > 3000) e_stop();
  if (e_state == E_PLAY && !sl_voice_playing()) { e_state = E_IDLE; e_end = VT; }
  if (e_m) {
    if (e_state == E_IDLE && VT - e_end > 400 && !sl_audio_busy() && MIC.db_x100 > -3800) e_start();
    else if (e_state == E_REC) { if (MIC.db_x100 > -4600) e_loud = VT; else if (VT - e_loud > 650) e_stop(); }
  }
}
static void e_draw(void) {
  header("复读 NABO", e_state == E_REC ? "录音中" : e_state == E_PLAY ? "复读中" : e_m ? "自动复读" : "按住说");
  int cx = 120, cy = 140, t = (int)VT;
  if (e_state == E_REC) {
    int p = fx_clamp((int)(VT - e_t0) * 1000 / 3000, 0, 1000), lv = fx_clamp((MIC.db_x100 + 6000) * 1000 / 5000, 0, 1000);
    for (int k = 0; k < 48; k++) if (k * 1000 / 48 <= p) { int a = -256 + k * 1024 / 48, a2 = a + 18; fc_line(cx + (fx_cos(a) * 72 >> 14), cy + (fx_sin(a) * 72 >> 14), cx + (fx_cos(a2) * 72 >> 14), cy + (fx_sin(a2) * 72 >> 14), 4, C_RED); }
    fc_ring(cx, cy, 76 + lv * 24 / 1000, 2, mix565(C_RED, C_BG, 500));
    nabo("S-LISTEN", 120, cx, cy, t);
    TF(&F_MONO14, cx, 224, C_RED, AL_C, "REC %d.%d s", (int)(VT - e_t0) / 1000, (int)(VT - e_t0) / 100 % 10);
  } else if (e_state == E_PLAY) {
    int k = sl_voice_level(), sx = 1000 - k * 60 / 255, sy = 1000 + k * 100 / 255, dx = 0, tt = t;
    for (int r = 0; r < 3; r++) fc_ring(cx, cy, 64 + r * 12 + k * 16 / 255, 2, mix565(C_CYAN, C_BG, 300 + r * 250));
    switch (e_v) {      /* 每种声音 NABO 演得不一样（引擎完成，不另画素材） */
      case NV_CHIPMUNK: tt = t * 3 / 2; sy += 60; sx -= 40; break;
      case NV_OLDMAN: tt = t * 7 / 10; sy -= 80; sx += 60; break;
      case NV_ROBOT: dx = (int)(VT / 60 % 3) - 1; tt = (t / 160) * 160; break;
      case NV_REVERSE: tt = -t; break;
      case NV_VIBRATO: dx = fx_sin((int)(VT * 1024 * 6 / 1000)) * 3 / 16384; break;
      case NV_ALIEN: dx = fx_sin((int)(VT * 1024 * 11 / 1000)) * 2 / 16384; sy += 80; break;
      case NV_STUTTER: tt = ((t / 110) % 3 == 0) ? t - 110 : t; break;
      default: break;
    }
    if (e_v == NV_LOWBAT) nabo_once("S-LOWBAT", 120, cx, cy, (int)(VT - e_t0));
    else if (e_v == NV_CHORUS) { nabo_sq("S-TALK", 76, cx - 66, cy + 22, tt + 300, sx, sy, 0); nabo_sq("S-TALK", 76, cx + 66, cy + 22, tt + 600, sx, sy, 0); nabo_sq("S-TALK", 120, cx, cy, tt, sx, sy, 0); }
    else nabo_sq("S-TALK", 120, cx + dx, cy, tt < 0 ? 100000 + tt : tt, sx, sy, 0);
    if (e_v == NV_WALKIE) for (int y = cy - 60; y < cy + 60; y += 3) fc_rect(cx - 60, y, 120, 1, RGB(0, 0, 0), 120);
    TF(&F_MONO14, cx, 224, C_DIM, AL_C, "ADPCM %d.%d KB", e_bytes / 1024, e_bytes % 1024 * 10 / 1024);
  } else {
    nabo("S-MIC-IDLE", 120, cx, cy, t);
    T(&F_DISP18, e_m ? "直接说话，说完 NABO 就学" : "按住 OK 说一句话", cx, 224, C_GOLD, AL_C);
  }
  rr(16, 240, 208, 54, 10, C_PANEL);
  T(&F_SANS14, "◀", 30, 258, C_DIM, AL_C); T(&F_SANS14, "▶", 210, 258, C_DIM, AL_C);
  T(&F_DISP18, VN[e_v], cx, 258, C_CYAN, AL_C);
  T(&F_SANS14, VD[e_v], cx, 280, C_DIM, AL_C);
  hint_t H[] = {{"▲", "声音"}, {"▼", e_m ? "自动" : "按住"}, {e_m ? NULL : "OK", "录音"}};
  hintK(H, 3, true);
}
const sl_scene_t SC_ECHO = {.id = "echo", .name = "复读 NABO", .en = "ECHO NABO", .ic = "◉", .mic = 0, .up = e_up, .down = e_down,
                            .ok_down = e_ok_down, .ok_up = e_ok_up, .leave = e_leave, .update = e_update, .draw = e_draw};
