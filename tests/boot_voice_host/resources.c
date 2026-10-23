#include <stdlib.h>
#include "../../firmware/main/gfx/fc_res.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API void fc_res_init(const uint8_t*,uint32_t);
API const uint8_t *fc_res(const char*,uint32_t*);
void *av_malloc(size_t); void av_free(void*);
#define malloc av_malloc
#define free av_free
#include "../../firmware/main/gfx/fc_res.c"
