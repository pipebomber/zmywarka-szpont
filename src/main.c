/* Szpont Washer ESP - watek glowny (JC3248W535EN) */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "config.h"
#include "esp_bsp.h"
#include "display.h"
#include "lv_port.h"
#include "screen_off.h"
#include "midea_api.h"
#include "wifi_manager.h"
#include "ota_server.h"
#include "mdns_mini.h"
#include "weather.h"
#include "ui.h"

static const char *TAG = "SZPONT_MAIN";

#define APP_DISPLAY_ROTATION    LV_DISP_ROT_90

typedef enum {
    CMD_POLL = 1,
    CMD_WIFI_CONNECTED,
    CMD_WIFI_DISCONNECTED,
    CMD_START_1H,
    CMD_START_1H_DRY,
    CMD_START_30MIN,
    CMD_START_ECO,
    CMD_START_INTENSIVE,
    CMD_CANCEL,
    CMD_DISCOVER_IP,
    CMD_ENTER_SLEEP,
    CMD_WAKE_UP,
    CMD_SCHED_30M,
    CMD_SCHED_1H,
    CMD_SCHED_2H,
    CMD_SCHED_3H,
    CMD_SCHED_CANCEL,
    CMD_OTA_OPEN,
    CMD_OTA_CLOSE,
    CMD_SYNC_CLOCK,
    CMD_WEATHER_UPDATE
} app_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;
static midea_status_t s_last_status = {0};
static volatile bool s_is_sleeping = false;
static bool s_sntp_started = false;

static int64_t s_sched_target_us = 0;
static midea_program_t s_sched_prog = MIDEA_PROG_1H;
static bool s_sched_dry = false;
static bool s_sched_restored = false;

static void on_ota_progress(int pct, bool finished, bool ok)
{
    screen_off_activity();
    screen_off_set_keep_awake(true);
    if (bsp_display_lock(150)) {
        ui_set_ota_progress(pct, finished, ok);
        bsp_display_unlock();
    }
}

static void save_schedule_nvs(int64_t target_epoch_s, midea_program_t prog, bool dry)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    if (target_epoch_s > 0) {
        nvs_set_i64(nvs, "sched_ep", target_epoch_s);
        nvs_set_u8(nvs, "sched_pr", (uint8_t)prog);
        nvs_set_u8(nvs, "sched_dr", dry ? 1 : 0);
    } else {
        nvs_erase_key(nvs, "sched_ep");
        nvs_erase_key(nvs, "sched_pr");
        nvs_erase_key(nvs, "sched_dr");
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void restore_schedule_from_nvs_once(void)
{
    if (s_sched_restored) return;
    time_t now = 0;
    time(&now);
    if (now < 1700000000LL) return; // Wait until NTP provides valid epoch time

    s_sched_restored = true;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;

    int64_t saved_ep = 0;
    uint8_t pr = (uint8_t)MIDEA_PROG_1H;
    uint8_t dr = 0;
    esp_err_t err = nvs_get_i64(nvs, "sched_ep", &saved_ep);
    nvs_get_u8(nvs, "sched_pr", &pr);
    nvs_get_u8(nvs, "sched_dr", &dr);
    nvs_close(nvs);

    if (err != ESP_OK || saved_ep <= 0) return;

    int64_t rem_s = saved_ep - (int64_t)now;
    if (rem_s > 5 && rem_s <= (4 * 3600)) {
        s_sched_prog = (midea_program_t)pr;
        s_sched_dry = (dr != 0);
        s_sched_target_us = esp_timer_get_time() + (rem_s * 1000000LL);
        ESP_LOGI(TAG, "Restored persisted schedule from NVS: %llds left (prog=%u dry=%u)",
                 rem_s, pr, dr);
        if (bsp_display_lock(200)) {
            ui_set_selected_options(s_sched_prog, s_sched_dry);
            ui_set_schedule_countdown((int)rem_s);
            bsp_display_unlock();
        }
    } else if (rem_s <= 5 && rem_s >= -60) {
        s_sched_prog = (midea_program_t)pr;
        s_sched_dry = (dr != 0);
        s_sched_target_us = esp_timer_get_time() + 1000000LL;
        save_schedule_nvs(0, 0, false);
        ESP_LOGI(TAG, "Restored due schedule from NVS (firing in 1s)");
    } else {
        save_schedule_nvs(0, 0, false);
        ESP_LOGI(TAG, "Cleared expired NVS schedule (delta=%llds)", rem_s);
    }
}

static void send_cmd(app_cmd_t cmd)
{
    if (s_cmd_queue) {
        xQueueSend(s_cmd_queue, &cmd, 0);
    }
}

static void on_weather_update(const weather_data_t *data)
{
    (void)data;
    send_cmd(CMD_WEATHER_UPDATE);
}

static void on_sntp_time_sync(struct timeval *tv)
{
    (void)tv;
    send_cmd(CMD_SYNC_CLOCK);
}

static void start_sntp_once(void)
{
    if (!s_sntp_started) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "Starting fast SNTP time sync (CET/CEST Poland)...");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_set_time_sync_notification_cb(on_sntp_time_sync);
        esp_sntp_setservername(0, "162.159.200.1"); // Cloudflare Anycast NTP (no DNS delay)
        esp_sntp_setservername(1, "216.239.35.0");  // Google Anycast NTP (no DNS delay)
        esp_sntp_setservername(2, "pool.ntp.org");
        esp_sntp_init();
        return;
    }

    time_t now = 0;
    struct tm t = {0};
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < (2024 - 1900)) {
        ESP_LOGI(TAG, "Retrying SNTP sync after Wi-Fi reconnect...");
        esp_sntp_restart();
    }
}

