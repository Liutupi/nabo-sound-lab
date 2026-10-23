/* Production state-machine network simulation. No ESP-IDF/radio/device claims. */
#include "duel_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define QMAX 8192
static int assertions, scenarios, completed_matches, sum_delivered, sum_dropped, sum_duplicates, sum_reordered, sum_busy;
static const uint8_t macs[3][6]={{2,0,0,0,0,1},{2,0,0,0,0,2},{2,0,0,0,0,3}};
#define CHECK(c) do { assertions++; if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c);exit(2);} } while(0)
typedef struct {int live,from,to; uint32_t due;uint8_t p[SD_WIRE];} packet_t;
typedef struct sim sim_t;
typedef struct {sim_t *s;int id;} endpoint_t;
struct sim {
  sd_model_t m[2];endpoint_t e[2];packet_t q[QMAX];
  uint32_t t,base[2],busy[2],rng;
  int loss,dup,jitter,oneflight,block_from,block_type;
  uint32_t block_mask[2];int block_committed_start;
  int block_final_confirm[2];
  int sends,queued,dropped,duplicates,busy_rejected,delivered,reordered;
  uint32_t last_delivery_seq[2][2];
  uint8_t last[2][SD_LEAVE+1][SD_WIRE];int have[2][SD_LEAVE+1];
};
static sim_t s;
static uint32_t rnd(sim_t *x){x->rng=x->rng*1664525u+1013904223u;return x->rng;}
static uint32_t local(sim_t *x,int id){return x->base[id]+x->t;}
static void queue(sim_t *x,int from,int to,const uint8_t *p,uint32_t due){
  int i;for(i=0;i<QMAX&&x->q[i].live;i++);CHECK(i<QMAX);
  x->q[i].live=1;x->q[i].from=from;x->q[i].to=to;x->q[i].due=due;memcpy(x->q[i].p,p,SD_WIRE);x->queued++;
}
static bool send_cb(void *ctx,const uint8_t *to,const uint8_t *p,int n){
  endpoint_t *e=(endpoint_t*)ctx;sim_t *x=e->s;int from=e->id,dest=1-from,type=p[5];
  CHECK(n==SD_WIRE);CHECK(sd_valid(p,n));CHECK(!to||memcmp(to,macs[dest],6)==0);
  x->sends++;memcpy(x->last[from][type],p,n);x->have[from][type]++;
  if(x->oneflight&&x->t<x->busy[from]){x->busy_rejected++;return false;}
  x->busy[from]=x->t+8;
  if((from==x->block_from&&type==x->block_type)||(x->block_mask[from]&(1u<<type))||
     (x->block_committed_start&&from==0&&type==SD_START&&sd_get32(p+20)==1)||
     (x->block_final_confirm[from]&&type==SD_SCORE&&p[6]==3&&(p[7]&2))||
     (int)(rnd(x)%100)<x->loss){x->dropped++;return true;}
  uint32_t delay=8+(x->jitter?rnd(x)%(uint32_t)x->jitter:0);
  queue(x,from,dest,p,x->t+delay);
  if((int)(rnd(x)%100)<x->dup){queue(x,from,dest,p,x->t+delay+1+rnd(x)%600);x->duplicates++;}
  return true;
}
static void setup(uint32_t seed,int32_t guest_delta,int loss,int dup,int jitter,int oneflight){
  memset(&s,0,sizeof s);s.rng=seed;s.base[0]=1000000;s.base[1]=s.base[0]+(uint32_t)guest_delta;
  s.loss=loss;s.dup=dup;s.jitter=jitter;s.oneflight=oneflight;s.block_from=-1;s.block_type=-1;
  for(int i=0;i<2;i++){s.e[i].s=&s;s.e[i].id=i;sd_init(&s.m[i],macs[i],send_cb,&s.e[i],local(&s,i));}
}
static void deliveries(void){
  for(int i=QMAX-1;i>=0;i--){packet_t *p=&s.q[i];if(!p->live||p->due>s.t)continue;
    int to=p->to,from=p->from;uint32_t seq=sd_get32(p->p+12);
    if(seq<s.last_delivery_seq[to][from])s.reordered++;
    if(seq>s.last_delivery_seq[to][from])s.last_delivery_seq[to][from]=seq;
    s.m[to].now=local(&s,to); /* Platform must update current local processing time. */
    sd_receive(&s.m[to],macs[from],p->p,SD_WIRE,s.base[to]+p->due);p->live=0;s.delivered++;
  }
}
static void advance(int ms){for(int j=0;j<ms;j+=5){s.t+=5;deliveries();sd_tick(&s.m[0],local(&s,0));sd_tick(&s.m[1],local(&s,1));}}
static void begin(void){sd_host(&s.m[0],0x12340000u^s.rng,local(&s,0));sd_search(&s.m[1],local(&s,1));}
static void lobby(int ready){
  begin();for(int i=0;i<30000;i+=5){advance(5);
    if(s.m[1].stage==SD_SEARCH&&s.m[1].room_n)CHECK(sd_join(&s.m[1],0,local(&s,1)));
    if(s.m[0].stage==SD_REQUEST)sd_ok(&s.m[0],local(&s,0));
    if(s.m[0].stage==SD_LOBBY&&s.m[1].stage==SD_LOBBY&&s.m[1].sync_n>=3){
      CHECK(s.m[0].session==s.m[1].session);CHECK(s.m[0].seed==s.m[1].seed);
      if(ready){sd_ok(&s.m[0],local(&s,0));sd_ok(&s.m[1],local(&s,1));}return;
    }
  }CHECK(!"join/accept/synchronize did not reach lobby");
}
static uint32_t checksum(const uint8_t *p){uint32_t h=2166136261u;for(int i=0;i<40;i++)h=(h^p[i])*16777619u;return h;}
static void seal(uint8_t *p){sd_put32(p+40,checksum(p));}
static void inject(int to,const uint8_t *mac,uint8_t *p,int n){s.m[to].now=local(&s,to);sd_receive(&s.m[to],mac,p,n,local(&s,to));}
static void unchanged(int to,const uint8_t *mac,uint8_t *p,int n){sd_model_t old=s.m[to];old.now=local(&s,to);inject(to,mac,p,n);CHECK(memcmp(&old,&s.m[to],sizeof old)==0);}
static void semantic_unchanged(int to,const uint8_t *mac,uint8_t *p,int n){sd_model_t old=s.m[to];old.now=local(&s,to);inject(to,mac,p,n);old.rx_seq=s.m[to].rx_seq;CHECK(memcmp(&old,&s.m[to],sizeof old)==0);}
static void fresh(int to,uint8_t *p){sd_put32(p+12,s.m[to].rx_seq+1000);seal(p);}

