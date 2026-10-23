#pragma once
#include "duel_rules.h"
#define SD_WIRE 44
#define SD_ROOMS 4
enum { SD_CHOOSE, SD_SEARCH, SD_HOST, SD_REQUEST, SD_JOIN, SD_LOBBY, SD_ARMED, SD_PLAY, SD_RESULT, SD_FINAL, SD_LOST };
enum { SD_ADV=1, SD_JOIN_MSG, SD_ACCEPT, SD_READY, SD_PING, SD_PONG, SD_START, SD_ACK, SD_SCORE, SD_LEAVE };
typedef bool (*sd_send_fn)(void *ctx, const uint8_t *to, const uint8_t *data, int len);
typedef struct { uint8_t mac[6]; uint32_t session, seen; uint16_t room; } sd_room_t;
typedef struct {
  uint8_t stage, host, round, ready, peer_ready, paired, ack, got_score, peer_got_score, room_n;
  uint8_t self[6], peer[6], sync_n;
  uint32_t session, seed, seq, rx_seq, now, since, rx_at, tx_at, deadline, ping_at, best_rtt;
  int32_t offset;
  bool ping_pending;
  uint16_t room, scores[3], theirs[3];
  sd_room_t rooms[SD_ROOMS];
  sd_score_t score;
  sd_send_fn send; void *ctx;
} sd_model_t;
void sd_init(sd_model_t *m, const uint8_t mac[6], sd_send_fn send, void *ctx, uint32_t now);
void sd_host(sd_model_t *m, uint32_t nonce, uint32_t now);
void sd_search(sd_model_t *m, uint32_t now);
bool sd_join(sd_model_t *m, int room, uint32_t now);
void sd_ok(sd_model_t *m, uint32_t now);
void sd_tick(sd_model_t *m, uint32_t now);
void sd_receive(sd_model_t *m, const uint8_t mac[6], const uint8_t *data, int len, uint32_t received_at);
void sd_leave(sd_model_t *m, uint32_t now);
int sd_tap(sd_model_t *m, uint32_t pressed_at);
int32_t sd_elapsed(const sd_model_t *m, uint32_t now);
bool sd_settled(const sd_model_t *m);
/* Wire codec is explicit little-endian; no packed-struct/ABI dependency. */
uint32_t sd_get32(const uint8_t *p);
void sd_put32(uint8_t *p, uint32_t v);
bool sd_valid(const uint8_t *p, int n);
