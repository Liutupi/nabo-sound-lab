// main/main.c — 《NABO 声音实验室》AI Passport 独立固件：启动、按键、送屏、声音（全双工录放 + 分析）、设置存档、休眠。
//
// 开机直接进 NABO 欢迎动画，不配网、不联网。整片 Flash 只装这一个程序。
//   ▲ 单击：调（数值 / 选项），双击：返回；▼：换（模式 / 样式）；OK：做（主动作），复读要按住
// 5 分钟无操作先存档再深睡，任意键唤醒（重新启动）。可视化工具正在听到声音时不睡。
// 串口（USB）调试命令，每行一条：status、perf、go <场景>、key <u|d|o> <1|0>、calm <0|1>、sens <dB>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "ui/sl_ui.h"
#include "ui/sc_newgames.h"

static const char *TAG = "soundlab";

#define IDLE_SLEEP_US (5LL * 60 * 1000 * 1000)
#define BACKLIGHT 85
#define FRAME_MS 33
#define NVS_NS "soundlab"

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_dma[2];
static int s_which;
static SemaphoreHandle_t s_free, s_audio_mx;
static QueueHandle_t s_keys;
static TaskHandle_t s_audio_task_handle;
static _Atomic int64_t s_last_input_us;
static atomic_bool s_sleep_now, s_save_pending;
static int64_t s_save_at;
static bool s_audio_ok;
static nm_state_t s_nm;                 // 约 20 KB
static int16_t s_ring[4096];            // 最近 0.25 秒麦克风采样（示波器、录音预卷）
static _Atomic uint32_t s_wp;
static _Atomic uint32_t s_physical_press[3], s_physical_release[3], s_key_queue_drop;
static _Atomic int s_physical_last_key = -1, s_physical_last_mv = -1;
static _Atomic int64_t s_physical_last_us;
static bool s_input_diag;              // Physical-event logging is opt-in; counters are always live.
static uint32_t s_input_diag_drop;
static _Atomic uint8_t s_target_volume = 80;
static atomic_bool s_volume_dirty = true;

typedef struct { uint8_t key, down; int16_t mv; int64_t at_us; } key_ev_t;
_Static_assert(sizeof(key_ev_t) <= 16, "physical input event exceeds queue budget");

/* ---------- 送屏：fc_gfx_end 每合成好一条 8 行就回调一次 ---------- */
static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free, &woken);
    return woken == pdTRUE;
}
static void flush(int y0, int rows, const uint16_t *pix, void *ctx)
{
    xSemaphoreTake(s_free, portMAX_DELAY);
    uint16_t *b = s_dma[s_which];
    s_which ^= 1;
    int n = FC_W * rows;
    for (int i = 0; i < n; i++) b[i] = (uint16_t)(pix[i] << 8 | pix[i] >> 8);   // 面板要大端
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, y0, FC_W, y0 + rows, b) != ESP_OK) xSemaphoreGive(s_free);
}
static void flush_wait(void)
{
    xSemaphoreTake(s_free, portMAX_DELAY); xSemaphoreTake(s_free, portMAX_DELAY);
    xSemaphoreGive(s_free); xSemaphoreGive(s_free);
}

