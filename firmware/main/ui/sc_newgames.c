/* Three complete 0.1.2 toys. Only existing NABO sprites; no new artwork. */
#include "sc_newgames.h"
#include "newgames_model.h"
#include "sl_sampler.h"
#include <stdio.h>
#include <string.h>
/* Platform input-ring count, not output/audio analysis hop count. */
extern uint32_t sl_mic_samples(void);
static void footer(const char *a) {
  fc_rect(0,260,240,60,C_PANEL,255);
  T(&F_SANS16,a,120,276,C_WHITE,AL_C);
  T(&F_SANS16,"▲▲ 返回",120,306,C_DIM,AL_C);
}
static bool voiced(void) { return MIC.f0_x10 && MIC.f0_conf_x1000>=800 && MIC.db_x100>-6000; }
/* ---------------- hum flight ---------------- */
static ng_flight_t flight;
static uint8_t flight_difficulty;
static void fl_enter(void) { ng_flight_init(&flight,rng_u32(),flight_difficulty); }
static void fl_up(void) { if(flight.state==NG_READY||flight.state==NG_OVER||flight.state==NG_WIN){flight_difficulty=(flight_difficulty+1)%3;fl_enter();} }
static void fl_down(void) { fl_enter(); }
static void fl_ok(void) {
  if(flight.state==NG_RUN){flight.state=NG_PAUSE;return;}
  if(flight.state==NG_PAUSE){flight.state=NG_RUN;return;}
  if(!voiced()){toast("先哼一个音，再按OK");return;}
  if(flight.state!=NG_READY)fl_enter();
  ng_flight_start(&flight,hz10_to_midi100(MIC.f0_x10));
}
static void fl_update(int dt) { ng_flight_step(&flight,dt,voiced()?hz10_to_midi100(MIC.f0_x10):0,voiced()); }
static void fl_draw(void) {
  static const char *ds[]={"轻松","标准","挑战"}; header("哼歌飞行",ds[flight.difficulty]);
  TF(&F_SANS16,10,43,C_GOLD,AL_L,"得分 %d",flight.score);
  TF(&F_SANS16,230,43,C_MAG,AL_R,"生命 %d",flight.lives);
  fc_gfx_clip(0,60,240,190);
  for(int i=0;i<4;i++) {
    int x=flight.gate[i].xq/256,gap=ng_flight_gap(&flight),a=flight.gate[i].y-gap/2,b=a+gap;
    fc_rect(x,60,16,a-60,C_PURPLE,255);fc_rect(x,b,16,250-b,C_PURPLE,255);
    fc_rect(x,a-3,16,3,C_CYAN,255);fc_rect(x,b,16,3,C_CYAN,255);
  }
  nabo("S-FLOAT",76,54,flight.yq/256,(int)VT);
  if(flight.flash_ms)fc_frame(2,62,236,186,2,C_RED);
  fc_gfx_clip(0,0,0,0);
  if(flight.state==NG_READY) {
    rr(8,100,224,112,10,C_PANEL);
    T(&F_SANS16,"哼一个舒服的音",120,123,C_WHITE,AL_C);
    T(&F_SANS16,"高音向上，低音向下",120,151,C_CYAN,AL_C);
    T(&F_SANS16,"穿过12道门，三次机会",120,180,C_DIM,AL_C);
  } else if(flight.state==NG_PAUSE || flight.state==NG_OVER || flight.state==NG_WIN) {
    rr(20,109,200,93,10,C_PANEL);
    T(&F_DISP24,flight.state==NG_PAUSE?"已暂停":flight.state==NG_WIN?"飞行完成":"再试一次",120,134,C_WHITE,AL_C);
    TF(&F_SANS16,120,172,C_GOLD,AL_C,"穿门 %d / 12",flight.cleared);
  } else if(!voiced()) T(&F_SANS16,"继续哼唱",120,238,C_GOLD,AL_C);
  footer(flight.state==NG_RUN?"OK 暂停  ▼ 重来":flight.state==NG_PAUSE?"OK 继续  ▼ 重来":"▲ 难度  OK 开始");
}
const sl_scene_t SC_FLIGHT={.id="flight",.name="哼歌飞行",.en="HUM FLIGHT",.ic="♪",.mic=NM_F_PITCH,
  .enter=fl_enter,.up=fl_up,.down=fl_down,.ok=fl_ok,.update=fl_update,.draw=fl_draw};