static void test_wire(void){
  setup(5,1743,0,0,0,0);lobby(0);CHECK(s.have[0][SD_ACCEPT]);
  uint8_t valid[SD_WIRE],p[SD_WIRE];memcpy(valid,s.last[0][SD_ACCEPT],SD_WIRE);CHECK(sd_valid(valid,SD_WIRE));
  CHECK(!sd_valid(NULL,SD_WIRE));
  for(int n=-1;n<=SD_WIRE+1;n++)if(n!=SD_WIRE)CHECK(!sd_valid(valid,n));
  for(int bit=0;bit<SD_WIRE*8;bit++){memcpy(p,valid,SD_WIRE);p[bit/8]^=(uint8_t)(1u<<(bit%8));CHECK(!sd_valid(p,SD_WIRE));}
  for(int version=0;version<=255;version++)if(version!=1){memcpy(p,valid,SD_WIRE);p[4]=(uint8_t)version;seal(p);CHECK(!sd_valid(p,SD_WIRE));}
  int indices[]={0,4,5,6,7,8,38,39,40};
  for(int i=0;i<(int)(sizeof indices/sizeof *indices);i++){memcpy(p,valid,SD_WIRE);p[indices[i]]=255;seal(p);
    if(indices[i]==8||indices[i]==40)continue;CHECK(!sd_valid(p,SD_WIRE));}
  memcpy(p,valid,SD_WIRE);sd_put32(p+8,0);seal(p);CHECK(!sd_valid(p,SD_WIRE));
  for(int i=0;i<3;i++){memcpy(p,valid,SD_WIRE);p[32+i*2]=0xe9;p[33+i*2]=3;seal(p);CHECK(!sd_valid(p,SD_WIRE));}
  scenarios++;
}
static void test_identity(void){
  setup(8,-2217,0,0,0,0);lobby(0);uint8_t p[SD_WIRE],zero[6]={0},broadcast[6]={255,255,255,255,255,255};
  memcpy(p,s.last[0][SD_ACCEPT],SD_WIRE);unchanged(1,macs[2],p,SD_WIRE);unchanged(1,macs[1],p,SD_WIRE);
  unchanged(1,zero,p,SD_WIRE);unchanged(1,broadcast,p,SD_WIRE);
  sd_put32(p+8,s.m[0].session+1);p[7]^=1;fresh(1,p);unchanged(1,macs[0],p,SD_WIRE);
  memcpy(p,s.last[0][SD_ACCEPT],SD_WIRE);p[28]^=32;seal(p);
  /* ACCEPT seed is fixed at JOIN->LOBBY; duplicate cannot replace negotiated seed. */
  uint32_t seed=s.m[1].seed;inject(1,macs[0],p,SD_WIRE);CHECK(s.m[1].seed==seed);
  memcpy(p,s.last[0][SD_ACCEPT],SD_WIRE);p[5]=SD_START;p[6]=1;sd_put32(p+16,local(&s,0)+2500);fresh(1,p);
  semantic_unchanged(1,macs[0],p,SD_WIRE); /* guest not ready */
  for(int n=0;n<SD_WIRE;n++)unchanged(1,macs[0],p,n);
  scenarios++;
}
static void test_match(uint32_t seed,int32_t delta,int loss,int dup,int jitter,int oneflight,int wrap){
  setup(seed,delta,loss,dup,jitter,oneflight);if(wrap){s.base[0]=UINT32_MAX-8000;s.base[1]=s.base[0]+(uint32_t)delta;}
  lobby(1);uint16_t tapped[2][3]={{0}};int entered[2][3]={{0}};uint32_t realstart[2][3]={{0}};
  for(int t=0;t<65000;t+=5){advance(5);
    if(s.m[0].round&&s.m[0].round==s.m[1].round){
      CHECK(s.m[0].score.n==s.m[1].score.n);CHECK(s.m[0].score.beat==s.m[1].score.beat);
      CHECK(s.m[0].score.duration==s.m[1].score.duration);CHECK(memcmp(s.m[0].score.at,s.m[1].score.at,sizeof s.m[0].score.at)==0);
    }
    for(int id=0;id<2;id++){sd_model_t *m=&s.m[id];
      CHECK(m->stage!=SD_LOST);
      if(m->stage==SD_ARMED&&!entered[id][m->round-1]){entered[id][m->round-1]=1;realstart[id][m->round-1]=m->deadline-s.base[id];}
      if(m->stage==SD_PLAY){for(int k=0;k<m->score.n;k++){
        int error=id?((k%3==0)?90:(k%3==1)?-90:0):0;
        uint32_t pressed=m->deadline+m->score.at[k]+error;
        if(!(tapped[id][m->round-1]&(1u<<k))&&(int32_t)(local(&s,id)-pressed)>=0){
          CHECK(sd_tap(m,pressed)==(id?((k%3==2)?100:60):100));tapped[id][m->round-1]|=(uint16_t)(1u<<k);
        }
      }}
      if(m->stage==SD_RESULT&&sd_settled(m)&&m->round<3)sd_ok(m,local(&s,id));
    }
    if(s.m[0].stage==SD_FINAL&&s.m[1].stage==SD_FINAL)break;
  }
  CHECK(s.m[0].stage==SD_FINAL);CHECK(s.m[1].stage==SD_FINAL);
  for(int r=0;r<3;r++){
    CHECK(entered[0][r]&&entered[1][r]);int32_t phase=(int32_t)(realstart[0][r]-realstart[1][r]);CHECK(abs(phase)<=40);
    CHECK(s.m[0].scores[r]==1000);CHECK(s.m[0].theirs[r]==s.m[1].scores[r]);CHECK(s.m[1].theirs[r]==s.m[0].scores[r]);
    CHECK(s.m[1].scores[r]<s.m[0].scores[r]);
  }
  if(loss)CHECK(s.dropped>0);if(dup)CHECK(s.duplicates>0);if(dup&&jitter)CHECK(s.reordered>0);
  CHECK(s.have[0][SD_START]>1);CHECK(s.have[1][SD_ACK]>1);CHECK(s.have[0][SD_SCORE]>1&&s.have[1][SD_SCORE]>1);
  completed_matches++;sum_delivered+=s.delivered;sum_dropped+=s.dropped;sum_duplicates+=s.duplicates;sum_reordered+=s.reordered;sum_busy+=s.busy_rejected;
  scenarios++;
}
static void test_replays(void){
  setup(75,-1808,0,0,0,0);lobby(1);
  for(int i=0;i<5000&&s.m[1].stage!=SD_PLAY;i+=5)advance(5);
  CHECK(s.m[0].stage==SD_PLAY&&s.m[1].stage==SD_PLAY);uint8_t start[SD_WIRE];memcpy(start,s.last[0][SD_START],SD_WIRE);
  uint32_t deadline=s.m[1].deadline;sd_score_t before=s.m[1].score;
  inject(1,macs[0],start,SD_WIRE);CHECK(s.m[1].deadline==deadline);CHECK(memcmp(&before,&s.m[1].score,sizeof before)==0);
  uint8_t bad[SD_WIRE];memcpy(bad,start,SD_WIRE);sd_put32(bad+16,sd_get32(bad+16)+200);seal(bad);unchanged(1,macs[0],bad,SD_WIRE);
  for(int i=0;i<10000&&!(s.m[0].stage==SD_RESULT&&s.m[1].stage==SD_RESULT&&sd_settled(&s.m[0])&&sd_settled(&s.m[1]));i+=5)advance(5);
  CHECK(s.m[0].stage==SD_RESULT&&s.m[1].stage==SD_RESULT);CHECK(sd_settled(&s.m[0])&&sd_settled(&s.m[1]));
  uint8_t score[SD_WIRE];memcpy(score,s.last[1][SD_SCORE],SD_WIRE);
  for(int i=0;i<100;i++){inject(0,macs[1],score,SD_WIRE);CHECK(s.m[0].theirs[0]==0);CHECK(s.m[0].scores[0]==0);}
  memcpy(bad,score,SD_WIRE);bad[32]=100;fresh(0,bad);semantic_unchanged(0,macs[1],bad,SD_WIRE); /* conflicting same-round score */
  s.m[1].seq=s.m[0].rx_seq; /* Resume genuine next packets above explicit high-sequence fault injection. */
  sd_ok(&s.m[0],local(&s,0));sd_ok(&s.m[1],local(&s,1));
  for(int i=0;i<4000&&s.m[1].round!=2;i+=5)advance(5);
  CHECK(s.m[0].round==2&&s.m[1].round==2);unchanged(1,macs[0],start,SD_WIRE);unchanged(0,macs[1],score,SD_WIRE);
  /* Bind to session even with same peer after exiting and creating another room. */
  sd_leave(&s.m[0],local(&s,0));sd_host(&s.m[0],0xbeef0123,local(&s,0));
  unchanged(0,macs[1],score,SD_WIRE);CHECK(s.m[0].stage==SD_HOST&&s.m[0].round==0);
  scenarios++;
}
static void test_timeouts(void){
  setup(91,1000,0,0,0,0);sd_search(&s.m[1],local(&s,1));sd_tick(&s.m[1],local(&s,1)+60000);CHECK(s.m[1].stage==SD_SEARCH);
  sd_tick(&s.m[1],local(&s,1)+60001);CHECK(s.m[1].stage==SD_LOST);
  setup(92,1000,0,0,0,0);sd_host(&s.m[0],92,local(&s,0));sd_tick(&s.m[0],local(&s,0)+60001);CHECK(s.m[0].stage==SD_LOST);
  setup(93,1000,0,0,0,0);lobby(0);s.loss=100;advance(8100);CHECK(s.m[0].stage==SD_LOST&&s.m[1].stage==SD_LOST);
  setup(94,1000,0,0,0,0);lobby(0);sd_leave(&s.m[1],local(&s,1));CHECK(s.m[1].stage==SD_CHOOSE);
  advance(100);CHECK(s.m[0].stage==SD_LOST);
  setup(95,1000,0,0,0,0);begin();advance(50);CHECK(s.m[1].room_n==1);sd_tick(&s.m[1],local(&s,1)+3001);CHECK(s.m[1].room_n==0);CHECK(!sd_join(&s.m[1],0,local(&s,1)+3001));
  scenarios++;
}
static void test_ack_loss(void){
  setup(99,-4000,0,0,0,0);lobby(0);s.block_from=1;s.block_type=SD_ACK;s.block_mask[0]=s.block_mask[1]=1u<<SD_LEAVE;
  sd_ok(&s.m[0],local(&s,0));sd_ok(&s.m[1],local(&s,1));
  for(int i=0;i<5000&&s.m[0].stage!=SD_LOST;i+=5)advance(5);
  CHECK(s.m[0].stage==SD_LOST);advance(1000);
  CHECK(s.m[1].stage!=SD_PLAY); /* Both sides must abort before countdown completes. */
  CHECK(s.m[1].stage==SD_LOST);CHECK(!s.m[0].got_score&&!s.m[1].got_score);
  scenarios++;
}
static void test_partitions(void){
  /* Host receives ACK; all committed START packets and both LEAVEs disappear. */
  setup(102,7193,0,0,0,1);lobby(0);s.block_committed_start=1;s.block_mask[0]=s.block_mask[1]=1u<<SD_LEAVE;
  sd_ok(&s.m[0],local(&s,0));sd_ok(&s.m[1],local(&s,1));
  int guest_aborted_before_play=0;
  for(int i=0;i<12000;i+=5){advance(5);CHECK(s.m[0].stage!=SD_FINAL&&s.m[1].stage!=SD_FINAL);
    if(s.m[1].stage==SD_LOST)guest_aborted_before_play=1;
    CHECK(s.m[1].stage!=SD_PLAY);
  }
  CHECK(guest_aborted_before_play);CHECK(s.m[0].stage==SD_LOST&&s.m[1].stage==SD_LOST);scenarios++;
  for(int direction=0;direction<3;direction++){
    setup((uint32_t)(110+direction),-5843,0,0,0,1);lobby(1);
    for(int i=0;i<5000&&!(s.m[0].stage==SD_PLAY&&s.m[1].stage==SD_PLAY);i+=5)advance(5);
    CHECK(s.m[0].stage==SD_PLAY&&s.m[1].stage==SD_PLAY);
    if(direction!=1)s.block_mask[0]=UINT32_MAX;if(direction!=0)s.block_mask[1]=UINT32_MAX;
    for(int i=0;i<11000;i+=5){advance(5);CHECK(s.m[0].stage!=SD_FINAL&&s.m[1].stage!=SD_FINAL);}
    CHECK(s.m[0].stage==SD_LOST&&s.m[1].stage==SD_LOST);scenarios++;
  }
}
static void test_old_keepalive(void){
  setup(181,3007,0,0,0,0);lobby(1);
  for(int i=0;i<5000&&!(s.m[0].stage==SD_PLAY&&s.m[1].stage==SD_PLAY);i+=5)advance(5);
  CHECK(s.m[0].stage==SD_PLAY&&s.m[1].stage==SD_PLAY);
  uint8_t old_ack[SD_WIRE],old_start[SD_WIRE];memcpy(old_ack,s.last[1][SD_ACK],SD_WIRE);memcpy(old_start,s.last[0][SD_START],SD_WIRE);
  s.block_mask[0]=s.block_mask[1]=UINT32_MAX;
  for(int i=0;i<9000;i+=5){advance(5);inject(0,macs[1],old_ack,SD_WIRE);inject(1,macs[0],old_start,SD_WIRE);}
  CHECK(s.m[0].stage==SD_LOST&&s.m[1].stage==SD_LOST);scenarios++;
}
static void test_room_bounds(void){
  setup(182,2001,0,0,0,0);begin();advance(50);CHECK(s.m[1].room_n==1);
  uint8_t adv[SD_WIRE];memcpy(adv,s.last[0][SD_ADV],SD_WIRE);
  for(int i=0;i<12;i++){uint8_t other[6]={2,0,0,0,1,(uint8_t)i};sd_put32(adv+8,(uint32_t)i+1);sd_put32(adv+16,(uint32_t)2000+i);seal(adv);inject(1,other,adv,SD_WIRE);CHECK(s.m[1].room_n<=SD_ROOMS);}
  CHECK(s.m[1].room_n==SD_ROOMS);CHECK(!sd_join(&s.m[1],-1,local(&s,1)));CHECK(!sd_join(&s.m[1],SD_ROOMS,local(&s,1)));
  sd_put32(adv+16,999);seal(adv);int before=s.m[1].room_n;inject(1,macs[2],adv,SD_WIRE);CHECK(s.m[1].room_n==before);
  s.block_mask[0]=UINT32_MAX;advance(3100);CHECK(s.m[1].room_n==0);scenarios++;
}
static void test_round_bound(void){
  /* UI may dispatch OK between score reception and the following tick. */
  setup(201,1731,0,0,0,0);lobby(0);sd_model_t *m=&s.m[0];
  m->stage=SD_RESULT;m->round=3;m->got_score=m->peer_got_score=1;m->peer_ready=1;
  sd_ok(m,local(&s,0));CHECK(!m->ready);
  m->ready=1; /* Defensive bound also survives an already queued/stale ready bit. */
  sd_tick(m,local(&s,0));CHECK(m->round==3);CHECK(m->stage==SD_FINAL);scenarios++;
}
static void final_without_taps(void){
  lobby(1);
  for(int t=0;t<60000;t+=5){advance(5);
    for(int id=0;id<2;id++)if(s.m[id].stage==SD_RESULT&&s.m[id].round<3&&sd_settled(&s.m[id]))sd_ok(&s.m[id],local(&s,id));
  }
}
static void test_final_confirmation(void){
  /* Regression: loss of guest's final receipt must not revoke host's known result. */
  setup(708,1731,0,0,0,1);s.block_final_confirm[1]=1;final_without_taps();
  CHECK(s.m[0].stage==SD_FINAL&&s.m[1].stage==SD_FINAL);
  CHECK(s.m[0].got_score&&s.m[1].got_score);CHECK(!s.m[0].peer_got_score);CHECK(s.m[1].peer_got_score);
  for(int r=0;r<3;r++){CHECK(s.m[0].scores[r]==s.m[1].theirs[r]);CHECK(s.m[1].scores[r]==s.m[0].theirs[r]);}
  /* Ordinary exit does not erase a completed result. */
  sd_leave(&s.m[1],local(&s,1));advance(100);CHECK(s.m[0].stage==SD_FINAL);CHECK(s.m[1].stage==SD_CHOOSE);
  /* Explicit abort after final revokes it, even if the normal LEAVE preceded it. */
  uint8_t abort[SD_WIRE];memcpy(abort,s.last[1][SD_LEAVE],SD_WIRE);sd_put32(abort+16,1);fresh(0,abort);inject(0,macs[1],abort,SD_WIRE);
  CHECK(s.m[0].stage==SD_LOST);CHECK(sd_get32(s.last[0][SD_LEAVE]+16)==1);scenarios++;
  /* Loss of host's final assertion leaves guest uncertain; its abort must reach host. */
  setup(709,-2713,0,0,0,1);s.block_final_confirm[0]=1;final_without_taps();
  CHECK(s.m[0].stage==SD_LOST&&s.m[1].stage==SD_LOST);CHECK(sd_get32(s.last[1][SD_LEAVE]+16)==1);scenarios++;
}
int main(int argc,char **argv){
  if(argc>1&&strcmp(argv[1],"--ack-loss")==0)test_ack_loss();
  else if(argc>1&&strcmp(argv[1],"--wire")==0)test_wire();
  else if(argc>1&&strcmp(argv[1],"--identity")==0)test_identity();
  else if(argc>1&&strcmp(argv[1],"--replay")==0)test_replays();
  else if(argc>1&&strcmp(argv[1],"--timeouts")==0)test_timeouts();
  else if(argc>1&&strcmp(argv[1],"--partitions")==0)test_partitions();
  else if(argc>1&&strcmp(argv[1],"--keepalive")==0)test_old_keepalive();
  else if(argc>1&&strcmp(argv[1],"--rooms")==0)test_room_bounds();
  else if(argc>1&&strcmp(argv[1],"--round-bound")==0)test_round_bound();
  else if(argc>1&&strcmp(argv[1],"--final-confirmation")==0)test_final_confirmation();
  else {
    test_wire();test_identity();test_replays();test_timeouts();
    test_match(1,0,0,0,0,0,0);test_match(2,4157,0,0,0,0,0);test_match(3,-27103,0,0,0,0,0);
    for(int i=0;i<24;i++)test_match((uint32_t)(123+i*739),i%2?15037:-9123,20,25,24,1,i%5==0);
    test_ack_loss();test_partitions();test_old_keepalive();test_room_bounds();test_round_bound();test_final_confirmation();
  }
  printf("{\"scenarios\":%d,\"assertions\":%d,\"completed_three_round_matches\":%d,\"match_packets_delivered\":%d,\"match_packets_dropped\":%d,\"match_duplicates_queued\":%d,\"match_out_of_order_deliveries\":%d,\"match_single_inflight_rejections\":%d,\"last_packets_delivered\":%d,\"device_tested\":false}\n",scenarios,assertions,completed_matches,sum_delivered,sum_dropped,sum_duplicates,sum_reordered,sum_busy,s.delivered);return 0;
}