/* ---------- 平台钩子（sl_ui / sl_audio 要的） ---------- */
void sl_audio_lock(void) { if (s_audio_mx) xSemaphoreTake(s_audio_mx, portMAX_DELAY); }
void sl_audio_unlock(void) { if (s_audio_mx) xSemaphoreGive(s_audio_mx); }
void sl_mic_last(int16_t *dst, int n)
{
    uint32_t w = atomic_load(&s_wp);
    for (int i = 0; i < n; i++) dst[i] = s_ring[(w - (uint32_t)n + (uint32_t)i) & 4095];
}
uint32_t sl_mic_samples(void) { return atomic_load(&s_wp); }
void sl_settings_save(void) { s_save_at = esp_timer_get_time() + 1000000; atomic_store(&s_save_pending, true); }   // 1 秒后再写，减少磨损
void sl_volume_apply(uint8_t volume)
{
    atomic_store(&s_target_volume, volume > 100 ? 100 : volume);
    atomic_store(&s_volume_dirty, true); /* Codec access belongs to the audio task. */
}
static void save_now(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "set", &SET, sizeof SET);
    nvs_set_u8(h, "vol", SET.volume);
    nvs_set_blob(h, "rec", &REC, sizeof REC);
    nvs_commit(h);
    nvs_close(h);
    atomic_store(&s_save_pending, false);
}
static void load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    sl_settings_t s; sl_records_t r; size_t n = sizeof s;
    if (nvs_get_blob(h, "set", &s, &n) == ESP_OK && n == sizeof s && s.a4 >= 415 && s.a4 <= 446 && s.quiet >= 40 && s.quiet <= 80 && s.cal >= 80 && s.cal <= 130) SET = s;
    /* Legacy byte9 was padding: its usual zero must not mute an upgrade. */
    uint8_t volume = 80;
    SET.volume = 80;
    if (nvs_get_u8(h, "vol", &volume) == ESP_OK && volume <= 100 && volume % 10 == 0) SET.volume = volume;
    n = sizeof r;
    if (nvs_get_blob(h, "rec", &r, &n) == ESP_OK && n == sizeof r && r.rope_n <= 12) REC = r;
    nvs_close(h);
}

/* ---------- 按键（esp_timer 任务里记只读诊断并入队，不打印） ---------- */
static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_RELEASE) return;
    const int key = btn == BSP_BTN_UP ? K_UP : btn == BSP_BTN_DOWN ? K_DOWN : K_OK;
    const int64_t at_us = esp_timer_get_time();
    /* button_level has returned before this callback; its ADC lock is released.
     * This diagnostic reading does not replace the BSP's debounce sample. */
    const int mv = bsp_button_read_mv();
    atomic_store(&s_last_input_us, at_us);
    atomic_store(&s_physical_last_key, key);
    atomic_store(&s_physical_last_mv, mv);
    atomic_store(&s_physical_last_us, at_us);
    if (ev == BSP_BTN_PRESS) atomic_fetch_add(&s_physical_press[key], 1u);
    else atomic_fetch_add(&s_physical_release[key], 1u);
    key_ev_t e = {.key = (uint8_t)key, .down = ev == BSP_BTN_PRESS, .mv = (int16_t)mv, .at_us = at_us};
    if (xQueueSend(s_keys, &e, 0) != pdTRUE) atomic_fetch_add(&s_key_queue_drop, 1u);
}

/* ---------- 深睡 ---------- */
static void warn(const char *what, esp_err_t err) { if (err != ESP_OK) ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err)); }
static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "idle, entering deep sleep");
    if (atomic_load(&s_save_pending)) save_now();
    bsp_display_backlight(0);
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart();
    warn("gpio wake", esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));
    warn("battery sleep", bsp_battery_sleep());
    if (s_audio_ok) { warn("audio sleep", bsp_audio_sleep()); warn("i2s release", bsp_audio_prepare_deep_sleep()); }
    warn("i2c release", bsp_i2c_prepare_deep_sleep());
    warn("display sleep", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    esp_restart();
}

