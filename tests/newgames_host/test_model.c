#include "newgames_model.h"
#include "sl_sampler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(c) do { if(!(c)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c);exit(2);} } while(0)
static int locks;
void sl_audio_lock(void){assert(!locks);locks++;}
void sl_audio_unlock(void){assert(locks==1);locks--;}
static void flight_json(ng_flight_t *s){
  printf("{\"state\":%u,\"score\":%d,\"lives\":%u,\"seen\":%u,\"cleared\":%u,\"yq\":%ld,\"seed\":%lu,\"gate\":[",s->state,s->score,s->lives,s->seen,s->cleared,(long)s->yq,(unsigned long)s->seed);
  for(int i=0;i<4;i++)printf("%s[%ld,%d,%u]",i?",":"",(long)s->gate[i].xq,s->gate[i].y,s->gate[i].passed);printf("]}");
}
static void trace(void){
  ng_flight_t f;ng_flight_init(&f,20261023,1);ng_flight_start(&f,6000);printf("[");
  for(int i=0;i<600;i++){ng_flight_step(&f,20,6000+((i/50)%5-2)*300,i%91<80);if(i%20==0){if(i)printf(",");flight_json(&f);}}
  ng_clap_t c;ng_clap_init(&c,120);ng_clap_start(&c);
  for(int i=0;i<16;i++){int t=(i+4)*500+(i%4==0?-120:i%4==1?45:i%4==2?100:0);ng_clap_hit(&c,t);ng_clap_step(&c,(i+4)*500+121);printf(",{\"state\":%u,\"score\":%d,\"hits\":%u,\"perfect\":%u,\"misses\":%u,\"extra\":%u,\"combo\":%u,\"best_combo\":%u,\"hit_mask\":%lu}",c.state,c.score,c.hits,c.perfect,c.misses,c.extra,c.combo,c.best_combo,(unsigned long)c.hit_mask);}
  puts("]");
}
static void flight_tests(void){
  ng_flight_t a,b;ng_flight_init(&a,42,0);ng_flight_init(&b,42,0);assert(!memcmp(&a,&b,sizeof a));
  ng_flight_start(&a,6000);ng_flight_start(&b,6000);ng_flight_step(&a,100,6300,true);for(int i=0;i<5;i++)ng_flight_step(&b,20,6300,true);assert(!memcmp(&a,&b,sizeof a));assert(a.yq<156*256);
  a.state=NG_PAUSE;b=a;ng_flight_step(&a,500,8000,true);assert(!memcmp(&a,&b,sizeof a));
  ng_flight_init(&a,123,0);ng_flight_start(&a,6000);
  for(int i=0;i<3000&&a.state==NG_RUN;i++){
    ng_gate_t *g=NULL;for(int j=0;j<4;j++)if(!a.gate[j].passed&&(!g||a.gate[j].xq<g->xq))g=&a.gate[j];
    int midi=g?6000+(156-g->y)*100/6:6000;ng_flight_step(&a,20,midi,true);
  }
  assert(a.state==NG_WIN&&a.seen==12&&a.cleared==12&&a.lives==3&&a.score>120);
  ng_flight_init(&a,1,2);ng_flight_start(&a,6000);for(int j=0;j<4;j++)a.gate[j].y=208;
  for(int i=0;i<1000&&a.state==NG_RUN;i++)ng_flight_step(&a,20,6000,true);
  assert(a.state==NG_OVER&&a.lives==0&&a.seen==3);
  ng_flight_init(&a,1,0);ng_flight_start(&a,6000);ng_flight_step(&a,300,6000,false);assert(a.yq==156*256);ng_flight_step(&a,20,6000,false);assert(a.yq>156*256);
}
static void clap_tests(void){
  ng_clap_t c;ng_clap_init(&c,120);ng_clap_start(&c);
  for(int i=0;i<16;i++){assert(ng_clap_hit(&c,(i+4)*500)==2);assert(ng_clap_hit(&c,(i+4)*500+10)==0);ng_clap_step(&c,(i+4)*500+121);}
  assert(c.state==NG_OVER&&c.score==1600&&c.hits==16&&c.perfect==16&&c.misses==0&&c.best_combo==16);
  ng_clap_init(&c,120);ng_clap_start(&c);assert(ng_clap_hit(&c,1880)==1);ng_clap_step(&c,2120);assert(c.judged==0);ng_clap_step(&c,2121);assert(c.judged==1&&c.misses==0);
  assert(ng_clap_hit(&c,2621)==-1);ng_clap_step(&c,2621);assert(c.misses==1&&c.extra==1);
  /* UI expiry and onset use the same compensated clock; late but valid input survives queue latency. */
  ng_clap_init(&c,120);ng_clap_start(&c);ng_clap_step(&c,2160-45);assert(c.judged==0);assert(ng_clap_hit(&c,2155-45)==1);
  ng_clap_init(&c,96);ng_clap_start(&c);assert(ng_clap_hit(&c,100)==0);ng_clap_step(&c,20*c.period);assert(c.misses==16&&c.score==0&&c.state==NG_OVER);
}
static void sampler_tests(void){
  int16_t raw[8000];for(int i=0;i<8000;i++)raw[i]=(int16_t)((i%64-32)*800);
  assert(sl_sampler_open());sl_sampler_begin();sl_sampler_push(raw,8000);assert(!sl_sampler_recording());assert(sl_sampler_end()==4000);
  int lengths[3];for(int k=0;k<3;k++){
    assert(sl_sampler_play(k));int total=0,nonzero=0;
    while(sl_sampler_playing()){
      int32_t out[320]={0};sl_audio_lock();sl_sampler_mix(out,320);sl_audio_unlock();
      for(int i=0;i<320;i++){assert(out[i]<=18000&&out[i]>=-18000);if(out[i])nonzero++;}total+=320;assert(total<9000);
    }assert(nonzero>100);lengths[k]=total;
  }
  assert(lengths[0]>lengths[1]&&lengths[1]>lengths[2]);
  sl_sampler_begin();memset(raw,0,sizeof raw);sl_sampler_push(raw,8000);assert(sl_sampler_end()==0);assert(!sl_sampler_play(0));
  sl_sampler_close();assert(sl_sampler_length()==0&&!sl_sampler_play(0));assert(locks==0);
}
int main(int argc,char **argv){if(argc>1&&!strcmp(argv[1],"--trace")){trace();return 0;}flight_tests();clap_tests();sampler_tests();printf("PASS flight,clap,sampler; flight_state=%u bytes clap_state=%u bytes sample=%d bytes\n",(unsigned)sizeof(ng_flight_t),(unsigned)sizeof(ng_clap_t),SL_SAMPLE_MAX*2);return 0;}
