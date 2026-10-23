#include <stdlib.h>
#include "../../firmware/main/ui/sl_sampler.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API bool sl_sampler_open(void);
API void sl_sampler_close(void);
API void sl_sampler_begin(void);
API void sl_sampler_push(const int16_t*,int);
API int sl_sampler_end(void);
API bool sl_sampler_recording(void);
API bool sl_sampler_playing(void);
API bool sl_sampler_play(int);
void *av_malloc(size_t); void av_free(void*);
#define malloc av_malloc
#define free av_free
#include "../../firmware/main/ui/sl_sampler.c"
