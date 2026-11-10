/* Klient lokalnego protokolu Midea V3 (port 6444) */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"

#include "midea_api.h"
#include "config.h"

static const char *TAG = "MIDEA_API";

// V3 Provisioned Device Token & Key (64B Token, 32B Key)
static const uint8_t MIDEA_TOKEN[64] = {
    0x25, 0xe0, 0xdb, 0x22, 0x67, 0x07, 0xe4, 0x65,
    0x3d, 0x0e, 0x90, 0x94, 0x39, 0x7d, 0x42, 0xbb,
    0x39, 0x40, 0x92, 0x2b, 0x36, 0xde, 0x55, 0x3c,
    0xa6, 0x30, 0xd8, 0x47, 0xee, 0x0e, 0x85, 0xc8,
    0x14, 0x38, 0x24, 0x9c, 0x95, 0x65, 0x0c, 0x09,
    0x62, 0x52, 0xf9, 0x38, 0x0f, 0x9b, 0xc3, 0xc9,
    0xf2, 0x6e, 0xf6, 0x13, 0x0a, 0x02, 0x3e, 0x88,
    0xe9, 0x8d, 0xdb, 0x4b, 0x33, 0xb1, 0xf7, 0xc5
};

static const uint8_t MIDEA_KEY[32] = {
    0x9e, 0xc3, 0x85, 0x4b, 0x3b, 0x41, 0x46, 0xea,
    0x90, 0x62, 0xfe, 0x07, 0x77, 0x4d, 0x61, 0xbb,
    0xa6, 0x73, 0xed, 0xba, 0x81, 0x3a, 0x44, 0xa6,
    0xb9, 0x22, 0xb1, 0x8c, 0x37, 0xae, 0xaf, 0x19
};

static const char *V2_SIGN_KEY = "xhdiwjnchekd4d512chdjx5d8e4c394D2D7S";
static uint8_t s_v2_enc_key[16];

static const uint8_t MIDEA_DISCOVERY_PKT[72] = {
    0x5a, 0x5a, 0x01, 0x11, 0x48, 0x00, 0x92, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0x75, 0xbd, 0x6b, 0x3e, 0x4f, 0x8b, 0x76,
    0x2e, 0x84, 0x9c, 0x6e, 0x57, 0x8d, 0x65, 0x90,
    0x03, 0x6e, 0x9d, 0x43, 0x42, 0xa5, 0x0f, 0x1f,
    0x56, 0x9e, 0xb8, 0xec, 0x91, 0x8e, 0x92, 0xe5
};

static SemaphoreHandle_t s_device_mutex = NULL;
static char s_host_ip[16] = DISHWASHER_IP;
static uint16_t s_packet_counter = 0;

static void next_packet_id(uint8_t out[2])
{
    s_packet_counter = (s_packet_counter + 1) & 0x0FFF;
    out[0] = (uint8_t)((s_packet_counter >> 8) & 0xFF);
    out[1] = (uint8_t)(s_packet_counter & 0xFF);
}

static uint8_t make_checksum(const uint8_t *buf, size_t start, size_t end_inclusive)
{
    int sum = 0;
    for (size_t i = start; i <= end_inclusive; i++) {
        sum += buf[i];
    }
    return (uint8_t)((-sum) & 0xFF);
}