/* ---------------- 16-beat clap challenge ---------------- */
static ng_clap_t clap;
static int clap_bpm=96,clap_feedback;
static uint32_t clap_origin,clap_feedback_at;
static void cl_onset(uint32_t sample) {
  if(clap.state!=NG_RUN || sl_onset_db_x100()<-5000)return;
  int t=(int32_t)(sample-clap_origin)/16-45;
  int r=ng_clap_hit(&clap,t);
  if(r){clap_feedback=r;clap_feedback_at=VT;}
}
static void cl_enter(void) {ng_clap_init(&clap,clap_bpm);g_onset_hook=cl_onset;clap_feedback=0;}
static void cl_leave(void){g_onset_hook=NULL;sl_audio_stop_all();}
static void cl_up(void){if(clap.state!=NG_RUN){clap_bpm=clap_bpm==96?120:clap_bpm==120?144:96;cl_enter();}}
static void cl_down(void){sl_audio_stop_all();cl_enter();}
static void cl_ok(void) {
  sl_audio_stop_all();cl_enter();ng_clap_start(&clap);clap_origin=sl_audio_time();
  for(int i=0;i<4;i++)sl_click(i==0,clap_origin+(uint32_t)(i*clap.period*16));
}
static void cl_update(int dt){(void)dt;if(clap.state==NG_RUN)ng_clap_step(&clap,(int32_t)(sl_audio_time()-clap_origin)/16-45);}
static void cl_draw(void) {
  char mode[24];snprintf(mode,sizeof mode,"%d BPM",clap_bpm);header("拍手节奏",mode);
  int t=clap.state==NG_RUN?(int32_t)(sl_audio_time()-clap_origin)/16:clap.now,first=4*clap.period,next=clap.judged;
  if(clap.state==NG_READY) {
    T(&F_DISP24,"跟着节拍拍手",120,61,C_WHITE,AL_C);
    T(&F_SANS16,"先听四拍，再跟光圈",120,99,C_CYAN,AL_C);
    T(&F_SANS16,"每拍一次，共16拍",120,127,C_DIM,AL_C);
  } else if(clap.state==NG_OVER) {
    TF(&F_DISP24,120,59,C_WHITE,AL_C,"得分 %d",clap.score);
    TF(&F_SANS16,120,97,C_CYAN,AL_C,"命中 %d / 16",clap.hits);
    TF(&F_SANS16,120,123,C_GOLD,AL_C,"精准 %d  连击 %d",clap.perfect,clap.best_combo);
    TF(&F_SANS16,120,149,C_DIM,AL_C,"漏拍 %d  多拍 %d",clap.misses,clap.extra);
  } else if(t<first) {
    T(&F_SANS16,"预备，听四拍",120,60,C_WHITE,AL_C);
    TF(&F_MONO48,120,116,C_GOLD,AL_C,"%d",4-t/clap.period);
  } else {
    TF(&F_SANS16,120,51,C_CYAN,AL_C,"第 %d / 16 拍",next<16?next+1:16);
    int phase=(t-first)%clap.period,dist=phase<clap.period/2?phase:clap.period-phase;
    int r=20+dist*40/(clap.period/2);
    fc_ring(120,132,r,3,C_PURPLE);fc_ring(120,132,20,3,dist<=120?C_CYAN:C_DIM);
    TF(&F_SANS16,157,208,C_GOLD,AL_C,"得分 %d",clap.score);
    TF(&F_SANS16,157,236,C_CYAN,AL_C,"连击 %d",clap.combo);
    if(clap_feedback && VT-clap_feedback_at<420)T(&F_DISP18,clap_feedback==2?"精准":clap_feedback==1?"命中":"多拍了",120,132,clap_feedback>0?C_GREEN:C_MAG,AL_C);
  }
  int cx=clap.state==NG_RUN&&t>=first?40:120;
  if(clap_feedback>0 && VT-clap_feedback_at<(uint32_t)nabo_ms("S-DRUM"))nabo_once("S-DRUM",76,cx,222,(int)(VT-clap_feedback_at));
  else nabo_once("S-DRUM",76,cx,222,0);
  footer(clap.state==NG_RUN?"跟光圈拍手  OK 重开":"▲ 速度  OK 开始");
}
const sl_scene_t SC_CLAP={.id="clap",.name="拍手节奏",.en="CLAP BEAT",.ic="♪",.mic=0,
  .enter=cl_enter,.leave=cl_leave,.up=cl_up,.down=cl_down,.ok=cl_ok,.update=cl_update,.draw=cl_draw};
