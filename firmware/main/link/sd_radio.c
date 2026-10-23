#include "sd_radio.h"
#include "esp_timer.h"
#ifdef SOUNDLAB_WIRELESS_DUEL
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"

/* Static storage stays valid even while unregistering callbacks at shutdown.
 * No heap allocation, parsing, UI, logging or waiting runs in callbacks. */
enum { RX_CAPACITY = 12 };
static sd_radio_rx_t s_rxq[RX_CAPACITY];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_head, s_count;
static uint32_t s_received, s_dropped, s_failed;
static bool s_running, s_inflight;
static esp_err_t s_error = ESP_OK;
/* Only the serialized UI caller touches driver-ownership flags / peer cache. */
static bool s_loop_owned, s_wifi_init, s_wifi_started, s_now_init;
static bool s_peer_valid;
static uint8_t s_peer[6];
static const uint8_t s_broadcast[6] = {255, 255, 255, 255, 255, 255};

static void set_error(esp_err_t err)
{
    portENTER_CRITICAL(&s_lock);
    s_error = err;
    portEXIT_CRITICAL(&s_lock);
}

uint32_t sd_radio_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    uint32_t at = sd_radio_now();
    portENTER_CRITICAL(&s_lock);
    if (s_running) {
        if (!info || !info->src_addr || !data || len != SD_RADIO_PACKET_BYTES ||
            s_count == RX_CAPACITY) {
            ++s_dropped;
        } else {
            sd_radio_rx_t *slot = &s_rxq[(s_head + s_count) % RX_CAPACITY];
            memcpy(slot->mac, info->src_addr, sizeof slot->mac);
            memcpy(slot->data, data, sizeof slot->data);
            slot->at = at;
            ++s_count;
            ++s_received;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

/* IDF 5.5.3 uses esp_now_send_info_t, not the older MAC-pointer signature. */
static void send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    portENTER_CRITICAL(&s_lock);
    if (s_running && s_inflight) {
        s_inflight = false;
        if (status != ESP_NOW_SEND_SUCCESS) {
            ++s_failed;
            s_error = ESP_FAIL;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

/* Reverse-order teardown, including partially initialized starts. Leave an
 * ownership flag set if its teardown failed, so a later begin retries cleanup
 * rather than losing ownership or trying to initialize a second driver. */
static esp_err_t cleanup(void)
{
    esp_err_t first = ESP_OK, err;
    portENTER_CRITICAL(&s_lock);
    s_running = s_inflight = false;
    s_head = s_count = 0;
    portEXIT_CRITICAL(&s_lock);
    if (s_now_init) {
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        err = esp_now_deinit();
        if (err == ESP_OK || err == ESP_ERR_ESPNOW_NOT_INIT) {
            s_now_init = s_peer_valid = false;
        } else { first = err; }
    }
    if (s_wifi_started) {
        err = esp_wifi_stop();
        if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT)
            s_wifi_started = false;
        else if (first == ESP_OK) first = err;
    }
    if (s_wifi_init) {
        err = esp_wifi_deinit();
        if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT) {
            s_wifi_init = s_wifi_started = false;
        } else if (first == ESP_OK) first = err;
    }
    if (s_loop_owned && !s_wifi_init) {
        err = esp_event_loop_delete_default();
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) s_loop_owned = false;
        else if (first == ESP_OK) first = err;
    }
    return first;
}

void sd_radio_end(void)
{
    esp_err_t err = cleanup();
    if (err != ESP_OK) set_error(err);
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = SD_RADIO_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}

bool sd_radio_begin(void)
{
    esp_err_t err;
    portENTER_CRITICAL(&s_lock);
    bool running = s_running;
    portEXIT_CRITICAL(&s_lock);
    if (running) return true;
    err = cleanup();
    if (err != ESP_OK || s_wifi_init || s_now_init || s_loop_owned) {
        set_error(err != ESP_OK ? err : ESP_ERR_INVALID_STATE);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_received = s_dropped = s_failed = 0;
    s_error = ESP_OK;
    portEXIT_CRITICAL(&s_lock);
    /* ESP-NOW is layer 2: no netif / lwIP, association, DHCP or IP task is
     * created. The default event loop belongs to us only if we created it. */
    err = esp_event_loop_create_default();
    if (err == ESP_OK) s_loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) goto failed;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = 0; /* Also forced off by the isolated SDK config. */
    err = esp_wifi_init(&config);
    if (err != ESP_OK) goto failed;
    s_wifi_init = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto failed;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto failed;
    err = esp_wifi_start();
    if (err != ESP_OK) goto failed;
    s_wifi_started = true;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) goto failed;
    err = esp_wifi_set_channel(SD_RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) goto failed;
    err = esp_now_init();
    if (err != ESP_OK) goto failed;
    s_now_init = true;
    err = esp_now_register_recv_cb(recv_cb);
    if (err != ESP_OK) goto failed;
    err = esp_now_register_send_cb(send_cb);
    if (err != ESP_OK) goto failed;
    err = add_peer(s_broadcast);
    if (err != ESP_OK) goto failed;
    portENTER_CRITICAL(&s_lock);
    s_running = true;
    portEXIT_CRITICAL(&s_lock);
    return true;
failed:
    cleanup();
    set_error(err);
    return false;
}