static int aes_cbc_crypt(bool encrypt, const uint8_t *key, size_t key_bits,
                         const uint8_t *in, size_t len, uint8_t *out)
{
    if (len == 0 || (len % 16) != 0) return -1;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = encrypt
        ? mbedtls_aes_setkey_enc(&aes, key, (unsigned int)key_bits)
        : mbedtls_aes_setkey_dec(&aes, key, (unsigned int)key_bits);
    if (ret == 0) {
        uint8_t iv[16] = {0};
        ret = mbedtls_aes_crypt_cbc(&aes,
                                    encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                    len, iv, in, out);
    }
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_ecb_crypt_blocks(bool encrypt, const uint8_t *key, size_t key_bits,
                                const uint8_t *in, size_t len, uint8_t *out)
{
    if (len == 0 || (len % 16) != 0) return -1;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = encrypt
        ? mbedtls_aes_setkey_enc(&aes, key, (unsigned int)key_bits)
        : mbedtls_aes_setkey_dec(&aes, key, (unsigned int)key_bits);
    if (ret == 0) {
        for (size_t off = 0; off < len; off += 16) {
            ret = mbedtls_aes_crypt_ecb(&aes,
                                        encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                        in + off, out + off);
            if (ret != 0) break;
        }
    }
    mbedtls_aes_free(&aes);
    return ret;
}

static size_t assemble_frame(const uint8_t *body, size_t body_len, uint8_t msg_type, uint8_t *out_frame)
{
    size_t total = 10 + body_len + 1;
    memset(out_frame, 0, total);
    out_frame[0] = 0xAA;
    out_frame[1] = (uint8_t)(total - 1);
    out_frame[2] = 0xE1; // Dishwasher device type
    out_frame[9] = msg_type;
    memcpy(out_frame + 10, body, body_len);
    out_frame[total - 1] = make_checksum(out_frame, 1, total - 2);
    return total;
}

static size_t v2_pack(uint64_t device_id, const uint8_t *frame, size_t frame_len, uint8_t *out_v2, size_t max_out)
{
    uint8_t padded[256];
    uint8_t encrypted[256];
    uint8_t hash_buf[384];

    uint8_t pad = (uint8_t)(16 - (frame_len % 16));
    size_t padded_len = frame_len + pad;
    if (padded_len > sizeof(padded) || (40 + padded_len + 16) > max_out) return 0;

    memcpy(padded, frame, frame_len);
    memset(padded + frame_len, pad, pad);

    if (aes_ecb_crypt_blocks(true, s_v2_enc_key, 128, padded, padded_len, encrypted) != 0) {
        return 0;
    }

    size_t total_len = 40 + padded_len + 16;
    uint8_t *hdr = out_v2;
    memset(hdr, 0, 40);
    hdr[0] = 0x5A;
    hdr[1] = 0x5A;
    hdr[2] = 0x01;
    hdr[3] = 0x11;
    hdr[4] = (uint8_t)(total_len & 0xFF);
    hdr[5] = (uint8_t)((total_len >> 8) & 0xFF);
    hdr[6] = 0x20;
    hdr[7] = 0x00;

    // Timestamp bytes
    hdr[12] = 0;
    hdr[13] = 0;
    hdr[14] = 0;
    hdr[15] = 12;
    hdr[16] = 25;
    hdr[17] = 10;
    hdr[18] = 26;
    hdr[19] = 20;

    // Little-endian 64-bit device ID
    for (int i = 0; i < 8; i++) {
        hdr[20 + i] = (uint8_t)((device_id >> (i * 8)) & 0xFF);
    }

    memcpy(out_v2 + 40, encrypted, padded_len);

    size_t sign_key_len = strlen(V2_SIGN_KEY);
    memcpy(hash_buf, out_v2, 40 + padded_len);
    memcpy(hash_buf + 40 + padded_len, V2_SIGN_KEY, sign_key_len);
    mbedtls_md5(hash_buf, 40 + padded_len + sign_key_len, out_v2 + 40 + padded_len);

    return total_len;
}

static size_t v2_unpack(const uint8_t *pkt, size_t pkt_len, uint8_t *out_frame, size_t max_out)
{
    if (pkt_len < 56 || pkt[0] != 0x5A || pkt[1] != 0x5A) return 0;
    uint16_t declared_len = (uint16_t)(pkt[4] | (pkt[5] << 8));
    size_t effective_len = (pkt_len > declared_len && declared_len >= 56) ? declared_len : pkt_len;
    if (effective_len <= 56) return 0;

    size_t enc_len = effective_len - 56;
    if ((enc_len % 16) != 0 || enc_len > max_out) return 0;

    if (aes_ecb_crypt_blocks(false, s_v2_enc_key, 128, pkt + 40, enc_len, out_frame) != 0) {
        return 0;
    }

    uint8_t pad = out_frame[enc_len - 1];
    if (pad >= 1 && pad <= 16 && enc_len >= pad) {
        return enc_len - pad;
    }
    return enc_len;
}

static int tcp_connect_timeout(const char *ip, int port, int timeout_ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) return -1;

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(sock);
        return -1;
    }

    int res = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sock);
        return -1;
    }

    if (res != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval sel_tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000
        };
        res = select(sock + 1, NULL, &wfds, NULL, &sel_tv);
        if (res <= 0) {
            close(sock);
            return -1;
        }
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &len);
        if (so_err != 0) {
            close(sock);
            return -1;
        }
    }

    // Restore blocking mode (subject to SO_RCVTIMEO / SO_SNDTIMEO)
    fcntl(sock, F_SETFL, flags);
    return sock;
}

