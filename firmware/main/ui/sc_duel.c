/* Wireless rhythm battle. No microphone, no pitch judgement, no mirrored NABO. */
#include "sl_ui.h"
#include "duel_model.h"
#include "sc_duel.h"
#include "../link/sd_radio.h"
#include <stdio.h>
#include <string.h>
#ifndef ESP_PLATFORM
__attribute__((weak)) bool sd_radio_begin(void){return true;}
__attribute__((weak)) void sd_radio_end(void){}
__attribute__((weak)) bool sd_radio_send(const uint8_t *to,const uint8_t *p,int n){(void)to;(void)p;(void)n;return true;}
__attribute__((weak)) bool sd_radio_poll(sd_radio_rx_t *r){(void)r;return false;}
__attribute__((weak)) void sd_radio_mac(uint8_t *m){memset(m,2,6);}
__attribute__((weak)) uint32_t sd_radio_now(void){return sl_clock_ms();}
__attribute__((weak)) const char *sd_radio_error(void){return "host simulation";}
__attribute__((weak)) void sd_radio_stats(uint32_t *r,uint32_t *d,uint32_t *f){*r=*d=*f=0;}
#endif
static sd_model_t D;
static int choice, room_sel, cue;
static bool opened, muted, fixture, start_failed;
static uint32_t feedback_at;
static uint32_t cue_at;
static int feedback;
static bool send_packet(void *ctx,const uint8_t *to,const uint8_t *p,int n){(void)ctx;return sd_radio_send(to,p,n);}
static void enter(void){
  uint8_t mac[6];sd_radio_mac(mac);sd_init(&D,mac,send_packet,NULL,sd_radio_now());
  choice=room_sel=0;opened=muted=fixture=start_failed=false;cue=-1;feedback=-1;cue_at=sd_radio_now();sl_audio_stop_all();
}
static void leave(void){sd_leave(&D,sd_radio_now());sd_radio_end();opened=false;sl_audio_stop_all();}
static void move(int delta){
  if(D.stage==SD_CHOOSE)choice^=1;
  else if(D.stage==SD_SEARCH&&D.room_n)room_sel=(room_sel+delta+D.room_n)%D.room_n;
}
static void up(void){move(-1);}
static void down(void){
  if(D.stage==SD_PLAY||D.stage==SD_ARMED){muted=!muted;if(muted)sl_audio_stop_all();}
  else move(1);
}
static void ok(void){
  uint32_t now=sd_radio_now();
  if(D.stage==SD_CHOOSE){
    fc_img_cache_clear(); /* Reclaim all animation cache before radio allocation. */
    if(!sd_radio_begin()){sd_radio_end();D.stage=SD_LOST;opened=false;start_failed=true;return;}
    opened=true;if(choice)sd_search(&D,now);else sd_host(&D,rng_u32(),now);return;
  }
  if(D.stage==SD_SEARCH){sd_join(&D,room_sel,now);return;}
  if(D.stage==SD_LOST||D.stage==SD_FINAL){leave();enter();return;}
  if(D.stage==SD_PLAY){feedback=sd_tap(&D,sl_key_time());feedback_at=now;return;}
  sd_ok(&D,now);
}
static void update(int dt){
  (void)dt;if(fixture)return;
  uint32_t now=sd_radio_now();D.now=now;
  sd_radio_rx_t r;
  for(int i=0;i<12&&sd_radio_poll(&r);i++)sd_receive(&D,r.mac,r.data,44,r.at);
  int stage=D.stage, round=D.round;sd_tick(&D,now);
  if(room_sel>=D.room_n)room_sel=0;
  if(D.stage==SD_LOST&&opened){sd_radio_end();opened=false;sl_audio_stop_all();}
  if(round!=D.round||stage!=D.stage){cue=-1;feedback=-1;}
  if(D.stage!=SD_PLAY)return;
  int32_t el=sd_elapsed(&D,now);int i=el/D.score.beat;
  if(i>=4){i=3;for(int j=0;j<D.score.n;j++)if(el>=D.score.at[j])i=4+j;}
  if(i!=cue&&i>=0&&i<4+D.score.n){cue=i;cue_at=now;if(!muted)sl_click(i%4==0,0);}
}
static void label(const char *s,int y,uint16_t c){T(&F_SANS16,s,120,y,c,AL_C);}
static void footer(const char *action){
  static const hint_t H[]={{"▲▲","返回"}};hintK(H,1,false);
  T(&F_SANS16,action,120,279,C_DIM,AL_C);
}
static void mascot(const char *id,int y){nabo(id,76,120,y,(int)VT);}
static void scores(void){
  rr(8,44,108,54,6,C_PANEL);rr(124,44,108,54,6,C_PANEL);
  T(&F_SANS16,"你",62,57,C_CYAN,AL_C);T(&F_SANS16,"对手",178,57,C_MAG,AL_C);
  TF(&F_MONO22,62,82,C_WHITE,AL_C,"%d",D.scores[D.round-1]);
  if(D.got_score)TF(&F_MONO22,178,82,C_WHITE,AL_C,"%d",D.theirs[D.round-1]);
  else T(&F_MONO22,"...",178,82,C_DIM,AL_C);
}
static void draw(void){
  header("无线对战",D.stage==SD_CHOOSE?"2P":"RHYTHM");
  if(D.stage==SD_CHOOSE){
    mascot("S-HEADPHONE",76);label("看拍点，按 OK 比节奏",128,C_WHITE);
    const char *items[]={"创建房间","寻找房间"};
    for(int i=0;i<2;i++){rr(24,150+i*42,192,34,7,choice==i?C_CYAN:C_PANEL);T(&F_SANS16,items[i],120,167+i*42,choice==i?C_BG:C_WHITE,AL_C);}
    label("无需路由器 · 不比音高",249,C_DIM);footer("▲▼ 选择   OK 进入");return;
  }
  if(D.stage==SD_SEARCH){
    label("选择相同房间号",51,C_CYAN);
    if(!D.room_n){mascot("S-HEADPHONE",128);label("正在寻找附近工牌",197,C_WHITE);label("请对方先创建房间",225,C_DIM);}
    for(int i=0;i<D.room_n;i++){rr(20,74+i*40,200,34,6,i==room_sel?C_CYAN:C_PANEL);TF(&F_MONO22,120,91+i*40,i==room_sel?C_BG:C_WHITE,AL_C,"%04u",D.rooms[i].room);}
    footer("▲▼ 选择   OK 加入");return;
  }
  if(D.stage==SD_HOST||D.stage==SD_REQUEST||D.stage==SD_JOIN||D.stage==SD_LOBBY){
    TF(&F_MONO22,120,51,C_GOLD,AL_C,"ROOM %04u",D.room);mascot("S-HEADPHONE",112);
    if(D.stage==SD_HOST){label("等待另一块工牌",174,C_WHITE);label("对方选这个房号加入",204,C_DIM);footer("双击▲ 退出房间");}
    else if(D.stage==SD_REQUEST){label("有对手申请加入",174,C_CYAN);label("确认房号后接受",204,C_DIM);footer("OK 接受");}
    else if(D.stage==SD_JOIN){label("已申请加入",174,C_CYAN);label("等待房主按 OK 接受",204,C_WHITE);footer("双击▲ 取消");}
    else{
      label(!D.host&&D.sync_n<3?"正在同步，请稍候":D.ready?"你已准备好":"按 OK 准备",170,D.ready?C_GREEN:C_CYAN);
      label(D.peer_ready?"对手已准备好":"等待对手准备",200,D.peer_ready?C_GREEN:C_DIM);
      if(!D.host&&D.sync_n<3)label("正在校准联机计时",235,C_GOLD);
      else label("同一谱面 · 三轮累计",235,C_DIM);
      footer("双方准备后开始");
    }return;
  }
  if(D.stage==SD_ARMED||D.stage==SD_PLAY){
    TF(&F_SANS16,120,45,C_GOLD,AL_C,"第 %u / 3 轮 · %u BPM",D.round,60000/D.score.beat);
    int32_t el=sd_elapsed(&D,sd_radio_now());
    if(D.stage==SD_ARMED){mascot("S-HEADPHONE",114);label("准备跟上拍点",182,C_WHITE);label("先数四拍，再按 OK",213,C_CYAN);footer("▼ 音效开关");return;}
    nabo_once("S-DRUM",76,120,94,(int)(sd_radio_now()-cue_at));
    int beat=el/D.score.beat;
    if(beat<4){TF(&F_MONO48,120,167,C_GOLD,AL_C,"%d",4-beat);label("预备，先别按",208,C_WHITE);}
    else{
      label("拍点到线，按 OK",148,C_CYAN);
      rr(8,167,224,66,7,C_PANEL);fc_rect(59,174,2,52,C_GOLD,255);
      fc_gfx_clip(10,169,220,62);
      for(int i=0;i<D.score.n;i++){
        int x=60+(int)((D.score.at[i]-el)*80/D.score.beat);
        if(x>=0&&x<240)fc_disc(x,199,7,(D.score.hit&(1u<<i))?C_GREEN:C_CYAN,255);
      }fc_gfx_clip(0,0,0,0);
      if(feedback>=0&&sd_radio_now()-feedback_at<450)label(feedback==100?"精准！":feedback?"命中":"多按了",249,feedback?C_GREEN:C_RED);
      else TF(&F_MONO22,120,250,C_WHITE,AL_C,"%d",sd_score_value(&D.score));
    }
    footer(muted?"▼ 音效关   OK 跟打":"▼ 音效开   OK 跟打");return;
  }
  if(D.stage==SD_RESULT||D.stage==SD_FINAL){
    scores();int mine=0,theirs=0;for(int i=0;i<3;i++){mine+=D.scores[i];theirs+=D.theirs[i];}
    bool done=D.stage==SD_FINAL||sd_settled(&D);const char *anim=!done?"S-HEADPHONE":mine>theirs?"S-CHEER":mine==theirs?"S-NOD":"S-OOPS";
    mascot(anim,147);
    if(D.stage==SD_FINAL){label(mine>theirs?"你赢啦！":mine==theirs?"势均力敌！":"下次再战！",203,C_GOLD);TF(&F_MONO22,120,236,C_WHITE,AL_C,"%d : %d",mine,theirs);footer("OK 重新找房");}
    else if(!done){label("正在确认双方成绩",209,C_GOLD);footer("成绩确认后继续");}
    else{label(D.ready?"你已准备，等对手":"本轮完成",207,C_CYAN);TF(&F_SANS16,120,239,C_DIM,AL_C,"累计 %d : %d",mine,theirs);footer("OK 准备下一轮");}return;
  }
  mascot("S-OOPS",102);label(start_failed?"无线未能启动":"本局已作废",167,C_GOLD);label(start_failed?"请返回后重试":"未连接或连接中断",198,C_WHITE);
  label("请双方重新进入房间",231,C_DIM);footer("OK 重试");
}
const sl_scene_t SC_DUEL={.id="duel",.name="无线对战",.en="2P",.ic="♫",.mic=0,.enter=enter,.leave=leave,
  .up=up,.down=down,.ok=ok,.update=update,.draw=draw};
