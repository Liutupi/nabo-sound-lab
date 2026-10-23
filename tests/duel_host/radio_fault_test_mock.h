#ifndef RADIO_HOST_MOCK_H
#define RADIO_HOST_MOCK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_WIFI_NOT_INIT 3
#define ESP_ERR_WIFI_NOT_STARTED 4
#define ESP_ERR_WIFI_NOT_STOPPED 5
#define ESP_ERR_ESPNOW_NOT_INIT 6
#define ESP_ERR_ESPNOW_NOT_FOUND 7
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 1
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
#define ESP_MAC_WIFI_STA 0
#define ESP_NOW_SEND_SUCCESS 0
#define ESP_NOW_SEND_FAIL 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef struct {uint8_t *src_addr;} esp_now_recv_info_t;
typedef struct {uint8_t *des_addr;} esp_now_send_info_t;
typedef int esp_now_send_status_t;
typedef struct {uint8_t peer_addr[6]; int channel, ifidx; bool encrypt;} esp_now_peer_info_t;
typedef struct {int nvs_enable;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){1})
typedef void (*recvcb_t)(const esp_now_recv_info_t *, const uint8_t *, int);
typedef void (*sendcb_t)(const esp_now_send_info_t *, esp_now_send_status_t);
int64_t esp_timer_get_time(void);
esp_err_t esp_read_mac(uint8_t *, int);
const char *esp_err_to_name(esp_err_t);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_set_channel(int,int);
esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_recv_cb(recvcb_t);
esp_err_t esp_now_register_send_cb(sendcb_t);
esp_err_t esp_now_unregister_recv_cb(void);
esp_err_t esp_now_unregister_send_cb(void);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *);
esp_err_t esp_now_del_peer(const uint8_t *);
esp_err_t esp_now_send(const uint8_t *,const uint8_t *,size_t);
#endif
