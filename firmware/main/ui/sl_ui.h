/* 声音实验室的场景框架：时间、按键规则、场景切换、顶栏、彩色按键提示、NABO 精灵、提示条、粒子、设置。
 * 和原型 prototype/index.html 一一对应（scr/go/back、press/release、header、hintK、nabo、toast）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../gfx/fc_fx.h"
#include "../gfx/fc_gfx.h"
#include "../gfx/fc_res.h"
#include "../mic/nabo_mic.h"
#include "sl_audio.h"

/* 调色板（原型 C 对象） */
#define C_BG RGB(0x0A, 0x07, 0x16)
#define C_PANEL RGB(0x15, 0x10, 0x30)
#define C_LINE RGB(0x2B, 0x23, 0x50)
#define C_WHITE RGB(0xEC, 0xE8, 0xFF)
#define C_DIM RGB(0x8A, 0x80, 0xB8)
#define C_DARK RGB(0x4A, 0x41, 0x70)
#define C_MAG RGB(0xFF, 0x3D, 0xA6)
#define C_CYAN RGB(0x35, 0xF0, 0xDC)
#define C_GOLD RGB(0xFF, 0xC9, 0x4A)
#define C_GREEN RGB(0x5C, 0xFF, 0x8A)
#define C_RED RGB(0xFF, 0x4D, 0x5E)
#define C_PURPLE RGB(0x8A, 0x5C, 0xFF)
#define C_BLUE RGB(0x3D, 0x8B, 0xFF)

enum { K_UP = 0, K_DOWN = 1, K_OK = 2 };

typedef struct sl_scene {
  const char *id, *name, *en, *ic;      /* ic：菜单图标字符 */
  uint8_t mic;                          /* 需要的 NM_F_* 分析 */
  bool up_now;                          /* ▲ 立刻响应（主菜单），不等双击判断 */
  void (*enter)(void);
  void (*leave)(void);
  void (*up)(void);                     /* ▲ 单击（青色：调） */
  void (*down)(void);                   /* ▼（绿色：换） */
  void (*ok)(void);                     /* OK（白色：做） */
  void (*ok_down)(void);                /* 需要按住 OK 的场景用这两个，代替 ok */
  void (*ok_up)(void);
  bool (*back)(void);                   /* 双击 ▲：返回 true 表示场景自己处理了（二级菜单） */
  void (*update)(int dt);
  void (*draw)(void);
} sl_scene_t;

/* 设置（设备上存 NVS） */
/* volume uses the former trailing padding byte; legacy settings remain 10 B.
 * The platform loads volume from its separate NVS key, never old padding. */
typedef struct { int16_t a4, quiet, cal, sens; uint8_t calm, volume; } sl_settings_t;
extern sl_settings_t SET;
/* 纪录（设备上和设置一起存 NVS） */
typedef struct { int16_t shout_best, ear_best; int32_t candle_best_ms; int8_t rope_hist[12]; uint8_t rope_n; } sl_records_t;
extern sl_records_t REC;
void sl_settings_save(void);            /* 平台实现，默认空：设置和纪录一起存 */
void sl_volume_apply(uint8_t volume);   /* 平台异步应用0-100音量；默认空 */

/* 字体 */
extern fc_font_t F_DISP24, F_DISP18, F_SANS16, F_SANS14, F_MONO14, F_MONO22, F_MONO48, F_DISP56;

