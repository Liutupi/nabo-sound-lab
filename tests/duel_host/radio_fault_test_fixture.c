#include "mock.h"
#include "sd_radio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define assert(c) do { ++checks; if (!(c)) { fprintf(stderr,"assertion failed at %s:%d: %s\n",__FILE__,__LINE__,#c);exit(3); } } while (0)
static int calls, fail_at, loop_live, wifi_live, started, now_live, peers, stop_fail;
static int64_t fake_us = 1234000;
static recvcb_t receive;
static sendcb_t sent;
static uint8_t last_dest[6];
static int step(void) {return ++calls == fail_at ? ESP_FAIL : ESP_OK;}
int64_t esp_timer_get_time(void) {return fake_us;}
esp_err_t esp_read_mac(uint8_t *out,int t) {(void)t;memcpy(out,"123456",6);return ESP_OK;}
const char *esp_err_to_name(esp_err_t e) {return e==ESP_OK?"ESP_OK":"mock error";}
esp_err_t esp_event_loop_create_default(void) {if(loop_live)return ESP_ERR_INVALID_STATE;int e=step();if(!e)loop_live=1;return e;}
esp_err_t esp_event_loop_delete_default(void) {assert(loop_live);loop_live=0;return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t *c) {assert(!wifi_live&&!c->nvs_enable);int e=step();if(!e)wifi_live=1;return e;}
esp_err_t esp_wifi_deinit(void) {assert(wifi_live);if(started)return ESP_ERR_WIFI_NOT_STOPPED;wifi_live=0;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int v) {assert(wifi_live&&v==WIFI_STORAGE_RAM);return step();}
esp_err_t esp_wifi_set_mode(int v) {assert(wifi_live&&v==WIFI_MODE_STA);return step();}
esp_err_t esp_wifi_start(void) {assert(wifi_live);int e=step();if(!e)started=1;return e;}
esp_err_t esp_wifi_stop(void) {assert(started);if(stop_fail){stop_fail=0;return ESP_FAIL;}started=0;return ESP_OK;}
esp_err_t esp_wifi_set_ps(int v) {assert(started&&v==WIFI_PS_NONE);return step();}
esp_err_t esp_wifi_set_channel(int v,int secondary) {assert(started&&v==6&&secondary==0);return step();}
esp_err_t esp_now_init(void) {assert(started);int e=step();if(!e)now_live=1;return e;}
esp_err_t esp_now_deinit(void) {assert(now_live);now_live=0;peers=0;return ESP_OK;}
esp_err_t esp_now_register_recv_cb(recvcb_t cb) {assert(now_live);int e=step();if(!e)receive=cb;return e;}
esp_err_t esp_now_register_send_cb(sendcb_t cb) {assert(now_live);int e=step();if(!e)sent=cb;return e;}
esp_err_t esp_now_unregister_recv_cb(void) {receive=0;return ESP_OK;}
esp_err_t esp_now_unregister_send_cb(void) {sent=0;return ESP_OK;}
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *p) {assert(now_live&&p->channel==6&&!p->encrypt);int e=step();if(!e)assert(++peers<=2);return e;}
esp_err_t esp_now_del_peer(const uint8_t *p) {assert(p&&peers==2);--peers;return ESP_OK;}
esp_err_t esp_now_send(const uint8_t *p,const uint8_t *d,size_t len) {assert(p&&d&&len==44);memcpy(last_dest,p,6);return step();}
static void reset(void) {calls=fail_at=0;assert(!loop_live&&!wifi_live&&!started&&!now_live&&!peers&&!receive&&!sent);}
int main(void) {
#ifdef TEST_STUB
    uint8_t data[44]={0}, mac[6]; uint32_t rx=1,drop=1,fail=1;
    assert(!sd_radio_begin()); assert(!sd_radio_send(0,data,44));assert(!sd_radio_poll(0));
    sd_radio_mac(mac);for(int i=0;i<6;i++)assert(!mac[i]);
    assert(sd_radio_now()==1234);assert(strstr(sd_radio_error(),"disabled"));
    sd_radio_stats(&rx,&drop,&fail);assert(!rx&&!drop&&!fail);sd_radio_stats(0,0,0);
    sd_radio_end();reset();printf("{\"checks\":%u,\"radio_initialized\":false}\n",checks);return 0;
#else
    for(int i=1;i<=11;++i){reset();fail_at=i;assert(!sd_radio_begin());reset();}
    assert(sd_radio_begin());assert(peers==1);assert(sd_radio_begin());
    uint8_t data[44]={0}, mac[6]={2,3,4,5,6,7}, outmac[6];
    sd_radio_mac(outmac);assert(!memcmp(outmac,"123456",6));assert(sd_radio_now()==1234);
    esp_now_recv_info_t info={mac};
    for(int i=0;i<14;++i){data[0]=i;fake_us+=1000;receive(&info,data,44);}
    receive(&info,data,43);uint32_t rx,drop,fail;sd_radio_stats(&rx,&drop,&fail);assert(rx==12&&drop==3&&!fail);
    sd_radio_rx_t packet;for(int i=0;i<12;++i){assert(sd_radio_poll(&packet));assert(packet.data[0]==i&&packet.at==(uint32_t)(1235+i)&&!memcmp(packet.mac,mac,6));}
    assert(!sd_radio_poll(&packet)&&!sd_radio_poll(0));
    receive(&info,data,43);receive(0,data,44);receive(&info,0,44);
    sd_radio_stats(&rx,&drop,&fail);assert(rx==12&&drop==6&&!fail);
    assert(!sd_radio_send(0,data,43));assert(!sd_radio_send(0,0,44));assert(sd_radio_send(0,data,44));
    for(int i=0;i<6;++i)assert(last_dest[i]==255);
    assert(!sd_radio_send(mac,data,44));sent(0,ESP_NOW_SEND_FAIL);
    assert(sd_radio_send(mac,data,44));assert(peers==2);sent(0,ESP_NOW_SEND_SUCCESS);
    mac[5]++;assert(sd_radio_send(mac,data,44));assert(peers==2);sent(0,ESP_NOW_SEND_SUCCESS);
    fail_at=calls+1;assert(!sd_radio_send(mac,data,44));fail_at=0;assert(sd_radio_send(mac,data,44));sent(0,ESP_NOW_SEND_SUCCESS);
    sd_radio_stats(&rx,&drop,&fail);assert(fail==2);
    recvcb_t stale_receive=receive;sendcb_t stale_sent=sent;
    sd_radio_end();reset();stale_receive(&info,data,44);stale_sent(0,ESP_NOW_SEND_FAIL);
    sd_radio_stats(&rx,&drop,&fail);assert(rx==12&&drop==6&&fail==2);
    assert(!sd_radio_send(mac,data,44));sd_radio_end();reset();
    loop_live=1;assert(sd_radio_begin());sd_radio_end();assert(loop_live);loop_live=0;reset();
    assert(sd_radio_begin());stop_fail=1;sd_radio_end();assert(wifi_live&&started&&loop_live);
    assert(sd_radio_begin());sd_radio_end();reset();
    for(int i=0;i<100;++i){assert(sd_radio_begin());sd_radio_end();reset();}
    printf("{\"checks\":%u,\"initialization_failure_points\":11,\"queue_capacity\":12,\"peer_cap\":2,\"single_inflight\":true,\"late_callbacks_ignored\":true,\"owned_and_borrowed_loop\":true,\"stop_failure_recovery\":true,\"clean_exit_cycles\":100}\n",checks);return 0;
#endif
}
