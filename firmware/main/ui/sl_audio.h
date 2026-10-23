/* 声音输出与复读：拨弦合成（Karplus-Strong）、节拍器咔嗒、复读录放（nabo_voice）的混音。
 * 设备上由音频任务每 20 ms 调一次 sl_audio_fill；界面任务只调下面的命令函数。两边用 sl_audio_lock 互斥。
 * 宿主机上同样可以调 sl_audio_fill 取出波形做测试。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SL_SR 16000

void sl_audio_init(void);
void sl_audio_fill(int16_t *out, int n);         /* 音频任务：混合所有正在响的声音 */
bool sl_audio_busy(void);                        /* 这一帧在发声（再加 10 ms）：音频任务跳过这一帧的麦克风分析，不送全 0（送 0 会在恢复时造成假起音） */
uint32_t sl_audio_time(void);                    /* 已输出的采样数（节奏判定的时钟） */
void sl_audio_stop_all(void);
bool sl_boot_voice_start(void);                 /* FNAS PCM16，直接从Flash读；不分配整段缓冲 */
void sl_boot_voice_stop(void);
bool sl_boot_voice_playing(void);

/* 平台提供（设备：FreeRTOS 互斥量；宿主机：空） */
void sl_audio_lock(void);
void sl_audio_unlock(void);

/* 拨弦：midi 音高，delay_ms 之后开始，amp 0..100，dur_ms 时长（末尾 250 ms 淡出） */
void sl_pluck(int midi, int delay_ms, int amp, int dur_ms);
/* 扫弦：frets[n]（-1 不弹），open[n] 空弦 midi；下扫从第 0 根开始，每根间隔 16 ms */
void sl_strum(const int8_t *frets, const uint8_t *open, int n, bool down, int amp);
/* 咔嗒：at_sample = 0 表示马上，否则在 sl_audio_time() 到达这个值时响 */
void sl_click(bool accent, uint32_t at_sample);
/* 节拍器：bpm = 0 关；每 beats 拍第 1 拍高音 */
void sl_metro(int bpm, int beats);
int  sl_metro_bpm(void);
uint32_t sl_metro_count(void);                   /* 已响的拍数 */

/* 复读 */
void sl_rec_begin(const int16_t *preroll, int n);   /* 预卷采样（16 kHz） */
void sl_rec_push(const int16_t *x, int n);          /* 音频任务：录音时送入麦克风采样 */
int  sl_rec_end(void);                              /* 结束录音，返回 8 kHz 采样数 */
bool sl_rec_on(void);
uint32_t sl_rec_samples16(void);                    /* 已录的 16 kHz 采样数（含预卷） */
int  sl_rec_bytes(void);
void sl_voice_play(int voice);
bool sl_voice_playing(void);
int  sl_voice_level(void);                          /* 最近 20 ms 输出电平 0..255（给 NABO 嘴型） */