/* ---------- 音频：每 20 ms 放一帧、录一帧、分析一帧 ---------- */
static uint32_t s_hops, s_hop_max_us; static uint64_t s_hop_us;
static uint32_t s_fills, s_fill_max_us, s_write_errors, s_read_errors; static uint64_t s_fill_us;
static void audio_task(void *arg)
{
    static int16_t out[NM_HOP], in[NM_HOP];
    for (;;) {
        if (atomic_load(&s_sleep_now)) enter_deep_sleep();
        if (s_audio_ok && atomic_exchange(&s_volume_dirty, false)) bsp_audio_set_volume(atomic_load(&s_target_volume));
        int64_t fill_at = esp_timer_get_time();
        sl_audio_fill(out, NM_HOP);
        uint32_t fill_us = (uint32_t)(esp_timer_get_time() - fill_at);
        s_fills++; s_fill_us += fill_us; if (fill_us > s_fill_max_us) s_fill_max_us = fill_us;
        const uint8_t volume = atomic_load(&s_target_volume);
        /* Codec volume0 is -50 dB, not a true mute. Send exact silence at0%. */
        if (!volume) memset(out, 0, sizeof out);
        bool busy = volume && sl_audio_busy();
        bool io_ok = s_audio_ok;
        if (io_ok && bsp_audio_write(out, sizeof out) != ESP_OK) { s_write_errors++; io_ok = false; }
        if (io_ok && bsp_audio_read(in, sizeof in) != ESP_OK) { s_read_errors++; io_ok = false; }
        if (!io_ok) { memset(in, 0, sizeof in); vTaskDelay(pdMS_TO_TICKS(20)); }
        uint32_t w = atomic_load(&s_wp);
        for (int i = 0; i < NM_HOP; i++) s_ring[(w + (uint32_t)i) & 4095] = in[i];
        atomic_store(&s_wp, w + NM_HOP);
        if (sl_rec_on()) sl_rec_push(in, NM_HOP);
        if (busy) { sl_mic_skip(); continue; }       // 自己在发声：整帧跳过分析（不送 0）
        int64_t t0 = esp_timer_get_time();
        nm_set_features(&s_nm, sl_ui_mic_features());
        nm_set_sens(&s_nm, SET.sens);
        sl_mic_hop(nm_process(&s_nm, in), sl_audio_time());
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        s_hops++; s_hop_us += us; if (us > s_hop_max_us) s_hop_max_us = us;
    }
}

/* ---------- 串口调试 ---------- */
static uint32_t s_frames, s_frame_slow, s_strips;
static uint64_t s_frame_us, s_frame_max;
static int s_force;
static bool s_capture_pending, s_capture_active, s_mic_capture_pending;
static void command(char *cmd, char *out, size_t n)
{
    int v;
    if (!strcmp(cmd, "perf")) {
        snprintf(out, n, "frames=%u avg_ms=%u max_ms=%u over_33ms=%u strips/frame=%u ops=%d hops=%u hop_avg_us=%u hop_max_us=%u fill_avg_us=%u fill_max_us=%u audio_write_err=%u audio_read_err=%u", (unsigned)s_frames,
                 (unsigned)(s_frames ? s_frame_us / s_frames / 1000 : 0), (unsigned)(s_frame_max / 1000), (unsigned)s_frame_slow,
                 (unsigned)(s_frames ? s_strips / s_frames : 0), fc_gfx_ops(), (unsigned)s_hops, (unsigned)(s_hops ? s_hop_us / s_hops : 0), (unsigned)s_hop_max_us,
                 (unsigned)(s_fills ? s_fill_us / s_fills : 0), (unsigned)s_fill_max_us, (unsigned)s_write_errors, (unsigned)s_read_errors);
        s_frames = s_frame_us = s_frame_max = s_frame_slow = s_strips = 0; s_hops = 0; s_hop_us = 0; s_hop_max_us = 0;
        s_fills = s_fill_max_us = 0; s_fill_us = 0;
    } else if (!strcmp(cmd, "capture")) { s_capture_pending = true; snprintf(out, n, "capture=queued"); }
    else if (!strcmp(cmd, "miccapture")) { s_mic_capture_pending = true; snprintf(out, n, "miccapture=queued"); }
    else if (!strncmp(cmd, "go ", 3)) { sl_ui_go_id(cmd + 3); s_force = 1; snprintf(out, n, "scene=%s", sl_ui_scene()->id); }
    else if (!strncmp(cmd, "key ", 4) && strlen(cmd) >= 7) { int k = cmd[4] == 'u' ? K_UP : cmd[4] == 'd' ? K_DOWN : K_OK; sl_ui_key(k, cmd[6] == '1'); snprintf(out, n, "key %d %c src=serial", k, cmd[6]); }
    else if (sscanf(cmd, "diag %d", &v) == 1) { s_input_diag = v != 0; snprintf(out, n, "diag=%u", (unsigned)s_input_diag); }
    else if (sscanf(cmd, "calm %d", &v) == 1) { SET.calm = (uint8_t)(v != 0); sl_settings_save(); snprintf(out, n, "calm=%d", SET.calm); }
    else if (sscanf(cmd, "sens %d", &v) == 1) { SET.sens = (int16_t)v; snprintf(out, n, "sens=%d", SET.sens); }
    else {
        snprintf(out, n, "scene=%s version=%s db=%d f0=%u f0_x10=%u conf=%u busy=%u bpm=%u chord=%d a4=%d calm=%u volume=%u sens=%d cache=%u batt=%dmV ui_stack_min=%u audio_stack_min=%u onset_drop=%u reset_reason=%d wake_cause=%d press_u=%u press_d=%u press_o=%u release_u=%u release_d=%u release_o=%u queue_drop=%u last_key=%d last_mv=%d last_t_us=%llu diag=%u diag_drop=%u", sl_ui_scene()->id, CONFIG_APP_PROJECT_VER, MIC.db_x100 / 100,
                 MIC.f0_x10 / 10, MIC.f0_x10, (unsigned)MIC.f0_conf_x1000, (unsigned)sl_audio_busy(), MIC.bpm_x10 / 10, MIC.chord, SET.a4, SET.calm, SET.volume, SET.sens, (unsigned)fc_img_cache_bytes(), bsp_battery_mv(),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)(s_audio_task_handle ? uxTaskGetStackHighWaterMark(s_audio_task_handle) : 0), (unsigned)sl_onset_dropped(),
                 (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(),
                 (unsigned)atomic_load(&s_physical_press[K_UP]), (unsigned)atomic_load(&s_physical_press[K_DOWN]), (unsigned)atomic_load(&s_physical_press[K_OK]),
                 (unsigned)atomic_load(&s_physical_release[K_UP]), (unsigned)atomic_load(&s_physical_release[K_DOWN]), (unsigned)atomic_load(&s_physical_release[K_OK]),
                 (unsigned)atomic_load(&s_key_queue_drop), atomic_load(&s_physical_last_key), atomic_load(&s_physical_last_mv), (unsigned long long)atomic_load(&s_physical_last_us),
                 (unsigned)s_input_diag, (unsigned)s_input_diag_drop);
        size_t used = strlen(out);
        if (used + 2 < n) { out[used++] = ' '; sl_newgames_status(out + used, n - used); }
    }
}
static void console_poll(void)
{
    static char line[96]; static int len;
    uint8_t buf[32];
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
    for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c != '\r' && c != '\n') { if (len < (int)sizeof(line) - 1) line[len++] = c; continue; }
        if (!len) continue;
        line[len] = 0; len = 0;
        static char out[768];
        char *cmd = line[0] == '>' ? line + 1 : line;
        command(cmd, out, sizeof out);
        printf("SL %s heap=%u largest=%u uptime_ms=%u\n", out, (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)(esp_timer_get_time() / 1000));
        atomic_store(&s_last_input_us, esp_timer_get_time());
    }
}