static void on_ui_action(ui_action_t action)
{
    switch (action) {
        case UI_ACTION_START_1H:        send_cmd(CMD_START_1H); break;
        case UI_ACTION_START_1H_DRY:    send_cmd(CMD_START_1H_DRY); break;
        case UI_ACTION_START_30MIN:     send_cmd(CMD_START_30MIN); break;
        case UI_ACTION_START_ECO:       send_cmd(CMD_START_ECO); break;
        case UI_ACTION_START_INTENSIVE: send_cmd(CMD_START_INTENSIVE); break;
        case UI_ACTION_CANCEL:          send_cmd(CMD_CANCEL); break;
        case UI_ACTION_RETRY:        send_cmd(CMD_POLL); break;
        case UI_ACTION_DISCOVER_IP:  send_cmd(CMD_DISCOVER_IP); break;
        case UI_ACTION_SLEEP_NOW:    screen_off_force_off(); break;
        case UI_ACTION_SCHED_30M:    send_cmd(CMD_SCHED_30M); break;
        case UI_ACTION_SCHED_1H:     send_cmd(CMD_SCHED_1H); break;
        case UI_ACTION_SCHED_2H:     send_cmd(CMD_SCHED_2H); break;
        case UI_ACTION_SCHED_3H:     send_cmd(CMD_SCHED_3H); break;
        case UI_ACTION_SCHED_CANCEL: send_cmd(CMD_SCHED_CANCEL); break;
        case UI_ACTION_OTA_OPEN:     send_cmd(CMD_OTA_OPEN); break;
        case UI_ACTION_OTA_CLOSE:    send_cmd(CMD_OTA_CLOSE); break;
        default: break;
    }
}

static void on_screen_power_change(bool sleeping)
{
    send_cmd(sleeping ? CMD_ENTER_SLEEP : CMD_WAKE_UP);
}

