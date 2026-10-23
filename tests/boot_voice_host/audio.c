#include <stdlib.h>
#include "../../firmware/main/ui/sl_audio.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API void sl_audio_init(void);
API void sl_audio_fill(int16_t*,int);
API bool sl_audio_busy(void);
API uint32_t sl_audio_time(void);
API void sl_audio_stop_all(void);
API bool sl_boot_voice_start(void);
API void sl_boot_voice_stop(void);
API bool sl_boot_voice_playing(void);
API void sl_pluck(int,int,int,int);
API void sl_rec_begin(const int16_t*,int);
API void sl_rec_push(const int16_t*,int);
API int sl_rec_end(void);
API void sl_voice_play(int);
API bool sl_voice_playing(void);
void *av_malloc(size_t); void av_free(void*);
#define malloc av_malloc
#define free av_free
#include "../../firmware/main/ui/sl_audio.c"
#undef malloc
#undef free
API uint32_t av_boot_pos(void){return g_boot_pos;}
API uint32_t av_boot_samples(void){return g_boot_samples;}
API const uint8_t *av_boot_pointer(void){return g_boot_pcm;}