/* ---------- 界面 ---------- */
static int s_strip_count;
static void flush_count(int y0, int rows, const uint16_t *pix, void *ctx) {
    s_strip_count++;
    if (s_capture_active) {
        /* One row only (~1KB), no 153600B full-screen allocation. */
        static char line[16 + FC_W * 4];
        static const char hex[] = "0123456789abcdef";
        for (int y = 0; y < rows; y++) {
            int p = snprintf(line, sizeof line, "SLPIX %03d ", y0 + y);
            for (int x = 0; x < FC_W; x++) {
                unsigned v = pix[y * FC_W + x];
                line[p++] = hex[(v >> 12) & 15]; line[p++] = hex[(v >> 8) & 15];
                line[p++] = hex[(v >> 4) & 15]; line[p++] = hex[v & 15];
            }
            line[p] = 0;
            printf("%s\n", line);
        }
    }
    flush(y0, rows, pix, ctx);
}
static bool keep_awake(void)
{
    const char *id = sl_ui_scene()->id;   // 可视化工具开着而且有声音时不睡
    return MIC_LIVE && (!strcmp(id, "spec") || !strcmp(id, "water") || !strcmp(id, "scope") || !strcmp(id, "db") || !strcmp(id, "chroma") || !strcmp(id, "beat"));
}
/* Diagnostic only: freeze 128 ms before printing. The 256 ms ring leaves a
 * full 128 ms safety margin; retry if an audio hop interrupted the copy. */
