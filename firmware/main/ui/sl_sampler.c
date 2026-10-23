#include "sl_sampler.h"
#include "sl_audio.h"
#include <stdlib.h>
#include <string.h>
static int16_t *pcm;
static int length,rec,playing,half,previous,peak,gain;
static uint32_t position,step,age;
bool sl_sampler_open(void) {
  sl_audio_lock(); if(!pcm)pcm=(int16_t*)malloc(SL_SAMPLE_MAX*sizeof(int16_t));
  length=rec=playing=half=peak=0;bool ok=pcm!=NULL;sl_audio_unlock();return ok;
}
void sl_sampler_close(void) { sl_audio_lock();rec=playing=length=0;free(pcm);pcm=NULL;sl_audio_unlock(); }
void sl_sampler_begin(void) { sl_audio_lock(); if(pcm){length=peak=playing=half=0;rec=1;}sl_audio_unlock(); }
void sl_sampler_push(const int16_t *x,int n) {
  sl_audio_lock();
  if(rec&&pcm)for(int i=0;i<n&&length<SL_SAMPLE_MAX;i++) {
    if(!half){previous=x[i];half=1;continue;}
    int v=(previous+x[i])/2;pcm[length++]=(int16_t)v;half=0;
    int a=v<0?-v:v;if(a>peak)peak=a;
  }
  if(length==SL_SAMPLE_MAX)rec=0;
  sl_audio_unlock();
}
int sl_sampler_end(void) { sl_audio_lock();rec=0;half=0;if(length<320||peak<64)length=0;gain=peak>0?12000*4096/peak:4096;if(gain<512)gain=512;if(gain>16384)gain=16384;int n=length;sl_audio_unlock();return n; }
void sl_sampler_cancel(void) {sl_audio_lock();rec=playing=length=half=0;sl_audio_unlock();}
bool sl_sampler_recording(void){sl_audio_lock();bool v=rec!=0;sl_audio_unlock();return v;}
int sl_sampler_length(void){sl_audio_lock();int n=length;sl_audio_unlock();return n;}
bool sl_sampler_playing(void){sl_audio_lock();bool v=playing!=0;sl_audio_unlock();return v;}
bool sl_sampler_play(int key) {
  static const uint32_t rates[3]={32768,41285,49097};
  sl_audio_lock();bool ok=pcm&&length>=320&&!rec;
  if(ok){step=rates[key<0?0:key>2?2:key];position=age=0;playing=1;}
  sl_audio_unlock();return ok;
}
void sl_sampler_mix(int32_t *acc,int n) {
  if(!playing||!pcm||rec)return;
  for(int i=0;i<n;i++) {
    uint32_t at=position>>16;if(at+1>=(uint32_t)length){playing=0;break;}
    int32_t a=pcm[at],b=pcm[at+1];
    int32_t v=a+(int32_t)(((int64_t)(b-a)*(position&65535))>>16);
    int env=age<64?(int)(age*512):32768;
    uint32_t remaining=((uint32_t)length*65536-position)/step;
    if(remaining<64&&env>(int)remaining*512)env=(int)remaining*512;
    v=(int32_t)((int64_t)v*gain*env/(32768LL*4096));
    /* One voice, capped headroom; final mixer remains responsible for saturation. */
    if(v>18000)v=18000;
    if(v<-18000)v=-18000;
    acc[i]+=v;
    position+=step;age++;
  }
}
