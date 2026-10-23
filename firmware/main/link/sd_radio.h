/* ESP-NOW transport, deliberately separate from the deterministic duel model.
 * UI-thread begin/end/send/poll calls must be serialized. Callbacks only copy
 * exact 44-byte packets into a bounded queue or release the one TX slot. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SD_RADIO_PACKET_BYTES = 44, SD_RADIO_CHANNEL = 6 };
typedef struct { uint8_t mac[6], data[44]; uint32_t at; } sd_radio_rx_t;

bool sd_radio_begin(void);
void sd_radio_end(void);
/* NULL means the broadcast MAC, never ESP-NOW's "send to all peers" option. */
bool sd_radio_send(const uint8_t to[6], const uint8_t *data, int len);
bool sd_radio_poll(sd_radio_rx_t *out);
void sd_radio_mac(uint8_t out[6]);
uint32_t sd_radio_now(void); /* monotonic milliseconds, modulo 2^32 */
const char *sd_radio_error(void);
/* Counts reset on begin; rx=enqueued, drop=invalid/full, fail=actual TX error.
 * Busy-slot retries are normal backpressure and do not increment fail. */
void sd_radio_stats(uint32_t *rx, uint32_t *drop, uint32_t *fail);
