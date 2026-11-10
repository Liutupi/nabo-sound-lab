/* 声音挑战赛（四关连玩）的纯规则：整数运算，不碰屏幕、声音、内存分配。
 * 浏览器对应实现：prototype/circus-model.js，两边同种子、同输入必须逐步相同。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "newgames_model.h"

enum { CR_TITLE, CR_INTRO, CR_PLAY, CR_SCORE, CR_FINAL };          /* phase */
enum { CR_CANDLE, CR_ROPE, CR_FLIGHT, CR_CLAP, CR_STAGES };        /* stage */
enum { CR_EV_PLAY = 1, CR_EV_SCORE = 2, CR_EV_FINAL = 4 };         /* cr_step / cr_ok / cr_skip 返回的事件位 */

#define CR_CANDLES 6
#define CR_INTRO_MS 2600
#define CR_SCORE_MS 2400
#define CR_CANDLE_LIMIT_MS 12000
#define CR_ROPE_LIMIT_MS 14000
#define CR_ROPE_WALK_MS 6000
#define CR_FLIGHT_WAIT_MS 8000
#define CR_CLAP_BPM 96

typedef struct {
  uint16_t blow;        /* 风力 0..1000，0 = 没在吹 */
  int16_t midi100;      /* 音高 MIDI×100，voiced 为 false 时忽略 */
  bool voiced;
  int32_t clap_ms;      /* 拍手关：音频时钟上从本关开始算的毫秒（已做延迟补偿） */
} cr_in_t;

typedef struct {
  uint32_t seed;
  int32_t t;                         /* 当前阶段已过的毫秒 */
  uint16_t carry, total;
  uint8_t phase, stage, score[CR_STAGES];
  /* 吹蜡烛 */
  int16_t life[CR_CANDLES]; uint8_t lit; int32_t blow_t0, blow_ms;
  /* 走钢丝 */
  int16_t target, cents; uint8_t lock; uint16_t prog, dev_n; int32_t dev_sum;
  /* 哼歌飞行 */
  ng_flight_t flight; uint16_t hum_ms;
  /* 拍手节奏 */
  ng_clap_t clap;
} cr_t;

void cr_init(cr_t *s, uint32_t seed);
int cr_ok(cr_t *s);                              /* OK：开始 / 跳过说明 / 下一关 / 再来一局 */
int cr_skip(cr_t *s);                            /* ▼：按当前成绩结束本关 */
int cr_step(cr_t *s, int dt, const cr_in_t *in);
int cr_clap_hit(cr_t *s, int onset_ms);          /* 同 ng_clap_hit；不在拍手关时返回 0 */
int cr_notes(int total);                         /* 1..5 个音符 */
int cr_rank(int total);                          /* 0..3，越大越好 */
