// nabo_voice：复读 NABO 的录音（8 kHz IMA ADPCM）和 12 种变声播放（流式，不展开整段音频）。
// 规则以 prototype/index.html 的 adpcmRoundTrip / voiceGainQ8 / VOICES 为准；宿主测试 tests/voice_host 对拍。
// 全部整数运算；样本按 Q15 处理（32768 = 1.0）。
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV_SR          16000
#define NV_BLOCK       256                       // 8 kHz 采样 / 块
#define NV_MAX_8K      24000                     // 3 秒（含预卷）
#define NV_MAX_BLOCKS  ((NV_MAX_8K + NV_BLOCK - 1) / NV_BLOCK)
#define NV_HIST        1024                      // 调制延迟历史（≥ 2×288+2）
#define NV_ECHO_DL     2880                      // 180 ms
#define NV_EDGE_RAMP   (NV_SR / 200)             // 5 ms, streamed; no extra PCM buffer
#define NV_SPLICE_RAMP (NV_SR * 3 / 1000)        // 3 ms around a stutter repeat
#define NV_LEVEL_BINS   41

enum {
    NV_CHIPMUNK, NV_OLDMAN, NV_ROBOT, NV_REVERSE, NV_ECHO, NV_LOWBAT,
    NV_VIBRATO, NV_ALIEN, NV_WALKIE, NV_CHORUS, NV_STUTTER, NV_ORIGINAL, NV_COUNT
};

typedef struct {
    int16_t pred;          // 块开始时编码器的预测值
    uint8_t idx;           // 块开始时的步长下标
    uint8_t pad;
    uint8_t data[NV_BLOCK / 2];   // 低 4 位在前
} nv_block_t;              // 132 字节

typedef struct {
    nv_block_t blk[NV_MAX_BLOCKS];
    int32_t n8;            // 已编码的 8 kHz 采样数
    int32_t pred, idx;     // 编码器状态
    uint8_t has_pend;
    int32_t peak;          // 解码值的最大绝对值（= 编码器预测值）
    int32_t dc_q16;        // Recording-only DC blocker, ~80 Hz
    int16_t fir[15];       // Anti-alias history; no expanded recording buffer
    uint8_t fir_pos;
    uint64_t level_energy;
    uint16_t level_n, levels[NV_LEVEL_BINS]; // 20 ms decoded RMS histogram
} nv_rec_t;

typedef struct {
    const nv_rec_t *rec;
    int32_t blk[2];        // 两格缓存的块号，-1 = 空
    int16_t buf[2][NV_BLOCK];
    uint8_t lru;
} nv_dec_t;

typedef struct {
    const nv_rec_t *rec;
    int voice;
    int32_t g_q8;          // 播放增益（×256）
    uint32_t L2;           // 2×n8：原速 16 kHz 长度
    uint32_t i, n_out;     // 已输出 / 总长（没电了、卡带结巴是 0：放到自然结束）
    uint8_t done;
    nv_dec_t da, db;
    int32_t hist[NV_HIST]; // 基础流 x 的历史（颤音、外星人、合唱团）
    int32_t echo[NV_ECHO_DL];
    int32_t hp, lp, px;    // 对讲机滤波器
    uint32_t rng;          // mulberry32 状态
    uint64_t p_q32;        // 没电了：读指针（16 kHz 单位，Q32）
    uint32_t seg0, seg_len, seg_u, seg_rep, lcg;   // 卡带结巴
    uint32_t fade_end;     // 没电了：仅尾部预测一次的自然结束位置，不改变 n_out/时长契约
} nv_player_t;

// 录音
void nv_rec_start(nv_rec_t *r);
// 送入 16 kHz 采样；返回接收的个数（满了就少于 n）
int  nv_rec_push(nv_rec_t *r, const int16_t *x16, int n);
static inline int nv_rec_bytes(const nv_rec_t *r) { return (r->n8 + NV_BLOCK - 1) / NV_BLOCK * (int)sizeof(nv_block_t); }
int32_t nv_gain_q8(const nv_rec_t *r);

// 解码（随机访问，两格块缓存）
void    nv_dec_init(nv_dec_t *d, const nv_rec_t *r);
int16_t nv_dec_get(nv_dec_t *d, int32_t i);     // 超出范围返回 0

// 播放
void nv_play_start(nv_player_t *p, const nv_rec_t *r, int voice);
// 填 n 个 16 kHz 采样；返回实际写入数，0 表示放完
int  nv_play_fill(nv_player_t *p, int16_t *out, int n);
// 总长度（采样数）；没电了 / 卡带结巴放完前返回 0
uint32_t nv_play_length(const nv_player_t *p);

#ifdef __cplusplus
}
#endif
