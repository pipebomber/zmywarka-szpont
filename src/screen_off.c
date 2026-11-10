#include <time.h>
#include "screen_off.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "nvs.h"
#include "config.h"
#include "esp_bsp.h"
#include "display.h"

static const char *TAG = "SCREEN_OFF";
static esp_timer_handle_t s_screen_off_timer = NULL;
static int64_t s_last_activity_ts_us = 0;
static uint32_t s_timeout_ms = 45000;
static volatile bool s_screen_off = false;
static volatile bool s_keep_awake = false;
static volatile bool s_manual_off = false;
static volatile bool s_sleep_prerender = false;
static volatile uint8_t s_pending_wake_bri = 0;
static int64_t s_wake_start_us = 0;
static volatile bool s_finish_active = false;
static int64_t s_finish_hold_until_us = 0;
static uint8_t s_bri_mode = SCREEN_BRI_AUTO;
static bool s_was_night = false;
static uint8_t s_cur_brightness = 100;
static screen_power_cb_t s_power_cb = NULL;

static esp_pm_lock_handle_t s_pm_cpu_lock = NULL;
static esp_pm_lock_handle_t s_pm_apb_lock = NULL;
static bool s_pm_locked = false;

static void lock_full_speed(bool enable)
{
    if (enable && !s_pm_locked) {
        if (s_pm_apb_lock) esp_pm_lock_acquire(s_pm_apb_lock);
        if (s_pm_cpu_lock) esp_pm_lock_acquire(s_pm_cpu_lock);
        s_pm_locked = true;
    } else if (!enable && s_pm_locked) {
        if (s_pm_cpu_lock) esp_pm_lock_release(s_pm_cpu_lock);
        if (s_pm_apb_lock) esp_pm_lock_release(s_pm_apb_lock);
        s_pm_locked = false;
    }
}

static void get_time_schedule(bool *out_time_valid, bool *out_night, uint8_t *out_brightness)
{
    time_t now = 0;
    struct tm t = {0};
    time(&now);
    localtime_r(&now, &t);

    bool valid = (t.tm_year >= (2024 - 1900));
    bool night = false;
    uint8_t bri = 100;

    if (valid && (t.tm_hour >= 22 || t.tm_hour < 6)) {
        night = true;
    }

    if (s_bri_mode == SCREEN_BRI_100) {
        bri = 100;
    } else if (s_bri_mode == SCREEN_BRI_50) {
        bri = 50;
    } else if (valid) {
        if (night) {
            bri = 35; // Night blackout brief peek
        } else if (t.tm_hour >= 18) {
            bri = 60; // Evening dim (18:00 - 21:59)
        } else {
            bri = 100; // Daytime crisp (06:00 - 17:59)
        }
    }

    if (out_time_valid) *out_time_valid = valid;
    if (out_night) *out_night = night;
    if (out_brightness) *out_brightness = bri;
}

static void enter_sleep_mode(void)
{
    bsp_display_backlight_off();
    s_pending_wake_bri = 0;
    s_screen_off = true;
    s_cur_brightness = 0;
    if (s_power_cb) s_power_cb(true);
    // zbijamy CPU do 80 MHz dopiero po zgaszeniu PWM podswietlenia
    lock_full_speed(false);
}

static void wake_display_mode(uint8_t bri)
{
    // najpierw pelne taktowanie pod szybki render; podswietlenie wlacza sie po flushu DMA
    lock_full_speed(true);
    s_sleep_prerender = false;
    s_screen_off = false;
    s_wake_start_us = esp_timer_get_time();
    s_pending_wake_bri = (bri > 0) ? bri : 100;
    s_cur_brightness = s_pending_wake_bri;
    if (s_power_cb) s_power_cb(false);
}

static void screen_off_timer_cb(void *arg)
{
    (void)arg;
    bool time_valid = false;
    bool night = false;
    uint8_t target_bri = 100;
    get_time_schedule(&time_valid, &night, &target_bri);

    // o 6:00 rano wybudzamy ekran raz przy przejsciu z nocy
    if (time_valid && s_was_night && !night) {
        s_was_night = false;
        s_manual_off = false;
        s_last_activity_ts_us = esp_timer_get_time();
        if (s_screen_off) {
            ESP_LOGI(TAG, "06:00 AM morning wakeup (%u%%)", target_bri);
            wake_display_mode(target_bri);
        }
        return;
    }
    if (time_valid) {
        s_was_night = night;
    }

    int64_t now = esp_timer_get_time();

    // zapasowy timeout gdy wybudzenie przyszlo bez nowego flushu
    if (!s_screen_off && s_pending_wake_bri > 0 && (now - s_wake_start_us) > 350000LL) {
        screen_off_complete_wake_backlight();
    }

    int64_t elapsed_ms = (s_last_activity_ts_us > 0) ? ((now - s_last_activity_ts_us) / 1000) : 0;

    // w nocy (22:00 - 06:00) podglad tylko 15s, nawet podczas mycia
    uint32_t effective_timeout = night ? 15000 : s_timeout_ms;

    // po zakonczeniu mycia zostawiamy przygaszony ekran (28%) przez 10 min
    bool in_finish_dim = (!night && !s_keep_awake && !s_manual_off &&
                          s_finish_hold_until_us > now && elapsed_ms >= (int64_t)effective_timeout);
    if (s_finish_hold_until_us > 0 && now >= s_finish_hold_until_us) {
        s_finish_hold_until_us = 0;
    }

    uint8_t desired_bri = in_finish_dim ? 28 : target_bri;

    if (!s_screen_off && s_pending_wake_bri == 0 && desired_bri != s_cur_brightness) {
        ESP_LOGI(TAG, "Brightness -> %u%%", desired_bri);
        s_cur_brightness = desired_bri;
        bsp_display_brightness_set(desired_bri);
    }

    // w dzien podczas mycia ekran nie gasnie
    if (!night && s_keep_awake && !s_manual_off) {
        if (s_screen_off) {
            ESP_LOGI(TAG, "Wash active in daytime -> waking screen (%u%%)", target_bri);
            wake_display_mode(target_bri);
        }
        return;
    }

    if (in_finish_dim) {
        return;
    }

    if (!s_screen_off && elapsed_ms >= (int64_t)effective_timeout) {
        ESP_LOGI(TAG, "%s timeout (%lld ms) -> sleeping screen & throttling CPU",
                 night ? "Night" : "Inactivity", elapsed_ms);
        enter_sleep_mode();
    }
}

