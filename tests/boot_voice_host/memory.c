/* Test-only allocator counters. Production code is included unchanged by wrappers. */
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static struct { void *p; size_t n; } blocks[32];
static unsigned live, peak, calls, releases, lock_depth, lock_max, bad_unlock;
void *av_malloc(size_t n) {
  void *p=malloc(n);
  if(p){for(int i=0;i<32;i++)if(!blocks[i].p){blocks[i].p=p;blocks[i].n=n;break;}
    live+=(unsigned)n;calls++;if(live>peak)peak=live;}
  return p;
}
void av_free(void *p){
  if(p)for(int i=0;i<32;i++)if(blocks[i].p==p){live-=(unsigned)blocks[i].n;blocks[i].p=NULL;releases++;break;}
  free(p);
}
void sl_audio_lock(void){lock_depth++;if(lock_depth>lock_max)lock_max=lock_depth;}
void sl_audio_unlock(void){if(lock_depth)lock_depth--;else bad_unlock++;}
API void av_memory_reset(void){peak=live;calls=releases=lock_max=bad_unlock=0;}
API unsigned av_memory(int field){switch(field){case 0:return live;case 1:return peak;case 2:return calls;case 3:return releases;case 4:return lock_max;case 5:return bad_unlock;default:return lock_depth;}}
