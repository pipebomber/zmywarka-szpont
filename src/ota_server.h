#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef void (*ota_progress_cb_t)(int pct, bool finished, bool ok);

/**
 * Install in-memory log ring buffer hook (safe to call once at startup).
 */
void ota_log_init(void);

/**
 * Register callback invoked during OTA upload progress and completion.
 */
void ota_server_set_progress_cb(ota_progress_cb_t cb);

/**
 * Start local HTTP OTA update & log server on port 80.
 * Serves upload + live logs page at GET /, accepts .bin stream at POST /update,
 * and exposes raw logs at GET /logs.
 */
esp_err_t ota_server_start(void);

/**
 * Stop local HTTP OTA server if running.
 */
void ota_server_stop(void);

/**
 * Return true if OTA server is currently running or flashing.
 */
bool ota_server_is_running(void);
