/* Compile the exact renderer plus host-only static-storage size probes. */
#ifdef PF_BASELINE
#include "baseline/fc_gfx_0_1_1.c"
#else
#include "../../firmware/main/gfx/fc_gfx.c"
#endif
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API unsigned pf_op_size(void) { return (unsigned)sizeof(op_t); }
API unsigned pf_display_list_bytes(void) { return (unsigned)sizeof(g_ops); }