bool sd_radio_send(const uint8_t to[6], const uint8_t *data, int len)
{
    const uint8_t *dest = to ? to : s_broadcast;
    if (!data || len != SD_RADIO_PACKET_BYTES) {
        set_error(ESP_ERR_INVALID_ARG);
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    bool available = s_running && !s_inflight;
    portEXIT_CRITICAL(&s_lock);
    if (!available) return false;
    esp_err_t err;
    if (memcmp(dest, s_broadcast, 6) != 0 &&
        (!s_peer_valid || memcmp(dest, s_peer, 6) != 0)) {
        /* Keep memory bounded even when finding several rooms: only one
         * unicast peer exists, in addition to the explicit broadcast peer. */
        if (s_peer_valid) {
            err = esp_now_del_peer(s_peer);
            if (err != ESP_OK && err != ESP_ERR_ESPNOW_NOT_FOUND) goto failed;
            s_peer_valid = false;
        }
        err = add_peer(dest);
        if (err != ESP_OK) goto failed;
        memcpy(s_peer, dest, 6);
        s_peer_valid = true;
    }
    portENTER_CRITICAL(&s_lock);
    s_inflight = true;
    portEXIT_CRITICAL(&s_lock);
    err = esp_now_send(dest, data, SD_RADIO_PACKET_BYTES);
    if (err == ESP_OK) return true;
failed:
    portENTER_CRITICAL(&s_lock);
    s_inflight = false;
    ++s_failed;
    s_error = err;
    portEXIT_CRITICAL(&s_lock);
    return false;
}

bool sd_radio_poll(sd_radio_rx_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_lock);
    bool has = s_running && s_count != 0;
    if (has) {
        *out = s_rxq[s_head];
        s_head = (s_head + 1) % RX_CAPACITY;
        --s_count;
    }
    portEXIT_CRITICAL(&s_lock);
    return has;
}

void sd_radio_mac(uint8_t out[6])
{
    if (!out) return;
    if (esp_read_mac(out, ESP_MAC_WIFI_STA) != ESP_OK) memset(out, 0, 6);
}

const char *sd_radio_error(void)
{
    portENTER_CRITICAL(&s_lock);
    esp_err_t err = s_error;
    portEXIT_CRITICAL(&s_lock);
    return esp_err_to_name(err);
}

void sd_radio_stats(uint32_t *rx, uint32_t *drop, uint32_t *fail)
{
    portENTER_CRITICAL(&s_lock);
    if (rx) *rx = s_received;
    if (drop) *drop = s_dropped;
    if (fail) *fail = s_failed;
    portEXIT_CRITICAL(&s_lock);
}
#else
/* A normal build still links the scene API without pulling in Wi-Fi. */
bool sd_radio_begin(void) { return false; }
void sd_radio_end(void) {}
bool sd_radio_send(const uint8_t to[6], const uint8_t *data, int len)
{ (void)to; (void)data; (void)len; return false; }
bool sd_radio_poll(sd_radio_rx_t *out) { (void)out; return false; }
void sd_radio_mac(uint8_t out[6]) { if (out) for (int i = 0; i < 6; ++i) out[i] = 0; }
uint32_t sd_radio_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
const char *sd_radio_error(void) { return "wireless duel disabled in this build"; }
void sd_radio_stats(uint32_t *rx, uint32_t *drop, uint32_t *fail)
{ if (rx) *rx = 0; if (drop) *drop = 0; if (fail) *fail = 0; }
#endif
