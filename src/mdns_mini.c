#include <ctype.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "mdns_mini.h"

static const char *TAG = "MDNS_MINI";
#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353

static int s_sock = -1;
static char s_host[32] = "zmywarka";
static bool s_started = false;
static volatile bool s_rejoin_needed = false;

static bool get_sta_ipv4(uint32_t *out_addr_net)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return false;
    esp_netif_ip_info_t info = {0};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    *out_addr_net = info.ip.addr;
    return true;
}

static size_t build_a_response(uint8_t *out, uint32_t ip_net)
{
    size_t hlen = strlen(s_host);
    size_t pos = 0;

    // naglowek DNS: odpowiedź autorytatywna, 1 rekord
    memset(out, 0, 12);
    out[2] = 0x84;
    out[7] = 0x01;
    pos = 12;

    out[pos++] = (uint8_t)hlen;
    memcpy(out + pos, s_host, hlen);
    pos += hlen;
    out[pos++] = 5;
    memcpy(out + pos, "local", 5);
    pos += 5;
    out[pos++] = 0;

    // rekord A (IN + cache flush, TTL 120s)
    out[pos++] = 0x00; out[pos++] = 0x01;
    out[pos++] = 0x80; out[pos++] = 0x01;
    out[pos++] = 0x00; out[pos++] = 0x00; out[pos++] = 0x00; out[pos++] = 120;
    out[pos++] = 0x00; out[pos++] = 0x04;
    memcpy(out + pos, &ip_net, 4);
    pos += 4;
    return pos;
}

static int parse_dns_name(const uint8_t *pkt, size_t len, size_t off, char *out, size_t max_out)
{
    size_t cur = off;
    size_t out_pos = 0;
    int end_off = -1;
    int jumps = 0;

    while (cur < len && jumps++ < 8) {
        uint8_t l = pkt[cur];
        if (l == 0) {
            if (end_off < 0) end_off = (int)(cur + 1);
            if (out_pos > 0 && out[out_pos - 1] == '.') out_pos--;
            out[out_pos] = '\0';
            return end_off;
        }
        if ((l & 0xC0) == 0xC0) {
            if (cur + 1 >= len) return -1;
            if (end_off < 0) end_off = (int)(cur + 2);
            cur = (size_t)(((l & 0x3F) << 8) | pkt[cur + 1]);
            continue;
        }
        cur++;
        if (cur + l > len) return -1;
        for (uint8_t i = 0; i < l && out_pos + 2 < max_out; i++) {
            out[out_pos++] = (char)tolower((unsigned char)pkt[cur + i]);
        }
        if (out_pos + 1 < max_out) out[out_pos++] = '.';
        cur += l;
    }
    return -1;
}

static void send_mdns_a(const struct sockaddr_in *unicast_dst)
{
    uint32_t ip_net = 0;
    if (s_sock < 0 || !get_sta_ipv4(&ip_net)) return;

    uint8_t resp[96];
    size_t rlen = build_a_response(resp, ip_net);

    struct sockaddr_in mcast = {0};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(MDNS_PORT);
    inet_pton(AF_INET, MDNS_ADDR, &mcast.sin_addr);
    sendto(s_sock, resp, rlen, 0, (struct sockaddr *)&mcast, sizeof(mcast));

    if (unicast_dst) {
        sendto(s_sock, resp, rlen, 0, (struct sockaddr *)unicast_dst, sizeof(*unicast_dst));
    }
}

static void join_multicast(void)
{
    if (s_sock < 0) return;
    struct ip_mreq mreq = {0};
    inet_pton(AF_INET, MDNS_ADDR, &mreq.imr_multiaddr);
    uint32_t ip_net = 0;
    mreq.imr_interface.s_addr = get_sta_ipv4(&ip_net) ? ip_net : htonl(INADDR_ANY);
    setsockopt(s_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

static void mdns_task(void *arg)
{
    (void)arg;
    char expected[48];
    snprintf(expected, sizeof(expected), "%s.local", s_host);

    uint8_t rx[512];
    while (1) {
        if (s_rejoin_needed) {
            s_rejoin_needed = false;
            join_multicast();
            send_mdns_a(NULL);
        }

        struct sockaddr_in src = {0};
        socklen_t slen = sizeof(src);
        ssize_t n = recvfrom(s_sock, rx, sizeof(rx), 0, (struct sockaddr *)&src, &slen);
        if (n < 12) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Standard DNS query only (QR == 0)
        if ((rx[2] & 0x80) != 0) continue;
        uint16_t qdcount = (uint16_t)((rx[4] << 8) | rx[5]);
        if (qdcount == 0 || qdcount > 16) continue;

        size_t off = 12;
        bool matched = false;
        bool want_unicast = false;
        for (uint16_t q = 0; q < qdcount && off < (size_t)n; q++) {
            char qname[128] = {0};
            int next_off = parse_dns_name(rx, (size_t)n, off, qname, sizeof(qname));
            if (next_off < 0 || next_off + 4 > (int)n) break;
            uint16_t qtype = (uint16_t)((rx[next_off] << 8) | rx[next_off + 1]);
            uint16_t qclass = (uint16_t)((rx[next_off + 2] << 8) | rx[next_off + 3]);
            off = (size_t)(next_off + 4);

            if ((qtype == 1 || qtype == 255) && strcmp(qname, expected) == 0) {
                matched = true;
                if ((qclass & 0x8000) != 0 || ntohs(src.sin_port) != MDNS_PORT) {
                    want_unicast = true;
                }
            }
        }

        if (matched) {
            send_mdns_a(want_unicast ? &src : NULL);
        }
    }
}

esp_err_t mdns_mini_start(const char *hostname)
{
    if (s_started) return ESP_OK;
    if (hostname && hostname[0]) {
        size_t i = 0;
        for (; i < sizeof(s_host) - 1 && hostname[i]; i++) {
            s_host[i] = (char)tolower((unsigned char)hostname[i]);
        }
        s_host[i] = '\0';
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGW(TAG, "mDNS socket create failed");
        return ESP_FAIL;
    }

    int reuse = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MDNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGW(TAG, "mDNS bind :5353 failed");
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    uint8_t ttl = 255;
    setsockopt(s_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    join_multicast();

    s_started = true;
    xTaskCreate(mdns_task, "mdns_mini", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "mDNS responder active: http://%s.local/", s_host);
    return ESP_OK;
}

void mdns_mini_announce(void)
{
    if (s_started) {
        s_rejoin_needed = true;
    }
}