static bool read_exact(int sock, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sock, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

static bool write_exact(int sock, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static bool v3_handshake(int sock, uint8_t out_tcp_key[32])
{
    uint8_t hs_req[72];
    const uint8_t hs_hdr[6] = {0x83, 0x70, 0x00, 0x40, 0x20, 0x00};
    memcpy(hs_req, hs_hdr, 6);
    next_packet_id(hs_req + 6);
    memcpy(hs_req + 8, MIDEA_TOKEN, 64);

    if (!write_exact(sock, hs_req, sizeof(hs_req))) {
        ESP_LOGW(TAG, "Handshake send failed");
        return false;
    }

    uint8_t resp_hdr[6];
    if (!read_exact(sock, resp_hdr, 6) || resp_hdr[0] != 0x83 || resp_hdr[1] != 0x70) {
        ESP_LOGW(TAG, "Bad handshake response header");
        return false;
    }

    uint16_t resp_size = (uint16_t)((resp_hdr[2] << 8) | resp_hdr[3]);
    if (resp_size < 64 || (2 + resp_size) > 256) {
        ESP_LOGW(TAG, "Unexpected handshake size: %u", resp_size);
        return false;
    }

    uint8_t resp_body[256];
    if (!read_exact(sock, resp_body, 2 + resp_size)) {
        ESP_LOGW(TAG, "Handshake body read failed");
        return false;
    }

    const uint8_t *payload = resp_body + 2;      // 32B
    const uint8_t *sign = resp_body + 2 + 32;    // 32B

    uint8_t plain[32];
    if (aes_cbc_crypt(false, MIDEA_KEY, 256, payload, 32, plain) != 0) {
        return false;
    }

    uint8_t expected_sign[32];
    mbedtls_sha256(plain, 32, expected_sign, 0);
    if (memcmp(expected_sign, sign, 32) != 0) {
        ESP_LOGW(TAG, "Handshake SHA256 signature mismatch");
        return false;
    }

    for (int i = 0; i < 32; i++) {
        out_tcp_key[i] = (uint8_t)(plain[i] ^ MIDEA_KEY[i]);
    }
    return true;
}

static size_t v3_send_frame(int sock, const uint8_t tcp_key[32],
                            const uint8_t *app_frame, size_t app_len,
                            uint8_t *out_reply, size_t max_reply)
{
    uint8_t v2_pkt[320];
    size_t v2_len = v2_pack(DISHWASHER_DEVICE_ID, app_frame, app_len, v2_pkt, sizeof(v2_pkt));
    if (v2_len == 0) return 0;

    size_t rem = (v2_len + 2) % 16;
    uint8_t pad = (uint8_t)((rem != 0) ? (16 - rem) : 0);
    size_t size = v2_len + pad + 32;
    uint8_t byte5 = (uint8_t)(((pad & 0x0F) << 4) | 0x06);

    uint8_t hdr[6] = {
        0x83, 0x70,
        (uint8_t)((size >> 8) & 0xFF),
        (uint8_t)(size & 0xFF),
        0x20, byte5
    };

    size_t plain_len = 2 + v2_len + pad; // multiple of 16
    uint8_t plain_payload[352];
    uint8_t cipher_payload[352];
    uint8_t to_sign[360];
    uint8_t wire[400];

    if (plain_len > sizeof(plain_payload) || (6 + plain_len + 32) > sizeof(wire)) return 0;

    next_packet_id(plain_payload);
    memcpy(plain_payload + 2, v2_pkt, v2_len);
    if (pad > 0) {
        esp_fill_random(plain_payload + 2 + v2_len, pad);
    }

    if (aes_cbc_crypt(true, tcp_key, 256, plain_payload, plain_len, cipher_payload) != 0) {
        return 0;
    }

    memcpy(to_sign, hdr, 6);
    memcpy(to_sign + 6, plain_payload, plain_len);
    uint8_t sign[32];
    mbedtls_sha256(to_sign, 6 + plain_len, sign, 0);

    memcpy(wire, hdr, 6);
    memcpy(wire + 6, cipher_payload, plain_len);
    memcpy(wire + 6 + plain_len, sign, 32);

    if (!write_exact(sock, wire, 6 + plain_len + 32)) {
        return 0;
    }

    uint8_t rx_head[6];
    uint8_t rx_body[512];
    uint8_t dec_payload[512];

    for (int tries = 0; tries < 5; tries++) {
        if (!read_exact(sock, rx_head, 6)) return 0;
        if (rx_head[0] != 0x83 || rx_head[1] != 0x70) return 0;

        uint16_t rx_size = (uint16_t)((rx_head[2] << 8) | rx_head[3]);
        uint8_t rx_type = rx_head[5] & 0x0F;
        uint8_t rx_pad = (rx_head[5] >> 4) & 0x0F;

        if ((2 + rx_size) > sizeof(rx_body)) return 0;
        if (!read_exact(sock, rx_body, 2 + rx_size)) return 0;

        if (rx_type == 0x03) {
            size_t body_total = 2 + rx_size;
            if (body_total < 48) return 0; // need at least 16B enc + 32B sign
            size_t enc_len = body_total - 32;
            if ((enc_len % 16) != 0) return 0;

            if (aes_cbc_crypt(false, tcp_key, 256, rx_body, enc_len, dec_payload) != 0) {
                return 0;
            }
            if (enc_len <= (size_t)(2 + rx_pad)) return 0;
            size_t v2_bytes_len = enc_len - 2 - rx_pad;
            return v2_unpack(dec_payload + 2, v2_bytes_len, out_reply, max_reply);
        }
    }
    return 0;
}

static esp_err_t execute_frame(const uint8_t *app_frame, size_t app_len,
                               uint8_t *out_reply, size_t max_reply, size_t *out_len)
{
    if (!s_device_mutex) midea_api_init();
    xSemaphoreTake(s_device_mutex, portMAX_DELAY);

    esp_err_t result = ESP_FAIL;
    for (int attempt = 0; attempt < 2; attempt++) {
        int sock = tcp_connect_timeout(s_host_ip, DISHWASHER_PORT, 1500);
        if (sock >= 0) {
            uint8_t tcp_key[32];
            if (v3_handshake(sock, tcp_key)) {
                size_t rlen = v3_send_frame(sock, tcp_key, app_frame, app_len, out_reply, max_reply);
                if (rlen > 0) {
                    if (out_len) *out_len = rlen;
                    result = ESP_OK;
                }
            }
            close(sock);
        }
        vTaskDelay(pdMS_TO_TICKS(result == ESP_OK ? 180 : 300));
        if (result == ESP_OK) break;
    }

    xSemaphoreGive(s_device_mutex);
    return result;
}

static void fill_mode_name(uint8_t mode_byte, char *out, size_t max_len)
{
    const char *name = "Inny";
    switch (mode_byte) {
        case 0x01: name = "Auto"; break;
        case 0x02: name = "Mocne (Garnek)"; break;
        case 0x03: name = "Normalne"; break;
        case 0x04: name = "Eco 50°C"; break;
        case 0x05: name = "Szkło 45°C"; break;
        case 0x06: name = "90 minut"; break;
        case 0x07: name = "Szybki 30 min"; break;
        case 0x08: name = "Namaczanie"; break;
        case 0x09: name = "1 Godzina"; break;
        case 0x0F: name = "Higiena 72°C"; break;
        case 0x10: name = "Czyszczenie"; break;
        default:   name = "Brak programu"; break;
    }
    snprintf(out, max_len, "%s", name);
}

static void fill_stage_name(uint8_t stage_byte, char *out, size_t max_len)
{
    const char *stage = "Czuwanie";
    switch (stage_byte) {
        case 1: stage = "Płukanie wstępne"; break;
        case 2: stage = "Mycie gorące"; break;
        case 3: stage = "Płukanie czyste"; break;
        case 4: stage = "Suszenie naczyń"; break;
        case 5: stage = "Zakończono"; break;
        default: stage = "Czuwanie"; break;
    }
    snprintf(out, max_len, "%s", stage);
}

static void parse_status(const uint8_t *raw, size_t len, midea_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->is_online = true;
    snprintf(out->host_ip, sizeof(out->host_ip), "%s", s_host_ip);

    if (len < 12) {
        out->machine_on = false;
        out->cycle_state = MIDEA_STATE_OFF;
        out->door_closed = true;
        return;
    }

    const uint8_t *body = raw + 10;
    size_t body_len = len - 11;

    uint8_t cs = (body_len > 1) ? body[1] : 0;
    out->machine_on = (cs != 0x00);

    uint8_t stage_byte = (body_len > 9) ? body[9] : 0;
    out->wash_stage = stage_byte;
    fill_stage_name(stage_byte, out->stage_name_pl, sizeof(out->stage_name_pl));

    switch (cs) {
        case 0x00: out->cycle_state = MIDEA_STATE_OFF; break;
        case 0x01:
            out->cycle_state = (stage_byte == 5) ? MIDEA_STATE_FINISH : MIDEA_STATE_IDLE;
            break;
        case 0x03: out->cycle_state = MIDEA_STATE_WORK; break;
        case 0x04: out->cycle_state = MIDEA_STATE_ERROR; break;
        default:   out->cycle_state = MIDEA_STATE_IDLE; break;
    }

    out->mode_code = (body_len > 2) ? body[2] : 0;
    fill_mode_name(out->mode_code, out->mode_name_pl, sizeof(out->mode_name_pl));

    uint8_t flags5 = (body_len > 5) ? body[5] : 0x01;
    out->door_closed = (flags5 & 0x01) != 0;
    out->rinse_aid_lack = (flags5 & 0x02) != 0;
    out->salt_lack = (flags5 & 0x04) != 0;

    out->error_code = (body_len > 10) ? body[10] : 0;
    out->temperature_c = (body_len > 11) ? body[11] : 0;

    if (out->cycle_state == MIDEA_STATE_WORK) {
        uint8_t low = (body_len > 6) ? body[6] : 0;
        uint8_t high = (body_len > 32) ? body[32] : 0;
        out->left_time_min = (uint16_t)((high << 8) | low);
    }
}

void midea_api_init(void)
{
    if (!s_device_mutex) {
        s_device_mutex = xSemaphoreCreateMutex();
    }
    mbedtls_md5((const uint8_t *)V2_SIGN_KEY, strlen(V2_SIGN_KEY), s_v2_enc_key);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        char buf[16] = {0};
        size_t len = sizeof(buf);
        if (nvs_get_str(nvs, NVS_KEY_DISHWASHER_IP, buf, &len) == ESP_OK && strlen(buf) >= 7) {
            snprintf(s_host_ip, sizeof(s_host_ip), "%s", buf);
            ESP_LOGI(TAG, "Loaded saved dishwasher IP: %s", s_host_ip);
        }
        nvs_close(nvs);
    }
}

void midea_api_get_ip(char *out_ip, size_t max_len)
{
    snprintf(out_ip, max_len, "%s", s_host_ip);
}

void midea_api_set_ip(const char *ip)
{
    if (!ip || strlen(ip) < 7) return;
    snprintf(s_host_ip, sizeof(s_host_ip), "%s", ip);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, NVS_KEY_DISHWASHER_IP, s_host_ip);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

esp_err_t midea_query_status(midea_status_t *out_status)
{
    if (!out_status) return ESP_ERR_INVALID_ARG;
    uint8_t query_body[1] = {0x00};
    uint8_t frame[64];
    uint8_t reply[256];
    size_t frame_len = assemble_frame(query_body, 1, 0x03, frame);
    size_t reply_len = 0;

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = execute_frame(frame, frame_len, reply, sizeof(reply), &reply_len);
    int64_t t1 = esp_timer_get_time();

    if (err != ESP_OK) {
        memset(out_status, 0, sizeof(*out_status));
        out_status->is_online = false;
        snprintf(out_status->host_ip, sizeof(out_status->host_ip), "%s", s_host_ip);
        return err;
    }

    parse_status(reply, reply_len, out_status);
    out_status->latency_ms = (uint32_t)((t1 - t0) / 1000);
    return ESP_OK;
}

esp_err_t midea_power(bool on)
{
    uint8_t body[38] = {0};
    body[0] = 0x08;
    body[1] = on ? 0x01 : 0x00;

    uint8_t frame[64];
    uint8_t reply[256];
    size_t frame_len = assemble_frame(body, sizeof(body), 0x02, frame);
    return execute_frame(frame, frame_len, reply, sizeof(reply), NULL);
}

uint8_t midea_program_mode_code(midea_program_t prog)
{
    switch (prog) {
        case MIDEA_PROG_30MIN:     return 0x07;
        case MIDEA_PROG_ECO:       return 0x04;
        case MIDEA_PROG_INTENSIVE: return 0x02;
        case MIDEA_PROG_1H:
        default:                   return 0x09;
    }
}

esp_err_t midea_start_program(midea_program_t prog, bool currently_off, bool extra_dry)
{
    if (currently_off) {
        ESP_LOGI(TAG, "Powering on dishwasher before start...");
        esp_err_t perr = midea_power(true);
        if (perr != ESP_OK) {
            ESP_LOGW(TAG, "Power-on before start failed, trying start directly anyway");
        }
        vTaskDelay(pdMS_TO_TICKS(750));
    }

    uint8_t mode_byte = midea_program_mode_code(prog);
    // dodatkowe suszenie (0x01) tylko dla programu 1h
    uint8_t add_flags = (prog == MIDEA_PROG_1H && extra_dry) ? 0x01 : 0x00;

    uint8_t body[38] = {0};
    body[0] = 0x08;
    body[1] = 0x03; // start pracy
    body[2] = mode_byte;
    body[3] = add_flags;

    uint8_t frame[64];
    uint8_t reply[256];
    size_t frame_len = assemble_frame(body, sizeof(body), 0x02, frame);
    return execute_frame(frame, frame_len, reply, sizeof(reply), NULL);
}

esp_err_t midea_cancel(void)
{
    uint8_t body[38] = {0};
    body[0] = 0x08;
    body[1] = 0x01; // Return to IDLE / cancel cycle

    uint8_t frame[64];
    uint8_t reply[256];
    size_t frame_len = assemble_frame(body, sizeof(body), 0x02, frame);
    return execute_frame(frame, frame_len, reply, sizeof(reply), NULL);
}

esp_err_t midea_discover_ip(char *found_ip, size_t max_len)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return ESP_FAIL;

    int bc = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));

    struct timeval tv = {.tv_sec = 2, .tv_usec = 500000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const char *targets[2] = {"255.255.255.255", "192.168.18.255"};
    const int ports[2] = {6445, 20086};

    for (int t = 0; t < 2; t++) {
        for (int p = 0; p < 2; p++) {
            struct sockaddr_in dst = {0};
            dst.sin_family = AF_INET;
            dst.sin_port = htons((uint16_t)ports[p]);
            inet_pton(AF_INET, targets[t], &dst.sin_addr);
            sendto(sock, MIDEA_DISCOVERY_PKT, sizeof(MIDEA_DISCOVERY_PKT), 0,
                   (struct sockaddr *)&dst, sizeof(dst));
        }
    }

    uint8_t rx_buf[512];
    struct sockaddr_in src_addr = {0};
    socklen_t src_len = sizeof(src_addr);
    ssize_t n = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&src_addr, &src_len);
    close(sock);

    if (n >= 40) {
        char ip_str[16] = {0};
        inet_ntoa_r(src_addr.sin_addr, ip_str, sizeof(ip_str));
        if (strlen(ip_str) >= 7) {
            midea_api_set_ip(ip_str);
            if (found_ip && max_len > 0) {
                snprintf(found_ip, max_len, "%s", ip_str);
            }
            ESP_LOGI(TAG, "Discovered dishwasher at IP: %s", ip_str);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}