void sl_duel_status(char *out,size_t n){
  uint32_t rx,drop,fail;sd_radio_stats(&rx,&drop,&fail);
  snprintf(out,n,"duel stage=%u host=%u room=%u round=%u ready=%u/%u sync=%u rtt=%u score=%d seed=%lu el_ms=%ld rx=%u drop=%u fail=%u error=%s",
    D.stage,D.host,D.room,D.round,D.ready,D.peer_ready,D.sync_n,(unsigned)D.best_rtt,D.round?sd_score_value(&D.score):0,(unsigned long)D.seed,(long)(D.round?sd_elapsed(&D,sd_radio_now()):0),(unsigned)rx,(unsigned)drop,(unsigned)fail,sd_radio_error());
}
#ifndef ESP_PLATFORM
void sl_duel_fixture(int stage,int outcome){
  fixture=true;D.stage=(uint8_t)stage;D.room=4826;D.round=2;D.got_score=D.peer_got_score=1;D.sync_n=3;D.host=1;D.ready=D.peer_ready=0;
  sd_score_init(&D.score,1234,2);D.deadline=sl_clock_ms()-D.score.beat*5;D.now=sl_clock_ms();
  D.scores[0]=750;D.scores[1]=875;D.theirs[0]=outcome==0?750:outcome>0?625:1000;D.theirs[1]=outcome==0?875:outcome>0?750:1000;
  if(outcome==99)D.got_score=D.peer_got_score=0;
  if(outcome==98)D.peer_got_score=0; /* Host authoritative final, last ACK lost. */
  if(stage==SD_SEARCH){D.room_n=4;for(int i=0;i<4;i++)D.rooms[i].room=(uint16_t)(4826+i*135);}
  if(stage==SD_FINAL){D.round=3;D.scores[2]=1000;D.theirs[2]=1000;}
}
#endif