static void mic_snapshot(void)
{
    enum { COUNT = 2048, ROW = 32 };
    int16_t *pcm = malloc(COUNT * sizeof *pcm);
    if (!pcm) { printf("SL_MIC_ERROR reason=no_memory\n"); return; }
    uint32_t end = 0; bool stable = false;
    for (int attempt = 0; attempt < 3 && !stable; attempt++) {
        end = atomic_load(&s_wp);
        for (int i = 0; i < COUNT; i++) pcm[i] = s_ring[(end - COUNT + (uint32_t)i) & 4095];
        stable = atomic_load(&s_wp) == end;
    }
    if (!stable) { free(pcm); printf("SL_MIC_ERROR reason=copy_interrupted\n"); return; }
    printf("SL_MIC_BEGIN sr=16000 samples=%u format=PCM16HEX start=%u end=%u scene=%s db=%d f0_x10=%u conf=%u busy=%u\n",
           COUNT, (unsigned)(end - COUNT), (unsigned)end, sl_ui_scene()->id, MIC.db_x100 / 100,
           MIC.f0_x10, (unsigned)MIC.f0_conf_x1000, (unsigned)sl_audio_busy());
    static const char hex[] = "0123456789abcdef";
    char line[16 + ROW * 4];
    for (int at = 0; at < COUNT; at += ROW) {
        int pos = snprintf(line, sizeof line, "SLMIC %04u ", (unsigned)at);
        for (int i = 0; i < ROW; i++) {
            unsigned v = (uint16_t)pcm[at + i];
            line[pos++] = hex[(v >> 12) & 15]; line[pos++] = hex[(v >> 8) & 15];
            line[pos++] = hex[(v >> 4) & 15]; line[pos++] = hex[v & 15];
        }
        line[pos] = 0; printf("%s\n", line);
    }
    free(pcm);
    printf("SL_MIC_END samples=%u\n", COUNT);
}
static void ui_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    int64_t last = esp_timer_get_time();
    for (;;) {
        key_ev_t ev;
        while (xQueueReceive(s_keys, &ev, 0) == pdTRUE) {
            if (s_input_diag) {
                char line[160];
                int len = snprintf(line, sizeof line, "SL INPUT src=physical key=%u down=%u mv=%d t_us=%llu scene=%s\n", (unsigned)ev.key, (unsigned)ev.down,
                                   (int)ev.mv, (unsigned long long)ev.at_us, sl_ui_scene()->id);
                /* Never wait for a host, including if USB is unplugged with diag enabled. */
                if (len <= 0 || len >= (int)sizeof line || !usb_serial_jtag_is_connected() ||
                    usb_serial_jtag_write_bytes(line, (size_t)len, 0) != len) s_input_diag_drop++;
            }
            sl_ui_key(ev.key, ev.down);
        }
        console_poll();
        if (s_mic_capture_pending) { s_mic_capture_pending = false; mic_snapshot(); }
        int64_t t0 = esp_timer_get_time();
        int dt = (int)((t0 - last) / 1000);
        if (dt > 50) dt = 50;
        if (dt < 1) dt = 1;
        last += (int64_t)dt * 1000;
        if (t0 - last > 50000) last = t0;
        sl_ui_step(dt);
        s_strip_count = 0;
        bool captured = s_capture_pending;
        s_capture_pending = false; s_capture_active = captured;
        if (captured) printf("SL_CAPTURE_BEGIN w=240 h=320 fmt=RGB565HEX scene=%s t=%u\n", sl_ui_scene()->id, (unsigned)VT);
        sl_ui_render(flush_count, NULL, s_force || captured);
        if (captured) { flush_wait(); printf("SL_CAPTURE_END rows=320\n"); }
        s_capture_active = false;
        s_force = 0;
        uint64_t used = (uint64_t)(esp_timer_get_time() - t0);
        if (!captured) {  /* Diagnostic USB transfer is not normal render cost. */
            s_frames++; s_frame_us += used; s_strips += (uint32_t)s_strip_count;
            if (used > s_frame_max) s_frame_max = used;
            if (used > FRAME_MS * 1000) s_frame_slow++;
        }
        if (atomic_load(&s_save_pending) && esp_timer_get_time() > s_save_at) save_now();
        if (keep_awake()) atomic_store(&s_last_input_us, esp_timer_get_time());
        if (esp_timer_get_time() - atomic_load(&s_last_input_us) > IDLE_SLEEP_US) {
            flush_wait();
            atomic_store(&s_sleep_now, true);
            vTaskSuspend(NULL);
        }
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS)) == pdFALSE) { wake = xTaskGetTickCount(); vTaskDelay(1); }
    }
}

