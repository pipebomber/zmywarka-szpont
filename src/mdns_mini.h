#pragma once

#include "esp_err.h"

/**
 * Start lightweight mDNS responder task answering "<hostname>.local" (default "zmywarka.local")
 * on UDP multicast 224.0.0.251:5353 using native LwIP sockets.
 */
esp_err_t mdns_mini_start(const char *hostname);

/**
 * Re-join multicast group (after Wi-Fi reconnect) and send a gratuitous A-record announcement.
 */
void mdns_mini_announce(void);
