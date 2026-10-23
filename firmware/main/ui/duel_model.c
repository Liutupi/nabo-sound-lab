/* Pure bounded state machine; radio callbacks never call this module. */
#include "duel_model.h"
#include <string.h>
#include <limits.h>
uint32_t sd_get32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
void sd_put32(uint8_t *p, uint32_t v) { for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
static uint32_t hash(const uint8_t *p) { uint32_t h=2166136261u; for(int i=0;i<40;i++) h=(h^p[i])*16777619u; return h; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0]|p[1]<<8); }
static bool same(const uint8_t *a,const uint8_t *b) { return memcmp(a,b,6)==0; }
static bool address(const uint8_t *a) { static const uint8_t zero[6]={0}; return !(a[0]&1) && !same(a,zero); }
bool sd_valid(const uint8_t *p,int n) {
  if(!p || n!=SD_WIRE || memcmp(p,"NSD1",4) || p[4]!=1 || p[5]<SD_ADV || p[5]>SD_LEAVE ||
     p[6]>3 || (p[7]&~3) || !sd_get32(p+8) || p[38] || p[39] || sd_get32(p+40)!=hash(p)) return false;
  for(int i=0;i<3;i++) if(get16(p+32+2*i)>1000) return false;
  return true;
}
static void emit(sd_model_t *m,int type,uint32_t a,uint32_t b,uint32_t c) {
  uint8_t p[SD_WIRE]={0}; memcpy(p,"NSD1",4); p[4]=1; p[5]=(uint8_t)type; p[6]=m->round;
  p[7]=(uint8_t)((m->ready?1:0)|(m->got_score?2:0)); sd_put32(p+8,m->session); sd_put32(p+12,++m->seq);
  sd_put32(p+16,a); sd_put32(p+20,b); sd_put32(p+24,c); sd_put32(p+28,m->seed);
  for(int i=0;i<3;i++){p[32+2*i]=(uint8_t)m->scores[i];p[33+2*i]=(uint8_t)(m->scores[i]>>8);}
  sd_put32(p+40,hash(p));
  if(m->send) m->send(m->ctx,type==SD_ADV?NULL:m->peer,p,sizeof p);
}
static void state(sd_model_t *m,int s,uint32_t now) { m->stage=(uint8_t)s; m->since=now; m->tx_at=now-200; }
static void fail(sd_model_t *m,uint32_t now) { if(m->paired)emit(m,SD_LEAVE,1,0,0);state(m,SD_LOST,now); }
void sd_init(sd_model_t *m,const uint8_t mac[6],sd_send_fn send,void *ctx,uint32_t now) {
  memset(m,0,sizeof *m); memcpy(m->self,mac,6); m->send=send;m->ctx=ctx;m->now=now;m->best_rtt=UINT32_MAX;
}
static void reset(sd_model_t *m,uint32_t now) { uint8_t mac[6];memcpy(mac,m->self,6);sd_init(m,mac,m->send,m->ctx,now); }
void sd_host(sd_model_t *m,uint32_t nonce,uint32_t now) {
  reset(m,now);m->host=1;m->session=nonce?nonce:1;m->seed=m->session^0xa7d1e503u;
  m->room=(uint16_t)(1000+m->session%9000);state(m,SD_HOST,now);
}
void sd_search(sd_model_t *m,uint32_t now) {reset(m,now);state(m,SD_SEARCH,now);}
bool sd_join(sd_model_t *m,int room,uint32_t now) {
  if(m->stage!=SD_SEARCH||room<0||room>=m->room_n||now-m->rooms[room].seen>3000)return false;
  sd_room_t *r=&m->rooms[room]; memcpy(m->peer,r->mac,6);m->session=r->session;m->room=r->room;
  m->rx_at=now;state(m,SD_JOIN,now);return true;
}
void sd_leave(sd_model_t *m,uint32_t now) {if(m->session&&m->stage!=SD_HOST&&m->stage!=SD_SEARCH)emit(m,SD_LEAVE,0,0,0);reset(m,now);}
bool sd_settled(const sd_model_t *m){return m->got_score&&m->peer_got_score;}
void sd_ok(sd_model_t *m,uint32_t now) {
  m->now=now;
  if(m->stage==SD_REQUEST){m->paired=1;m->rx_at=now;state(m,SD_LOBBY,now);}
  else if(m->stage==SD_LOBBY && (m->host||m->sync_n>=3))m->ready=1;
  else if(m->stage==SD_RESULT&&m->round<3&&sd_settled(m))m->ready=1;
}
static void start(sd_model_t *m,int round,uint32_t deadline,uint32_t now){
  m->round=(uint8_t)round;m->deadline=deadline;m->ready=m->peer_ready=m->got_score=m->peer_got_score=m->ack=0;
  sd_score_init(&m->score,m->seed,round);state(m,SD_ARMED,now);
}
int32_t sd_elapsed(const sd_model_t *m,uint32_t now){return(int32_t)(now-m->deadline);}
int sd_tap(sd_model_t *m,uint32_t pressed_at){return m->stage==SD_PLAY?sd_score_tap(&m->score,sd_elapsed(m,pressed_at)):-1;}
void sd_tick(sd_model_t *m,uint32_t now){
  m->now=now;
  if(m->stage==SD_CHOOSE||m->stage==SD_LOST)return;
  if(m->stage==SD_SEARCH){
    for(int i=m->room_n-1;i>=0;i--)if(now-m->rooms[i].seen>3000)m->rooms[i]=m->rooms[--m->room_n];
    if(now-m->since>60000)fail(m,now);
    return;
  }
  if((m->stage==SD_HOST||m->stage==SD_REQUEST)&&now-m->since>60000){fail(m,now);return;}
  if(m->stage>=SD_JOIN&&m->stage<SD_FINAL&&now-m->rx_at>8000){fail(m,now);return;}
  if(m->host&&m->ready&&m->peer_ready&&(m->stage==SD_LOBBY||(m->stage==SD_RESULT&&m->round<3&&sd_settled(m))))
    start(m,m->round+1,now+2500,now);
  if(m->stage==SD_ARMED){
    int32_t el=sd_elapsed(m,now);
    if(!m->ack&&el>=(m->host?-500:-300)){fail(m,now);return;}
    if(el>=0){state(m,SD_PLAY,now);}
  }
  if(m->stage==SD_PLAY&&sd_elapsed(m,now)>m->score.duration){
    m->scores[m->round-1]=(uint16_t)sd_score_value(&m->score);state(m,SD_RESULT,now);
  }
  /* Host is authoritative once it has both validated scores. Its repeated
   * SCORE(got_score=1) is the guest's final certificate; a lost last ACK must
   * not make an already certified guest win turn into a host abort. */
  if(m->stage==SD_RESULT&&m->round==3&&(m->host?m->got_score:sd_settled(m)))state(m,SD_FINAL,now);
  if(now-m->tx_at<200)return;
  m->tx_at=now;
  switch(m->stage){
    case SD_HOST:emit(m,SD_ADV,m->room,0,0);break;
    case SD_JOIN:emit(m,SD_JOIN_MSG,m->room,0,0);break;
    case SD_LOBBY:
      if(m->host)emit(m,SD_ACCEPT,m->room,0,0);
      else if(m->ready)emit(m,SD_READY,0,0,0);
      else if(!m->ping_pending){m->ping_at=now;m->ping_pending=true;emit(m,SD_PING,now,0,0);}
      if(m->ping_pending&&now-m->ping_at>1000)m->ping_pending=false;
      break;
    case SD_ARMED:case SD_PLAY:emit(m,m->host?SD_START:SD_ACK,m->deadline+(m->host?0:(uint32_t)m->offset),m->host?m->ack:0,0);break;
    case SD_RESULT:case SD_FINAL:emit(m,SD_SCORE,m->deadline+(m->host?0:(uint32_t)m->offset),0,0);break;
    default:break;
  }
}
static void discover(sd_model_t *m,const uint8_t *mac,const uint8_t *p,uint32_t now){
  uint32_t room=sd_get32(p+16);if(room<1000||room>9999||p[6])return;
  int i;for(i=0;i<m->room_n;i++)if(same(m->rooms[i].mac,mac))break;
  if(i==SD_ROOMS)return;
  if(i==m->room_n)m->room_n++;
  sd_room_t *r=&m->rooms[i];memcpy(r->mac,mac,6);r->session=sd_get32(p+8);r->room=(uint16_t)room;r->seen=now;
}
void sd_receive(sd_model_t *m,const uint8_t mac[6],const uint8_t *p,int n,uint32_t rx){
  if(!address(mac)||same(mac,m->self)||!sd_valid(p,n))return;
  int type=p[5],round=p[6];uint32_t a=sd_get32(p+16),b=sd_get32(p+20),c=sd_get32(p+24),session=sd_get32(p+8);
  uint32_t now=m->now;
  if(m->stage==SD_SEARCH&&type==SD_ADV){discover(m,mac,p,rx);return;}
  if(session!=m->session||m->stage==SD_CHOOSE||m->stage==SD_LOST)return;
  if(type==SD_JOIN_MSG&&(m->stage==SD_HOST||m->stage==SD_REQUEST)&&!round&&a==m->room){
    if(m->stage==SD_HOST){memcpy(m->peer,mac,6);state(m,SD_REQUEST,now);}return;
  }
  if(!same(mac,m->peer))return;
  uint32_t seq=sd_get32(p+12);if(!seq||(m->rx_seq&&(int32_t)(seq-m->rx_seq)<=0))return;m->rx_seq=seq;
  if(type==SD_LEAVE){if(m->stage!=SD_FINAL||a==1)fail(m,now);return;}
  if(type==SD_ACCEPT&&!m->host&&!round&&a==m->room&&(m->stage==SD_JOIN||m->stage==SD_LOBBY)){
    if(m->stage==SD_JOIN){m->seed=sd_get32(p+28);m->paired=1;state(m,SD_LOBBY,now);}
    m->peer_ready=(p[7]&1);m->rx_at=rx;return;
  }
  if(!m->paired||sd_get32(p+28)!=m->seed)return;
  if(type==SD_PING&&m->host&&m->stage==SD_LOBBY&&!round){emit(m,SD_PONG,a,rx,now);m->rx_at=rx;return;}
  if(type==SD_PONG&&!m->host&&m->stage==SD_LOBBY&&m->ping_pending&&a==m->ping_at){
    m->ping_pending=false;int32_t rtt=(int32_t)(rx-a)-(int32_t)(c-b);
    if(rtt<0||rtt>80)return;
    int32_t offset=(int32_t)(((int64_t)(int32_t)(b-a)+(int32_t)(c-rx))/2);
    if(m->sync_n<255)m->sync_n++;
    if((uint32_t)rtt<m->best_rtt){m->best_rtt=(uint32_t)rtt;m->offset=offset;}
    m->rx_at=rx;return;
  }
  if(type==SD_READY&&m->host&&m->stage==SD_LOBBY&&!round){m->peer_ready=(p[7]&1);m->rx_at=rx;return;}
  if(type==SD_START&&!m->host){
    if(round==m->round+1&&m->ready&&m->sync_n>=3&&
       (m->stage==SD_LOBBY||(m->stage==SD_RESULT&&m->got_score))){
      int32_t until=(int32_t)(a-(now+(uint32_t)m->offset));
      if(until<500||until>3000){fail(m,now);return;}
      start(m,round,a,now);m->deadline=a-(uint32_t)m->offset;
    }
    if(round==m->round&&(m->stage==SD_ARMED||m->stage==SD_PLAY)&&a==m->deadline+(uint32_t)m->offset){m->ack|=b==1;m->rx_at=rx;}
    return;
  }
  /* Host deadlines are used on wire on both ends; guest plays on its local clock. */
  uint32_t wire_deadline=m->deadline+(m->host?0:(uint32_t)m->offset);
  if(round!=m->round||a!=wire_deadline)return;
  if(type==SD_ACK&&m->host&&(m->stage==SD_ARMED||m->stage==SD_PLAY)){m->ack=1;m->rx_at=rx;return;}
  if(type==SD_SCORE&&(m->stage==SD_PLAY||m->stage==SD_RESULT||m->stage==SD_FINAL)&&round>=1){
    if(sd_elapsed(m,now)<m->score.duration-200)return;
    for(int i=0;i<round-1;i++)if(get16(p+32+i*2)!=m->theirs[i])return;
    uint16_t score=get16(p+32+(round-1)*2);
    if(m->got_score&&score!=m->theirs[round-1])return;
    m->theirs[round-1]=score;m->got_score=1;m->peer_got_score|=!!(p[7]&2);m->peer_ready|=!!(p[7]&1);m->rx_at=rx;
  }
}
