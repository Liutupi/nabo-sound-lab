// nabo_mic：声音实验室的共用麦克风分析（纯 C，不依赖屏幕、声音驱动和 RTOS）。
// 规则以 prototype/index.html 的 analyzeHop() 为准；宿主测试 tests/mic_host 逐项对拍。
// 全部整数运算（ESP32-C3 没有 FPU），只有 nm_init() 里建表时用到浮点。
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NM_SR        16000
#define NM_HOP       320      // 20 ms
#define NM_BANDS     32
#define NM_WF_COLS   120
#define NM_RING      2048
#define NM_ENV       300      // 6 s 起音包络
#define NM_YIN_W     512
#define NM_YIN_TMAX  267      // ceil(16000/60)
#define NM_CHROMA_K0 11       // ceil(80 / 7.8125)
#define NM_CHROMA_K1 192      // 1500 / 7.8125
#define NM_DB_NONE   (-14000) // 静音时的 dB×100

// 按需开关：每个画面只开自己用得到的分析（音量、频段、起音总是开着）
#define NM_F_WF      0x01   // 声纹瀑布行
#define NM_F_PITCH   0x02   // YIN 音高（最贵）
#define NM_F_CHROMA  0x04   // 音级与和弦
#define NM_F_BPM     0x08   // BPM 估计
#define NM_F_ALL     0x0F

typedef struct {
    uint32_t hop;                    // 已处理的帧数（从 1 开始）
    int16_t  db_x100;                // 本帧 RMS，dBFS×100
    int16_t  db_s_x100;              // 快起慢落的平滑 dB
    uint8_t  bands[NM_BANDS];        // 0..255，快起慢落
    uint8_t  peaks[NM_BANDS];        // 0..255，峰值帽
    uint8_t  wf_row[NM_WF_COLS];     // 声纹瀑布新的一行，0..31
    uint8_t  onset;                  // 本帧检测到起音
    uint16_t bpm_x10;                // 0 = 还没有
    uint16_t bpm_conf_x1000;
    uint16_t f0_x10;                 // 音高 Hz×10，0 = 无
    uint16_t f0_conf_x1000;
    uint8_t  chroma[12];             // 12 个音级，0..255
    int8_t   chord;                  // -1 = 无；root*2 + (小三和弦 ? 1 : 0)
    uint16_t chord_conf_x1000;
} nm_result_t;

typedef struct {
    // 采样历史
    int16_t  ring[NM_RING];
    uint16_t wp;
    // FFT 工作区（512 点用前一半），int32 复数，Q15 旋转因子
    int32_t  re[1024], im[1024];
    int16_t  tw_cos[512], tw_sin[512];
    int16_t  win512[512], win1024[1024];
    // 频段与瀑布列对应的 FFT bin 范围
    uint8_t  band_lo[NM_BANDS], band_hi[NM_BANDS];
    uint8_t  col_lo[NM_WF_COLS], col_hi[NM_WF_COLS];
    uint8_t  pc_of_bin[NM_CHROMA_K1];
    // 频段状态（Q12，0..4095）
    uint16_t band_q12[NM_BANDS], peak_q12[NM_BANDS], raw_q12[NM_BANDS];
    // 起音与 BPM
    uint32_t flux_hist[30];
    uint8_t  fh;
    uint32_t env[NM_ENV];
    uint16_t ep;
    uint16_t bpm_w_q12[52];          // 偏好 120 BPM 的权重，下标是滞后帧数
    uint32_t last_onset_hop;
    uint8_t  has_onset;
    // YIN
    uint32_t yd_q16[NM_YIN_TMAX + 2];
    // 音级（Q12）
    uint16_t chroma_q12[12];
    // 当前输出
    nm_result_t out;
    int16_t  sens_db;                // 频谱灵敏度（设置项）
    uint8_t  features;               // NM_F_* 组合
} nm_state_t;

void nm_init(nm_state_t *st, int sens_db);
void nm_set_sens(nm_state_t *st, int sens_db);
void nm_set_features(nm_state_t *st, uint8_t mask);   // 默认 NM_F_ALL
// 送入一帧 320 个采样（16 kHz、单声道）。喇叭发声时调用方送全 0。
const nm_result_t *nm_process(nm_state_t *st, const int16_t *hop);
// 取最近 n 个采样（示波器、录音预卷用），n <= NM_RING
void nm_last(const nm_state_t *st, int16_t *dst, int n);

// 工具函数（宿主测试也会用）
int32_t nm_db10_x100(uint64_t p);   // 1000*log10(p)，p=0 时返回很小的值
uint32_t nm_isqrt64(uint64_t v);

#ifdef __cplusplus
}
#endif