extern uint32_t VT;                     /* 界面毫秒 */
/* 麦克风：音频任务每分析完一帧调 sl_mic_hop（只写下面这些缓冲，不碰场景）；界面线程读。 */
#define WF_ROWS 256
#define PH_N 200                        /* 音高历史 4 秒 */
#define DBH_N 500                       /* 音量历史 10 秒 */
extern nm_result_t MIC;                 /* 最新一帧 */
extern bool MIC_LIVE;                   /* 最近 2 秒有声音 */
extern uint8_t WF_BUF[WF_ROWS][NM_WF_COLS];
extern volatile uint16_t WF_HEAD;       /* 最新一行 */
extern volatile bool WF_FROZEN;
extern int16_t PITCH_HIST[PH_N];        /* MIDI×100，0 = 无 */
extern volatile uint16_t PITCH_HEAD;    /* 下一个写入位置 */
extern int16_t DB_HIST[DBH_N];          /* dBFS×100 */
extern volatile uint16_t DB_HEAD;
void sl_mic_hop(const nm_result_t *r, uint32_t t_sample);   /* 音频任务调用 */
void sl_mic_skip(void);                 /* 音频任务：这一帧在发声，跳过分析 */
void sl_mic_last(int16_t *dst, int n);  /* 平台实现：最近 n 个采样（示波器、录音预卷） */
uint32_t sl_mic_samples(void);          /* 实际接收的16kHz采样总数，采样琴去重使用 */
/* 起音：场景挂 g_onset_hook，界面线程在 sl_ui_step 里按顺序回调，参数是起音的采样时刻 */
extern void (*g_onset_hook)(uint32_t t_sample);
int16_t sl_onset_db_x100(void);         /* 当前 hook 事件的音量，不是后来覆盖的 MIC */
uint32_t sl_onset_dropped(void);        /* 16 槽满时丢新事件的累计数，初始化时清零 */
extern uint32_t ONSET_T[16];            /* 最近 16 次起音的采样时刻（拍手测速用） */
extern int ONSET_CNT;

bool sl_ui_init(const uint8_t *blob, uint32_t size, uint32_t seed);
void sl_ui_go(const sl_scene_t *s);
void sl_ui_go_id(const char *id);
void sl_ui_back(void);
const sl_scene_t *sl_ui_scene(void);
void sl_ui_key(int k, bool down);
void sl_ui_step(int dt);
void sl_ui_render(fc_flush_fn flush, void *ctx, bool force);
uint32_t sl_ui_t0(void);
static inline int EL(void) { return (int)(VT - sl_ui_t0()); }
uint8_t sl_ui_mic_features(void);       /* 当前场景要的分析 */

/* 随机数（mulberry32，和原型一致） */
uint32_t rng_u32(void);
static inline int RI(int n) { return n > 0 ? (int)(rng_u32() % (uint32_t)n) : 0; }

/* 画面工具 */
void header(const char *title, const char *mode);
typedef struct { const char *key, *label; } hint_t;
void hintK(const hint_t *h, int n, bool back);   /* key："▲" "▼" "OK" "▲▼" */
void toast(const char *s);
void need_mic(void);
void icon(const sl_scene_t *s, const char *glyph, int cx, int cy, uint16_t c);   /* 菜单图标：prop/icon-<id> → 字形 → 小圆点 */
void T(const fc_font_t *f, const char *s, int x, int y, uint16_t c, int al);
void TF(const fc_font_t *f, int x, int y, uint16_t c, int al, const char *fmt, ...);
void rr(int x, int y, int w, int h, int r, uint16_t c);   /* 圆角矩形（填充） */
void rr_frame(int x, int y, int w, int h, int r, int th, uint16_t c, uint16_t inner);   /* 圆角描边（里面填 inner） */
uint16_t mix565(uint16_t a, uint16_t b, int t1000);       /* a→b，t 千分比 */
extern const uint16_t BARC[32], PC12[12];

/* NABO：中心 (cx,cy)，size 是素材尺寸（120/76/56），k 是缩放千分比（1000 = 原大），t 毫秒 */
void nabo(const char *id, int size, int cx, int cy, int t);
void nabo_once(const char *id, int size, int cx, int cy, int t);           /* 不循环，停在最后一帧 */
void nabo_sq(const char *id, int size, int cx, int cy, int t, int kx, int ky, int ang);   /* 挤压拉伸 / 旋转：脚底中心不动 */
int  nabo_ms(const char *id);
const fc_img_t *prop(const char *name);

/* 粒子（彩带） */
void confetti(int n);
void particles_clear(void);

void sl_cycle_sens(void);               /* 频谱灵敏度 0/+6/+12/−12/−6 */
uint32_t fnv32(const void *p, int n);

/* 折线：每列一个 y（-1 断开），相邻两列之间连起来；用 fc_cb 一次画完，不占显示列表 */
typedef struct { int16_t y[240]; uint16_t c[240]; int16_t x0, n, th; int16_t lo[240]; uint8_t sp[240]; } trace_t;   /* lo/sp 由 trace_draw 算 */
void trace_draw(trace_t *t, int top, int h);

/* 音名 */
extern const char *const NN[12];
void note_name(int midi, char *out);      /* "C4" */
/* Hz×10 → MIDI ×100（以设置里的 A4 为准） */
int hz10_to_midi100(uint32_t hz10);