/* ---------------- sampler ---------------- */
static bool sample_memory, sample_held, sample_started, sample_done;
static uint32_t sample_press,sample_cursor,sample_record_at,sample_last_up,sample_note_at;
static bool sample_had_up;
static int sample_note=-1;
static void sa_enter(void){sample_memory=sl_sampler_open();sample_held=sample_started=sample_done=sample_had_up=false;sample_note=-1;}
static void sa_leave(void){sample_held=false;sl_sampler_close();}
static void sa_note(int key){if(sl_sampler_play(key)){sample_note=key;sample_note_at=VT;}else toast(sample_memory?"先按住OK采样":"内存不足，请返回");}
/* Sampler alone responds UP immediately; second press within300ms returns. */
static void sa_up(void){if(sample_had_up&&VT-sample_last_up<300){sample_had_up=false;sl_ui_back();return;}sample_had_up=true;sample_last_up=VT;sa_note(0);}
static void sa_down(void){sa_note(1);}
static void sa_ok_down(void){if(!sample_memory)return;sample_held=true;sample_started=sample_done=false;sample_press=VT;}
static void sa_finish(void){int n=sl_sampler_end();sample_done=true;if(n)toast("采样完成，三键演奏");else toast("声音太短或太轻，请重录");}
static void sa_ok_up(void){if(!sample_held)return;sample_held=false;if(sample_started){if(!sample_done)sa_finish();}else sa_note(2);}
static void sa_update(int dt) {
  (void)dt;
  if(sample_held&&!sample_started&&VT-sample_press>=350){
    sl_audio_stop_all();sl_sampler_begin();sample_started=true;sample_record_at=VT;sample_cursor=sl_mic_samples();
  }
  if(!sample_started||sample_done)return;
  int16_t tmp[2048]; /* 4KB transient UI stack, not a persistent second recording. */
  for(int retry=0;retry<2;retry++) {
    uint32_t now=sl_mic_samples(),n=now-sample_cursor;
    if(n>2048){sl_sampler_cancel();sample_done=true;toast("采样中断，请重录");return;}
    if(!n)break;
    sl_mic_last(tmp,(int)n);if(sl_mic_samples()!=now)continue;
    sl_sampler_push(tmp,(int)n);sample_cursor=now;break;
  }
  if(sl_sampler_length()==SL_SAMPLE_MAX || VT-sample_record_at>=600)sa_finish();
}
static void sa_draw(void) {
  header("采样琴","SAMPLE PIANO");
  if(!sample_memory){T(&F_DISP18,"内存不足，请返回",120,110,C_RED,AL_C);footer("▲▲ 返回");return;}
  int n=sl_sampler_length();bool rec=sample_started&&!sample_done;
  T(&F_SANS16,rec?"录音中，请发一个声音":n?"把你的声音弹成旋律":"按住OK，录半秒声音",120,53,rec?C_MAG:C_WHITE,AL_C);
  if(rec){rr(20,83,200,14,6,C_LINE);rr(20,83,n*200/SL_SAMPLE_MAX,14,6,C_MAG);}
  else TF(&F_SANS16,120,88,C_DIM,AL_C,"采样 %d ms / 500 ms",n/8);
  nabo(sl_sampler_playing()?"S-GUITAR-STRUM":"S-GUITAR-IDLE",76,120,150,sample_note>=0?(int)(VT-sample_note_at):(int)VT);
  static const char *labels[]={"▲ 原音","▼ +4","OK +7"};
  for(int i=0;i<3;i++){int x=8+i*76;rr_frame(x,202,72,44,6,1,sample_note==i&&VT-sample_note_at<220?C_GOLD:C_PURPLE,C_PANEL);T(&F_SANS16,labels[i],x+36,224,C_WHITE,AL_C);}
  footer("轻按三键弹  按住OK录");
}
const sl_scene_t SC_SAMPLER={.id="sampler",.name="采样琴",.en="SAMPLER",.ic="♪",.mic=0,.up_now=true,
  .enter=sa_enter,.leave=sa_leave,.up=sa_up,.down=sa_down,.ok_down=sa_ok_down,.ok_up=sa_ok_up,.update=sa_update,.draw=sa_draw};
void sl_newgames_status(char *out,size_t n) {
  const sl_scene_t *s=sl_ui_scene();
  if(s==&SC_FLIGHT)snprintf(out,n,"flight state=%u score=%d lives=%u gates=%u/12 ref=%d",flight.state,flight.score,flight.lives,flight.seen,flight.reference);
  else if(s==&SC_CLAP)snprintf(out,n,"clap state=%u score=%d hit=%u perfect=%u miss=%u extra=%u bpm=%d",clap.state,clap.score,clap.hits,clap.perfect,clap.misses,clap.extra,clap_bpm);
  else if(s==&SC_SAMPLER)snprintf(out,n,"sampler allocated=%d samples8k=%d recording=%d playing=%d",sample_memory,sl_sampler_length(),sl_sampler_recording(),sl_sampler_playing());
  else if(n)out[0]=0;
}
