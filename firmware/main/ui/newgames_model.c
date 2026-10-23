#include "newgames_model.h"
#include <string.h>
#include <limits.h>
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t random32(uint32_t *seed) {
  uint32_t t = (*seed += 0x6D2B79F5u);
  t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}
int ng_flight_gap(const ng_flight_t *s) { static const int gap[3] = {88,74,62}; return gap[s->difficulty]; }
void ng_flight_init(ng_flight_t *s, uint32_t seed, int difficulty) {
  memset(s, 0, sizeof *s); s->seed = seed ? seed : 1; s->difficulty = (uint8_t)clamp(difficulty,0,2);
  s->state = NG_READY; s->lives = 3; s->yq = 156 * 256;
  for (int i=0;i<4;i++) { s->gate[i].xq = (270+i*120)*256; s->gate[i].y = (int16_t)(108+(random32(&s->seed)%5)*24); }
}
void ng_flight_start(ng_flight_t *s, int midi100) { s->reference=(int16_t)clamp(midi100,2400,9600); s->state=NG_RUN; }
static void flight_tick(ng_flight_t *s, int midi100, bool voiced) {
  s->elapsed += 20;
  s->flash_ms = s->flash_ms > 20 ? (uint16_t)(s->flash_ms-20) : 0;
  if (voiced) {
    s->silent_ms=0;
    int target=clamp(156*256-(midi100-s->reference)*6*256/100,72*256,232*256);
    s->yq += clamp(target-s->yq,-716,716); /* <=140 px/s */
  } else {
    s->silent_ms=(uint16_t)clamp(s->silent_ms+20,0,60000);
    if (s->silent_ms>300) s->yq=clamp(s->yq+194,72*256,232*256);
  }
  static const int speed[3]={60,72,84};
  for(int i=0;i<4;i++) {
    ng_gate_t *g=&s->gate[i]; g->xq-=speed[s->difficulty]*256*20/1000;
    if(!g->passed && g->xq<=54*256) {
      g->passed=1; s->seen++;
      int d=s->yq/256-g->y; if(d<0)d=-d;
      if(d<=ng_flight_gap(s)/2-12) { s->cleared++; s->combo++; s->score=(int16_t)(s->score+10+2*clamp(s->combo,0,5)); }
      else { s->lives--; s->combo=0; s->flash_ms=320; }
      if(!s->lives || s->seen==12) { s->state=s->lives?NG_WIN:NG_OVER; return; }
    }
    if(g->xq < -20*256) { g->xq+=480*256; g->passed=0; g->y=(int16_t)(108+(random32(&s->seed)%5)*24); }
  }
}
void ng_flight_step(ng_flight_t *s,int dt,int midi100,bool voiced) {
  if(s->state!=NG_RUN || dt<=0)return;
  s->carry=(uint16_t)(s->carry+clamp(dt,0,1000));
  while(s->carry>=20 && s->state==NG_RUN) { s->carry-=20; flight_tick(s,midi100,voiced); }
}
void ng_clap_init(ng_clap_t *s,int bpm) {
  memset(s,0,sizeof *s);s->state=NG_READY;s->period=(uint16_t)(60000/clamp(bpm,60,180));s->last_hit=INT32_MIN/2;
}
void ng_clap_start(ng_clap_t *s) { s->state=NG_RUN; }
void ng_clap_step(ng_clap_t *s,int now_ms) {
  if(s->state!=NG_RUN)return;
  s->now=now_ms;
  while(s->judged<16 && now_ms>(4+s->judged)*s->period+120) {
    if(!(s->hit_mask&(1u<<s->judged))) { s->misses++;s->combo=0; }
    s->judged++;
  }
  if(s->judged==16)s->state=NG_OVER;
}
int ng_clap_hit(ng_clap_t *s,int t) {
  if(s->state!=NG_RUN || t<0 || t-s->last_hit<160)return 0;
  s->last_hit=t;
  int d=t-4*s->period;
  int i=d>=0?(d+s->period/2)/s->period:-1;
  if(d<0 && d>=-120)i=0;
  int error=i>=0&&i<16 ? t-(4+i)*s->period : 10000;
  int ae=error<0?-error:error;
  if(i<0 || i>=16 || ae>120) { if(t>=4*s->period-120) {s->extra++;s->combo=0;s->score=(int16_t)clamp(s->score-10,0,1600);return -1;} return 0; }
  if((s->hit_mask&(1u<<i)) || i<s->judged)return 0;
  s->hit_mask|=1u<<i;s->hits++;s->last_error=(int16_t)error;s->combo++;
  if(s->combo>s->best_combo)s->best_combo=s->combo;
  if(ae<=45) {s->perfect++;s->score+=100;return 2;}s->score+=60;return 1;
}
