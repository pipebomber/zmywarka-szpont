/**
 * Midea Dishwasher V3 Local LAN Protocol Client
 * Standalone ESP32-S3 implementation (no external server required)
 */

#ifndef MIDEA_API_H
#define MIDEA_API_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MIDEA_PROG_1H = 0,       // 1 Godzina (0x09, 60 min)
    MIDEA_PROG_30MIN = 1,    // Szybki 30 min (0x07, 30 min)
    MIDEA_PROG_ECO = 2,      // Eco 50C (0x04)
    MIDEA_PROG_INTENSIVE = 3 // Intensywny / Mocne (0x02)
} midea_program_t;

typedef enum {
    MIDEA_STATE_UNKNOWN = 0,
    MIDEA_STATE_OFF,
    MIDEA_STATE_IDLE,
    MIDEA_STATE_WORK,
    MIDEA_STATE_FINISH,
    MIDEA_STATE_ERROR
} midea_cycle_state_t;

typedef struct {
    bool is_online;
    bool machine_on;
    midea_cycle_state_t cycle_state;
    uint8_t mode_code;         // Raw mode byte (e.g. 0x09 ONE_HOUR, 0x07 RAPID)
    uint8_t wash_stage;        // 0=IDLE, 1=PRE_WASH, 2=MAIN_WASH, 3=RINSE, 4=DRY, 5=FINISH
    char mode_name_pl[32];     // Human-readable Polish program name
    char stage_name_pl[32];    // Human-readable Polish wash stage
    uint16_t left_time_min;    // Minutes remaining
    uint8_t temperature_c;     // Water temperature (0 if inactive)
    bool door_closed;
    bool rinse_aid_lack;
    bool salt_lack;
    uint8_t error_code;        // 0 = NONE, 1 = E1, 3 = E3, 4 = E4, 6/7 = E6/E7
    uint32_t latency_ms;
    char host_ip[16];
} midea_status_t;

/**
 * Initialize Midea client (loads saved IP from NVS, creates mutex)
 */
void midea_api_init(void);

/**
 * Get / Set current dishwasher IP address (persisted in NVS)
 */
void midea_api_get_ip(char *out_ip, size_t max_len);
void midea_api_set_ip(const char *ip);

/**
 * Query live dishwasher status over LAN (port 6444)
 */
esp_err_t midea_query_status(midea_status_t *out_status);

/**
 * Turn dishwasher on/off
 */
esp_err_t midea_power(bool on);

/**
 * Return raw Midea protocol mode byte for a program enum.
 */
uint8_t midea_program_mode_code(midea_program_t prog);

/**
 * Start selected dishwasher program (optional Extra Dry on 1H).
 * Automatically turns power on first if currently_off is true.
 */
esp_err_t midea_start_program(midea_program_t prog, bool currently_off, bool extra_dry);

/**
 * Cancel / stop running wash cycle
 */
esp_err_t midea_cancel(void);

/**
 * UDP LAN broadcast discovery on port 6445/20086
 * Updates saved IP if found.
 */
esp_err_t midea_discover_ip(char *found_ip, size_t max_len);

#endif // MIDEA_API_H
