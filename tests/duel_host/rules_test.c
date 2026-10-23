/* Production-rule host checks and event trace. No ESP-IDF/device dependency. */
#include "duel_rules.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const uint32_t seeds[]={0,1,2,42,20261023,0xffffffffu,0x80000000u,0x9e3779b9u,7,31,123,999,65535,123456789,0x12345678,0xdeadbeef};
static int tests;
#define CHECK(v) do { assert(v); tests++; } while(0)
static void units(void){
  const int offsets[]={-181,-180,-121,-120,-61,-60,0,60,61,120,121,180,181};
  for(int round=1;round<=3;round++)for(unsigned z=0;z<sizeof seeds/sizeof *seeds;z++){
    sd_score_t s;sd_score_init(&s,seeds[z],round);
    CHECK(s.n>=8&&s.n<=16);CHECK(s.beat==60000/(90+(round-1)*15));
    CHECK(s.at[0]==4*s.beat);CHECK(s.duration==12*s.beat+250);
    for(int i=1;i<s.n;i++)CHECK(s.at[i]>s.at[i-1]);
    for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++){
      sd_score_init(&s,seeds[z],round);int d=offsets[k],index=d>0?s.n-1:0,got=sd_score_tap(&s,s.at[index]+d),absd=d<0?-d:d;
      int want=d< -180?-1:absd>180?0:absd<=60?100:absd<=120?60:20;
      CHECK(got==want);CHECK(s.earned==(want>0?want:0));CHECK(s.hit==(want>0?(1u<<index):0));
    }
    sd_score_init(&s,seeds[z],round);CHECK(sd_score_tap(&s,s.at[0]-181)==-1);CHECK(sd_score_tap(&s,s.duration+1)==-1);CHECK(s.extras==0);
    for(int i=0;i<s.n;i++)CHECK(sd_score_tap(&s,s.at[i])==100);
    CHECK(sd_score_value(&s)==1000);CHECK(s.best==s.n);CHECK(s.combo==s.n);
    CHECK(sd_score_tap(&s,s.at[0])==0);CHECK(s.extras==1);CHECK(s.combo==0);
    CHECK(sd_score_value(&s)==(s.n*100-20)*1000/(s.n*100));
    for(int k=0;k<1100;k++)sd_score_tap(&s,s.at[0]);
    CHECK(s.extras==1000);CHECK(sd_score_value(&s)==0);
    sd_score_init(&s,seeds[z],round);CHECK(sd_score_value(&s)==0);CHECK(s.hit==0);CHECK(s.earned==0);
    sd_score_init(&s,seeds[z],1);CHECK(s.last_index==-1);
    CHECK(sd_score_tap(&s,s.at[0])==100);CHECK(s.combo==1);
    CHECK(sd_score_tap(&s,s.at[2])==100);CHECK(s.combo==1);CHECK(s.best==1);
    CHECK(sd_score_tap(&s,s.at[3])==100);CHECK(s.combo==2);CHECK(s.best==2);
    CHECK(sd_score_tap(&s,s.at[3]+1)==0);CHECK(s.combo==0);
    CHECK(sd_score_tap(&s,s.at[4])==100);CHECK(s.combo==1);
    sd_score_init(&s,seeds[z],3);
    CHECK(sd_score_tap(&s,s.at[1])==100);CHECK(s.combo==1);
    CHECK(sd_score_tap(&s,s.at[0])==100);CHECK(s.combo==1);CHECK(s.last_index==0);
  }
}
static int first=1;
static void snapshot(const sd_score_t *s,uint32_t seed,int scenario,int elapsed,int result){
  if(!first)puts(",");first=0;
  printf("{\"seed\":%u,\"scenario\":%d,\"t\":%d,\"result\":%d,\"round\":%u,\"beat\":%u,\"duration\":%u,\"n\":%u,\"at\":[",seed,scenario,elapsed,result,s->round,s->beat,s->duration,s->n);
  for(int i=0;i<s->n;i++)printf("%s%u",i?",":"",s->at[i]);
  printf("],\"hit\":%u,\"combo\":%u,\"best\":%u,\"earned\":%u,\"extras\":%u,\"last_index\":%d,\"last_error\":%d,\"value\":%d}",s->hit,s->combo,s->best,s->earned,s->extras,s->last_index,s->last_error,sd_score_value(s));
}
static void event(sd_score_t *s,uint32_t seed,int scenario,int elapsed){int r=sd_score_tap(s,elapsed);snapshot(s,seed,scenario,elapsed,r);}
static void trace(void){
  const int offsets[]={-181,-180,-121,-120,-61,-60,-1,0,1,60,61,120,121,180,181};
  printf("[");
  for(int round=1;round<=3;round++)for(unsigned z=0;z<sizeof seeds/sizeof *seeds;z++){
    const uint32_t seed=seeds[z];sd_score_t s;
    for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++){
      sd_score_init(&s,seed,round);snapshot(&s,seed,k,-999,-999);
      int index=offsets[k]>0?s.n-1:0;
      event(&s,seed,k,s.at[index]+offsets[k]);event(&s,seed,k,s.at[index]+offsets[k]);
    }
    sd_score_init(&s,seed,round);snapshot(&s,seed,100,-999,-999);
    event(&s,seed,100,-100);event(&s,seed,100,s.at[0]-181);
    for(int i=0;i<s.n;i++){event(&s,seed,100,s.at[i]);event(&s,seed,100,s.at[i]+1);}
    event(&s,seed,100,s.duration);event(&s,seed,100,s.duration+1);
    sd_score_init(&s,seed,round);snapshot(&s,seed,101,-999,-999);
    for(int i=0;i<s.n;i++)event(&s,seed,101,s.at[i]+offsets[(i+z+round)%15]);
    event(&s,seed,101,s.duration+1);
    sd_score_init(&s,seed,round);snapshot(&s,seed,102,-999,-999);
    for(int k=0;k<1002;k++)event(&s,seed,102,s.at[0]);
    /* Advancing an empty round has no tap side effects. */
    sd_score_init(&s,seed,round);snapshot(&s,seed,103,0,-999);
    snapshot(&s,seed,103,s.at[0],-999);snapshot(&s,seed,103,s.at[s.n-1],-999);
    snapshot(&s,seed,103,s.duration,-999);
    sd_score_init(&s,seed,round);snapshot(&s,seed,104,-999,-999);
    event(&s,seed,104,s.at[0]);event(&s,seed,104,s.at[2]);event(&s,seed,104,s.at[3]);
    event(&s,seed,104,s.at[1]);event(&s,seed,104,s.at[4]);
  }
  puts("]");
}
int main(int argc,char **argv){if(argc>1&&!strcmp(argv[1],"--trace")){trace();return 0;}units();printf("PASS %d production C rhythm assertions\n",tests);return 0;}
