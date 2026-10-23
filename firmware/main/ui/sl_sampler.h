/* Single 8kHz/16-bit sample <=0.5s, 8000B only while this scene is open.
 * Commands lock sl_audio_lock. mix is called INSIDE the existing audio lock. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SL_SAMPLE_MAX 4000
bool sl_sampler_open(void);
void sl_sampler_close(void);
void sl_sampler_begin(void);
void sl_sampler_push(const int16_t *input16k,int n);
int sl_sampler_end(void);
void sl_sampler_cancel(void);
bool sl_sampler_recording(void);
int sl_sampler_length(void);
bool sl_sampler_play(int key);  /* key=0/1/2 => recorded pitch / +4 / +7 semitones */
bool sl_sampler_playing(void);
void sl_sampler_mix(int32_t *acc,int n); /* Never locks recursively. */