esp_err_t screen_off_init(uint32_t timeout_ms)
{
    if (timeout_ms == 0) return ESP_ERR_INVALID_ARG;

    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    esp_err_t pm_err = esp_pm_configure(&pm_cfg);
    if (pm_err == ESP_OK) {
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "disp_cpu", &s_pm_cpu_lock);
        esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "disp_apb", &s_pm_apb_lock);
        lock_full_speed(true);
        ESP_LOGI(TAG, "PM DFS enabled (240 MHz active, 80 MHz when screen off)");
    } else {
        ESP_LOGW(TAG, "esp_pm_configure skipped (%s)", esp_err_to_name(pm_err));
    }

    s_timeout_ms = timeout_ms;
    s_screen_off = false;
    s_manual_off = false;

    nvs_handle_t nvs;
    uint8_t saved_bri = SCREEN_BRI_AUTO;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "bri_mode", &saved_bri);
        nvs_close(nvs);
    }
    if (saved_bri > SCREEN_BRI_50) saved_bri = SCREEN_BRI_AUTO;
    s_bri_mode = saved_bri;

    uint8_t init_bri = (s_bri_mode == SCREEN_BRI_50) ? 50 : 100;
    s_cur_brightness = init_bri;
    bsp_display_brightness_set(init_bri);
    s_last_activity_ts_us = esp_timer_get_time();

    if (s_screen_off_timer) {
        esp_timer_stop(s_screen_off_timer);
        esp_timer_delete(s_screen_off_timer);
        s_screen_off_timer = NULL;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = screen_off_timer_cb,
        .name = "screen_off_timer"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_screen_off_timer);
    if (err != ESP_OK) return err;

    return esp_timer_start_periodic(s_screen_off_timer, 1000 * 1000);
}

void screen_off_deinit(void)
{
    if (s_screen_off_timer) {
        esp_timer_stop(s_screen_off_timer);
        esp_timer_delete(s_screen_off_timer);
        s_screen_off_timer = NULL;
    }
    lock_full_speed(false);
    s_screen_off = false;
}

void screen_off_set_callback(screen_power_cb_t cb)
{
    s_power_cb = cb;
}

void screen_off_set_keep_awake(bool keep_awake)
{
    s_keep_awake = keep_awake;
    if (!keep_awake) {
        s_manual_off = false;
    }
}

void screen_off_set_finish_hold(bool finish_active)
{
    if (finish_active && !s_finish_active) {
        s_finish_active = true;
        s_finish_hold_until_us = esp_timer_get_time() + (10LL * 60LL * 1000000LL);
        ESP_LOGI(TAG, "Finish detected -> 10 min dimmed window enabled");
    } else if (!finish_active) {
        s_finish_active = false;
        s_finish_hold_until_us = 0;
    }
}

uint8_t screen_off_get_bri_mode(void)
{
    return s_bri_mode;
}

uint8_t screen_off_cycle_bri_mode(void)
{
    s_bri_mode = (uint8_t)((s_bri_mode + 1) % 3);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "bri_mode", s_bri_mode);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    if (!s_screen_off) {
        uint8_t bri = 100;
        get_time_schedule(NULL, NULL, &bri);
        s_cur_brightness = bri;
        bsp_display_brightness_set(bri);
    }
    ESP_LOGI(TAG, "Brightness mode -> %u", (unsigned)s_bri_mode);
    return s_bri_mode;
}

void screen_off_force_off(void)
{
    s_manual_off = true;
    if (!s_screen_off) {
        ESP_LOGI(TAG, "Manual sleep requested -> throttling CPU");
        enter_sleep_mode();
    }
}

void screen_off_activity(void)
{
    s_last_activity_ts_us = esp_timer_get_time();
    s_manual_off = false;
    if (s_screen_off) {
        bool night = false;
        uint8_t bri = 100;
        get_time_schedule(NULL, &night, &bri);
        ESP_LOGI(TAG, "Wake-up from touch (%u%% brightness)", bri);
        wake_display_mode(bri);
    }
}

bool screen_off_is_off(void)
{
    return s_screen_off;
}

bool screen_off_is_flush_paused(void)
{
    return s_screen_off && !s_sleep_prerender;
}

void screen_off_set_sleep_prerender(bool active)
{
    s_sleep_prerender = active;
}

bool screen_off_has_pending_wake_backlight(void)
{
    return (!s_screen_off && s_pending_wake_bri > 0);
}

void screen_off_complete_wake_backlight(void)
{
    uint8_t bri = s_pending_wake_bri;
    if (bri > 0 && !s_screen_off) {
        s_pending_wake_bri = 0;
        s_cur_brightness = bri;
        bsp_display_brightness_set(bri);
    }
}
