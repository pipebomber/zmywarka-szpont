/* Szpont Washer ESP - konfiguracja sprzetowa (JC3248W535EN) */

#ifndef CONFIG_H
#define CONFIG_H

#if defined(__has_include)
  #if __has_include("secrets.h")
    #include "secrets.h"
  #endif
#endif

// Ekran QSPI
#define DISPLAY_WIDTH           480
#define DISPLAY_HEIGHT          320
#define DISPLAY_ROTATION        0

#define DISPLAY_QSPI_CS         45
#define DISPLAY_QSPI_SCK        47
#define DISPLAY_QSPI_D0         21
#define DISPLAY_QSPI_D1         48
#define DISPLAY_QSPI_D2         40
#define DISPLAY_QSPI_D3         39
#define DISPLAY_RST             -1    // polaczone z EN
#define DISPLAY_BL              1     // PWM podswietlenia

// Dotyk I2C (CST816S / AXS15231B)
#define TOUCH_I2C_SDA           4
#define TOUCH_I2C_SCL           8
#define TOUCH_I2C_RST           -1
#define TOUCH_I2C_INT           3     // RTC GPIO 3 budzi z deep sleep

// Zmywarka Midea V3
#ifndef DISHWASHER_IP
  #define DISHWASHER_IP         "192.168.18.98"
#endif
#define DISHWASHER_PORT         6444
#define DISHWASHER_DEVICE_ID    153931629819533ULL

// Czasy odpytywania i wygaszania
#define STATUS_POLL_INTERVAL_MS 4000
#define INACTIVITY_SLEEP_MS     45000
#define WAKE_TOUCH_GUARD_MS     350     // blokuje przypadkowy klik przy wybudzeniu

// Pogoda Open-Meteo (Wroclaw, Polska)
#define WEATHER_LAT_STR         "51.1029"
#define WEATHER_LON_STR         "17.0301"
#define WEATHER_POLL_INTERVAL_S 3600

#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_PERIOD_MS     5
#define LVGL_BUFFER_LINES       40

// Zapasowy AP do konfiguracji
#define WIFI_PROV_SSID          "Szpont-Setup"
#define WIFI_PROV_PASS          "zmywarka123"
#define WIFI_MAX_RETRY          6

// Bazowe kolory ui_styles.h
#define COLOR_BG_BLACK          0x000000
#define COLOR_SURFACE           0x111827
#define COLOR_SURFACE_ALT       0x1F2937
#define COLOR_BORDER            0x374151
#define COLOR_TEXT_MAIN         0xF9FAFB
#define COLOR_TEXT_MUTED        0x9CA3AF

#define COLOR_GREEN             0x10B981
#define COLOR_GREEN_DARK        0x064E3B
#define COLOR_AMBER             0xF59E0B
#define COLOR_AMBER_DARK        0x451A03
#define COLOR_BLUE              0x38BDF8
#define COLOR_RED               0xEF4444
#define COLOR_RED_DARK          0x450A0A

// Klucze NVS
#define NVS_NAMESPACE           "szpont_esp"
#define NVS_LEGACY_NAMESPACE    "eve_dash"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_DISHWASHER_IP   "dish_ip"

#endif // CONFIG_H
