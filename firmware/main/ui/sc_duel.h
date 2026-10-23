#pragma once
#include <stddef.h>
#include <stdint.h>
void sl_duel_status(char *out, size_t n);
/* Test fixtures are absent from ESP firmware. */
#ifndef ESP_PLATFORM
void sl_duel_fixture(int stage, int outcome);
#endif