static void on_wifi_event(app_wifi_event_t event)
{
    switch (event) {
        case APP_WIFI_CONNECTED:
            start_sntp_once();
            if (!s_is_sleeping) {
                send_cmd(CMD_WIFI_CONNECTED);
            }
            break;
        case APP_WIFI_DISCONNECTED:
            if (!s_is_sleeping) {
                send_cmd(CMD_WIFI_DISCONNECTED);
            }
            break;
    }
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void update_power_hold_flags(void)
{
    bool work = (s_last_status.is_online && s_last_status.cycle_state == MIDEA_STATE_WORK);
    bool finish = (s_last_status.is_online && s_last_status.cycle_state == MIDEA_STATE_FINISH && !ui_is_finish_dismissed());
    screen_off_set_keep_awake(ota_server_is_running() || work);
    screen_off_set_finish_hold(finish);
}

static void execute_start_cmd(midea_program_t prog, bool extra_dry, TickType_t *last_poll_tick)
{
    uint8_t expected_mode = midea_program_mode_code(prog);
    const char *busy_msg = "Włączanie: 30 Minut...";
    if (prog == MIDEA_PROG_1H) busy_msg = extra_dry ? "Włączanie: 1H + Susz..." : "Włączanie: 1 Godzina...";
    else if (prog == MIDEA_PROG_ECO) busy_msg = "Włączanie: Eco 50°C...";
    else if (prog == MIDEA_PROG_INTENSIVE) busy_msg = "Włączanie: Intensywny...";

    if (bsp_display_lock(200)) {
        ui_set_busy(true, busy_msg);
        bsp_display_unlock();
    }

    for (int w = 0; w < 30 && !wifi_manager_is_connected(); w++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (wifi_manager_is_connected()) {
        bool was_off = !s_last_status.is_online || !s_last_status.machine_on;
        midea_start_program(prog, was_off, extra_dry);
        vTaskDelay(pdMS_TO_TICKS(700));

        // Verify dishwasher actually entered WORK state with requested program
        midea_query_status(&s_last_status);
        if (s_last_status.is_online && s_last_status.door_closed &&
            (s_last_status.cycle_state != MIDEA_STATE_WORK || s_last_status.mode_code != expected_mode)) {
            if (bsp_display_lock(200)) {
                ui_set_busy(true, "Potwierdzanie programu...");
                bsp_display_unlock();
            }
            midea_start_program(prog, !s_last_status.machine_on, extra_dry);
            vTaskDelay(pdMS_TO_TICKS(700));
            midea_query_status(&s_last_status);
        }
    }

    int rssi = wifi_manager_get_rssi();
    if (bsp_display_lock(200)) {
        ui_set_busy(false, NULL);
        ui_update_status(&s_last_status, wifi_manager_is_connected(), rssi);
        bsp_display_unlock();
    }
    update_power_hold_flags();
    if (last_poll_tick) *last_poll_tick = xTaskGetTickCount();
}

static void worker_task(void *pvParameters)
{
    (void)pvParameters;
    TickType_t last_poll_tick = 0;

    while (1) {
        app_cmd_t cmd = 0;
        TickType_t wait_ticks = (s_is_sleeping && s_sched_target_us == 0)
            ? portMAX_DELAY
            : pdMS_TO_TICKS(500);

        if (xQueueReceive(s_cmd_queue, &cmd, wait_ticks) == pdTRUE) {
            if (cmd == CMD_ENTER_SLEEP) {
                if (!s_is_sleeping) {
                    s_is_sleeping = true;
                    ota_server_stop();
                    ESP_LOGI(TAG, "Entering sleep: stopping Wi-Fi and preloading wake screen");
                    wifi_manager_stop();
                    if (bsp_display_lock(200)) {
                        ui_set_busy(false, NULL);
                        ui_prepare_wake_screen();
                        screen_off_set_sleep_prerender(true);
                        lv_obj_invalidate(lv_scr_act());
                        lv_refr_now(NULL);
                        screen_off_set_sleep_prerender(false);
                        bsp_display_unlock();
                    }
                }
                continue;
            }

            if (cmd == CMD_WAKE_UP) {
                if (s_is_sleeping) {
                    s_is_sleeping = false;
                    ESP_LOGI(TAG, "Waking up: reconnecting Wi-Fi");
                    wifi_manager_start();
                    if (bsp_display_lock(200)) {
                        lv_obj_invalidate(lv_scr_act());
                        bsp_display_unlock();
                    }
                    last_poll_tick = 0;
                }
                continue;
            }

            if (cmd == CMD_SCHED_30M || cmd == CMD_SCHED_1H || cmd == CMD_SCHED_2H || cmd == CMD_SCHED_3H) {
                int delay_sec = 3600;
                if (cmd == CMD_SCHED_30M) delay_sec = 30 * 60;
                else if (cmd == CMD_SCHED_2H) delay_sec = 2 * 3600;
                else if (cmd == CMD_SCHED_3H) delay_sec = 3 * 3600;
                ui_get_selected_options(&s_sched_prog, &s_sched_dry);
                s_sched_target_us = esp_timer_get_time() + ((int64_t)delay_sec * 1000000LL);
                time_t now = 0;
                time(&now);
                if (now >= 1700000000LL) {
                    s_sched_restored = true;
                    save_schedule_nvs((int64_t)now + (int64_t)delay_sec, s_sched_prog, s_sched_dry);
                }
                ESP_LOGI(TAG, "Scheduled start in %d min (prog=%d dry=%d)", delay_sec / 60, (int)s_sched_prog, s_sched_dry);
                if (bsp_display_lock(200)) {
                    ui_set_schedule_countdown(delay_sec);
                    bsp_display_unlock();
                }
                continue;
            }

            if (cmd == CMD_SCHED_CANCEL) {
                s_sched_target_us = 0;
                save_schedule_nvs(0, 0, false);
                if (bsp_display_lock(200)) {
                    ui_set_schedule_countdown(0);
                    ui_update_status(&s_last_status, wifi_manager_is_connected(), wifi_manager_get_rssi());
                    bsp_display_unlock();
                }
                continue;
            }

            if (cmd == CMD_SYNC_CLOCK) {
                time_t now = 0;
                struct tm t = {0};
                time(&now);
                localtime_r(&now, &t);
                ESP_LOGI(TAG, "Clock synced: %04d-%02d-%02d %02d:%02d:%02d",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec);
                restore_schedule_from_nvs_once();
                if (!s_is_sleeping && bsp_display_lock(100)) {
                    ui_update_clock();
                    bsp_display_unlock();
                }
                if (!s_is_sleeping && wifi_manager_is_connected()) {
                    weather_poll_if_due();
                }
                continue;
            }

            if (cmd == CMD_WEATHER_UPDATE) {
                weather_data_t w = {0};
                weather_get_cached(&w);
                if (bsp_display_lock(100)) {
                    ui_set_weather(&w);
                    bsp_display_unlock();
                }
                continue;
            }

            if (s_is_sleeping) continue;

            if (cmd == CMD_WIFI_DISCONNECTED) {
                if (bsp_display_lock(200)) {
                    ui_update_status(&s_last_status, false, -100);
                    bsp_display_unlock();
                }
            } else if (cmd == CMD_WIFI_CONNECTED) {
                mdns_mini_start("zmywarka");
                mdns_mini_announce();
                if (!ui_is_on_main_screen()) {
                    char dish_ip[16] = DISHWASHER_IP;
                    midea_api_get_ip(dish_ip, sizeof(dish_ip));
                    char detail[48];
                    snprintf(detail, sizeof(detail), "Zmywarka: %s", dish_ip);
                    if (bsp_display_lock(200)) {
                        ui_show_loading("Sprawdzanie zmywarki...", detail);
                        bsp_display_unlock();
                    }
                }
                cmd = CMD_POLL;
            }

            if (cmd == CMD_OTA_OPEN) {
                screen_off_set_keep_awake(true);
                mdns_mini_announce();
                ota_server_start();
                char ip[16] = "192.168.18.88";
                wifi_manager_get_ip(ip, sizeof(ip));
                if (bsp_display_lock(200)) {
                    ui_set_ota_url(ip);
                    bsp_display_unlock();
                }
            } else if (cmd == CMD_OTA_CLOSE) {
                ota_server_stop();
                update_power_hold_flags();
            } else if (cmd == CMD_POLL) {
                if (wifi_manager_is_connected()) {
                    midea_status_t st;
                    midea_query_status(&st);
                    s_last_status = st;
                    int rssi = wifi_manager_get_rssi();
                    if (bsp_display_lock(200)) {
                        ui_update_status(&s_last_status, true, rssi);
                        bsp_display_unlock();
                    }
                    update_power_hold_flags();
                    last_poll_tick = xTaskGetTickCount();
                }
            } else if (cmd == CMD_START_1H || cmd == CMD_START_1H_DRY || cmd == CMD_START_30MIN ||
                       cmd == CMD_START_ECO || cmd == CMD_START_INTENSIVE) {
                s_sched_target_us = 0;
                save_schedule_nvs(0, 0, false);
                if (bsp_display_lock(200)) {
                    ui_set_schedule_countdown(0);
                    bsp_display_unlock();
                }
                midea_program_t prog = MIDEA_PROG_30MIN;
                if (cmd == CMD_START_1H || cmd == CMD_START_1H_DRY) prog = MIDEA_PROG_1H;
                else if (cmd == CMD_START_ECO) prog = MIDEA_PROG_ECO;
                else if (cmd == CMD_START_INTENSIVE) prog = MIDEA_PROG_INTENSIVE;
                execute_start_cmd(prog, (cmd == CMD_START_1H_DRY), &last_poll_tick);
            } else if (cmd == CMD_CANCEL) {
                if (bsp_display_lock(200)) {
                    ui_set_busy(true, "Zatrzymywanie zmywarki...");
                    bsp_display_unlock();
                }

                for (int w = 0; w < 30 && !wifi_manager_is_connected() && !s_is_sleeping; w++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                if (wifi_manager_is_connected()) {
                    midea_cancel();
                    vTaskDelay(pdMS_TO_TICKS(800));
                    midea_query_status(&s_last_status);

                    if (s_last_status.is_online && s_last_status.cycle_state == MIDEA_STATE_WORK) {
                        if (bsp_display_lock(200)) {
                            ui_set_busy(true, "Potwierdzanie zatrzymania...");
                            bsp_display_unlock();
                        }
                        midea_cancel();
                        vTaskDelay(pdMS_TO_TICKS(800));
                        midea_query_status(&s_last_status);
                    }
                }

                int rssi = wifi_manager_get_rssi();
                if (bsp_display_lock(200)) {
                    ui_set_busy(false, NULL);
                    ui_update_status(&s_last_status, wifi_manager_is_connected(), rssi);
                    bsp_display_unlock();
                }
                update_power_hold_flags();
                last_poll_tick = xTaskGetTickCount();
            } else if (cmd == CMD_DISCOVER_IP) {
                if (bsp_display_lock(200)) {
                    ui_set_busy(true, "Szukanie zmywarki w sieci Wi-Fi...");
                    bsp_display_unlock();
                }
                char found_ip[16] = {0};
                if (wifi_manager_is_connected() && midea_discover_ip(found_ip, sizeof(found_ip)) == ESP_OK) {
                    midea_query_status(&s_last_status);
                }
                int rssi = wifi_manager_get_rssi();
                if (bsp_display_lock(200)) {
                    ui_set_busy(false, NULL);
                    ui_update_status(&s_last_status, wifi_manager_is_connected(), rssi);
                    bsp_display_unlock();
                }
                last_poll_tick = xTaskGetTickCount();
            }
        }

        // sprawdzenie opoznionego startu (dziala tez gdy ekran spi)
        if (s_sched_target_us > 0) {
            int64_t now_us = esp_timer_get_time();
            if (now_us >= s_sched_target_us) {
                s_sched_target_us = 0;
                save_schedule_nvs(0, 0, false);
                ESP_LOGI(TAG, "Scheduled start timer fired!");
                if (s_is_sleeping) {
                    screen_off_activity();
                    s_is_sleeping = false;
                    wifi_manager_start();
                }
                for (int w = 0; w < 40 && !wifi_manager_is_connected(); w++) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                if (bsp_display_lock(200)) {
                    ui_set_schedule_countdown(0);
                    bsp_display_unlock();
                }
                execute_start_cmd(s_sched_prog, s_sched_dry, &last_poll_tick);
            } else if (!s_is_sleeping) {
                int rem_s = (int)((s_sched_target_us - now_us) / 1000000LL);
                if (bsp_display_lock(100)) {
                    ui_set_schedule_countdown(rem_s);
                    bsp_display_unlock();
                }
            }
        }

        if (!s_is_sleeping) {
            if (bsp_display_lock(50)) {
                ui_update_clock();
                bsp_display_unlock();
            }
        }

        // podczas mycia odpytujemy co 8s, w gotowosci co 5s
        if (!s_is_sleeping && wifi_manager_is_connected()) {
            uint32_t poll_ms = (s_last_status.cycle_state == MIDEA_STATE_WORK) ? 8000 : 5000;
            TickType_t now = xTaskGetTickCount();
            if (last_poll_tick == 0 || (now - last_poll_tick) >= pdMS_TO_TICKS(poll_ms)) {
                midea_status_t st;
                midea_query_status(&st);
                s_last_status = st;
                int rssi = wifi_manager_get_rssi();
                if (bsp_display_lock(200)) {
                    ui_update_status(&s_last_status, true, rssi);
                    bsp_display_unlock();
                }
                update_power_hold_flags();
                last_poll_tick = now;
                weather_poll_if_due();
            }
        }
    }
}

void app_main(void)
{
    ota_log_init();
    ESP_LOGI(TAG, "=== Zmywarka ESP v2 (JC3248W535EN) ===");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    init_nvs();
    esp_ota_mark_app_valid_cancel_rollback();
    midea_api_init();
    weather_init(on_weather_update);

    s_cmd_queue = xQueueCreate(8, sizeof(app_cmd_t));

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = APP_DISPLAY_ROTATION,
    };
    if (bsp_display_start_with_config(&cfg) == NULL) {
        ESP_LOGE(TAG, "Display initialization failed!");
        return;
    }
    bsp_display_backlight_on();

    if (bsp_display_lock(0)) {
        ui_init(on_ui_action);
        weather_data_t cached_w = {0};
        weather_get_cached(&cached_w);
        if (cached_w.valid) {
            ui_set_weather(&cached_w);
        }
        char detail[48] = "Proszę chwilkę poczekać";
#ifdef WIFI_SSID
        snprintf(detail, sizeof(detail), "Sieć: %s", WIFI_SSID);
#endif
        ui_show_loading("Łączenie z siecią Wi-Fi...", detail);
        bsp_display_unlock();
    }

    ota_server_set_progress_cb(on_ota_progress);
    screen_off_set_callback(on_screen_power_change);
    screen_off_init(INACTIVITY_SLEEP_MS);

    ESP_ERROR_CHECK(wifi_manager_init(on_wifi_event));
    wifi_manager_start();

    xTaskCreatePinnedToCore(worker_task, "szpont_worker", 8192, NULL, 4, NULL, 0);
}