/* ---------- 启动 ---------- */
static const uint8_t *map_assets(uint32_t *size)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "assets");
    if (!p) return NULL;
    uint8_t head[8];
    if (esp_partition_read(p, 0, head, sizeof head) != ESP_OK || memcmp(head, "FNAS", 4)) return NULL;
    uint32_t n = head[6] | head[7] << 8, end = 0;
    for (uint32_t i = 0; i < n && i < 512; i++) {
        uint8_t e[8];
        if (esp_partition_read(p, 8 + 40 * i + 32, e, sizeof e) != ESP_OK) return NULL;
        uint32_t off = e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24, len = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
        if (off + len > end) end = off + len;
    }
    if (!end || end > p->size) return NULL;
    const void *ptr = NULL;
    esp_partition_mmap_handle_t h;
    uint32_t len = (end + 0xFFFF) & ~0xFFFFu;
    if (len > p->size) len = p->size;
    if (esp_partition_mmap(p, 0, len, ESP_PARTITION_MMAP_DATA, &ptr, &h) != ESP_OK) return NULL;
    ESP_LOGI(TAG, "assets %u bytes mapped (%u entries)", (unsigned)end, (unsigned)n);
    *size = end;
    return ptr;
}
static void fatal_screen(void)
{
    uint16_t *b = s_dma[0];
    for (int i = 0; i < FC_W * FC_STRIP; i++) b[i] = 0x00F8;   // 红（大端）
    for (int y = 0; y < FC_H; y += FC_STRIP) {
        xSemaphoreTake(s_free, portMAX_DELAY);
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, FC_W, y + FC_STRIP, b) != ESP_OK) xSemaphoreGive(s_free);
    }
    bsp_display_backlight(BACKLIGHT);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); err = nvs_flash_init(); }
    warn("nvs", err);
    usb_serial_jtag_driver_config_t uc = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&uc) == ESP_OK) usb_serial_jtag_vfs_use_driver();

    ESP_ERROR_CHECK(bsp_display_init());
    s_panel = bsp_display_panel();
    s_free = xSemaphoreCreateCounting(2, 2);
    s_audio_mx = xSemaphoreCreateMutex();
    for (int i = 0; i < 2; i++) {
        s_dma[i] = heap_caps_malloc(FC_W * FC_STRIP * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_ERROR_CHECK(s_dma[i] ? ESP_OK : ESP_ERR_NO_MEM);
    }
    esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(bsp_display_io(), &cbs, NULL));

    warn("battery", bsp_battery_init());
    s_audio_ok = bsp_audio_init() == ESP_OK && bsp_audio_set_format(NM_SR, 16, 1) == ESP_OK;
    if (!s_audio_ok) ESP_LOGE(TAG, "audio unavailable, running silent");

    uint32_t size = 0;
    const uint8_t *blob = map_assets(&size);
    if (!blob || !sl_ui_init(blob, size, esp_random())) {
        ESP_LOGE(TAG, "assets missing or broken: flash soundlab_assets.bin to the assets partition");
        fatal_screen();
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    load_nvs();
    sl_volume_apply(SET.volume);
    nm_init(&s_nm, SET.sens);
    sl_ui_go_id("boot");
    atomic_store(&s_last_input_us, esp_timer_get_time());
    s_keys = xQueueCreate(16, sizeof(key_ev_t));
    sl_ui_render(flush, NULL, true);
    flush_wait();
    bsp_display_backlight(BACKLIGHT);

    xTaskCreate(ui_task, "sl_ui", 8192, NULL, 6, NULL);
    xTaskCreate(audio_task, "sl_audio", 6144, NULL, 8, &s_audio_task_handle);
    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    ESP_LOGI(TAG, "NABO sound lab ready, free heap %u, largest %u", (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
