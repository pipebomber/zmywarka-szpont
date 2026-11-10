#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"

#include "ota_server.h"
#include "weather.h"

static const char *TAG = "OTA_SRV";
static httpd_handle_t s_httpd = NULL;
static ota_progress_cb_t s_progress_cb = NULL;

// bufor kolowy logow dla podgladu w przegladarce (/logs)
#define OTA_LOG_BUF_SIZE 16384
static char s_log_buf[OTA_LOG_BUF_SIZE];
static size_t s_log_head = 0;
static size_t s_log_len = 0;
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_orig_vprintf = NULL;
static bool s_log_inited = false;

static void log_ring_append_clean(char *str, size_t len)
{
    if (!str || len == 0) return;

    // wycinamy kody kolorow ANSI w miejscu, bez alokacji na stosie
    size_t clen = 0;
    bool in_esc = false;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\x1b') {
            in_esc = true;
            continue;
        }
        if (in_esc) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                in_esc = false;
            }
            continue;
        }
        str[clen++] = c;
    }
    if (clen == 0) return;

    portENTER_CRITICAL(&s_log_mux);
    for (size_t i = 0; i < clen; i++) {
        s_log_buf[s_log_head] = str[i];
        s_log_head = (s_log_head + 1) % OTA_LOG_BUF_SIZE;
        if (s_log_len < OTA_LOG_BUF_SIZE) {
            s_log_len++;
        }
    }
    portEXIT_CRITICAL(&s_log_mux);
}

static int ota_vprintf_hook(const char *fmt, va_list ap)
{
    if (!xPortInIsrContext()) {
        va_list ap_copy;
        va_copy(ap_copy, ap);
        char tmp[160];
        int n = vsnprintf(tmp, sizeof(tmp), fmt, ap_copy);
        va_end(ap_copy);
        if (n > 0) {
            size_t store = ((size_t)n < sizeof(tmp)) ? (size_t)n : (sizeof(tmp) - 1);
            log_ring_append_clean(tmp, store);
        }
    }
    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, ap);
    }
    return vprintf(fmt, ap);
}

void ota_log_init(void)
{
    if (s_log_inited) return;
    s_log_inited = true;
    s_orig_vprintf = esp_log_set_vprintf(ota_vprintf_hook);
}

void ota_server_set_progress_cb(ota_progress_cb_t cb)
{
    s_progress_cb = cb;
}

static const char OTA_HTML[] =
"<!DOCTYPE html><html lang='pl'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Zmywarka ESP · OTA & Logi</title>"
"<style>"
"*{box-sizing:border-box}"
"body{font-family:system-ui,-apple-system,sans-serif;background:#090d16;color:#f8fafc;"
"display:flex;flex-direction:column;align-items:center;min-height:100vh;margin:0;padding:20px 14px;gap:16px}"
".card{background:#111726;border:1px solid #222d42;border-radius:18px;padding:22px;max-width:760px;width:100%}"
"h2{margin:0 0 6px;color:#38bdf8;font-size:20px}p{color:#94a3b8;font-size:14px;line-height:1.45;margin:6px 0}"
".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;justify-content:space-between}"
"input[type=file]{width:100%;margin:12px 0;color:#cbd5e1}"
"input[type=text]{flex:1;min-width:190px;padding:10px 12px;border-radius:10px;border:1px solid #2a3954;background:#090d16;color:#f8fafc;font-size:14px}"
"button{padding:11px 16px;border:0;border-radius:11px;background:#0ea5e9;color:#fff;"
"font-size:14px;font-weight:700;cursor:pointer}button:disabled{opacity:.5}"
".btn-main{width:100%;padding:14px;font-size:16px}"
".btn-sec{background:#1e293b;color:#e2e8f0;padding:8px 12px;font-size:13px}"
".btn-sec.on{background:#0284c7;color:#fff}"
".city-item{display:flex;align-items:center;justify-content:space-between;gap:10px;background:#090d16;border:1px solid #1e293b;border-radius:11px;padding:9px 12px;margin-top:8px}"
".city-meta{font-size:12px;color:#94a3b8}"
"#bar{height:10px;background:#1e293b;border-radius:6px;margin-top:14px;overflow:hidden;display:none}"
"#fill{height:100%;width:0;background:#10b981;transition:width .15s}"
"#msg,#wmsg{margin-top:10px;font-size:14px;font-weight:600}"
"pre#logs{background:#060910;border:1px solid #1e293b;border-radius:12px;padding:12px;"
"margin:12px 0 0;height:380px;overflow:auto;font:12px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace;"
"color:#cbd5e1;white-space:pre-wrap;word-break:break-word}"
"</style></head><body>"
"<div class='card'><h2>Zmywarka ESP · OTA v2</h2>"
"<p>Wybierz plik <code>firmware.bin</code> (<code>E:\\esp\\.pio\\build\\jc3248w535\\firmware.bin</code>), aby zaktualizować przez Wi-Fi.</p>"
"<input id='f' type='file' accept='.bin'><button id='btn' class='btn-main' onclick='go()'>Wgraj oprogramowanie</button>"
"<div id='bar'><div id='fill'></div></div><div id='msg'></div></div>"
"<div class='card'><div class='row'><h2 style='margin:0'>Pogoda na zegarze (Open-Meteo)</h2>"
"<button class='btn-sec' onclick='setCity(\"Wrocław\",\"51.1029\",\"17.0301\")'>Domyślne: Wrocław</button></div>"
"<p>Aktywne miasto: <strong id='curCity' style='color:#f8fafc'>...</strong></p>"
"<div class='row' style='margin-top:10px'>"
"<input id='cityQ' type='text' placeholder='Wpisz miasto (np. Wrocław, Kalisz, Poznań)...' onkeydown='if(event.key===\"Enter\")searchCity()'>"
"<button id='sbtn' onclick='searchCity()'>Szukaj</button></div>"
"<div id='wmsg'></div><div id='cityRes'></div></div>"
"<div class='card'><div class='row'><h2 style='margin:0'>Logi ESP32 (Live)</h2>"
"<div style='display:flex;gap:8px'>"
"<button id='autoBtn' class='btn-sec on' onclick='toggleAuto()'>Auto: WŁ</button>"
"<button class='btn-sec' onclick='fetchLogs(true)'>Odśwież</button>"
"<button class='btn-sec' onclick='copyLogs()'>Kopiuj</button>"
"<button class='btn-sec' onclick='clearLogs()'>Wyczyść</button>"
"</div></div>"
"<pre id='logs'>Ładowanie logów...</pre></div>"
"<script>"
"var autoOn=true,timer=null;"
"function go(){var f=document.getElementById('f').files[0];if(!f){alert('Wybierz plik .bin');return;}"
"var b=document.getElementById('btn'),m=document.getElementById('msg'),bar=document.getElementById('bar'),"
"fill=document.getElementById('fill');b.disabled=true;bar.style.display='block';m.textContent='Wysyłanie...';"
"var x=new XMLHttpRequest();x.open('POST','/update',true);x.upload.onprogress=function(e){if(e.lengthComputable){"
"var p=Math.round(e.loaded*100/e.total);fill.style.width=p+'%';m.textContent='Wgrywanie: '+p+'%';}};"
"x.onload=function(){if(x.status===200){fill.style.width='100%';m.style.color='#34d399';"
"m.textContent='Gotowe! Zmywarka ESP uruchamia się ponownie...';}else{m.style.color='#f87171';"
"m.textContent='Błąd: '+x.responseText;b.disabled=false;}};x.onerror=function(){m.style.color='#f87171';"
"m.textContent='Błąd połączenia';b.disabled=false;};x.send(f);}"
"function fetchLogs(forceScroll){var pre=document.getElementById('logs');"
"var atBot=forceScroll||(pre.scrollTop+pre.clientHeight>=pre.scrollHeight-24);"
"fetch('/logs').then(function(r){return r.text();}).then(function(t){"
"if(pre.textContent!==t){pre.textContent=t||'(brak logów)';if(atBot)pre.scrollTop=pre.scrollHeight;}"
"}).catch(function(){});}"
"function toggleAuto(){autoOn=!autoOn;var b=document.getElementById('autoBtn');"
"b.textContent='Auto: '+(autoOn?'WŁ':'WYŁ');b.className='btn-sec'+(autoOn?' on':'');}"
"function clearLogs(){fetch('/logs/clear',{method:'POST'}).then(function(){fetchLogs(true);});}"
"function copyLogs(){var t=document.getElementById('logs').textContent;"
"if(navigator.clipboard)navigator.clipboard.writeText(t);}"
"function loadCity(){fetch('/weather/city').then(function(r){return r.json();}).then(function(d){"
"document.getElementById('curCity').textContent=d.city+' ('+d.lat+', '+d.lon+')';}).catch(function(){});}"
"function setCity(n,la,lo){var wm=document.getElementById('wmsg');wm.style.color='#38bdf8';wm.textContent='Zapisywanie...';"
"fetch('/weather/city',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({city:n,lat:String(la),lon:String(lo)})}).then(function(r){return r.json();}).then(function(d){"
"if(d.ok){wm.style.color='#34d399';wm.textContent='Ustawiono: '+d.city+' (prognoza odświeży się za chwilę)';"
"document.getElementById('cityRes').innerHTML='';loadCity();}else{wm.style.color='#f87171';wm.textContent='Błąd zapisu miasta';}"
"}).catch(function(){wm.style.color='#f87171';wm.textContent='Błąd połączenia';});}"
"function searchCity(){var q=document.getElementById('cityQ').value.trim();var res=document.getElementById('cityRes');"
"var wm=document.getElementById('wmsg');if(q.length<2){wm.style.color='#f59e0b';wm.textContent='Wpisz min. 2 znaki';return;}"
"var sb=document.getElementById('sbtn');sb.disabled=true;wm.style.color='#94a3b8';wm.textContent='Szukanie...';res.innerHTML='';"
"fetch('/weather/search?q='+encodeURIComponent(q)).then(function(r){return r.json();}).then(function(d){sb.disabled=false;"
"var arr=d.results||[];if(!arr.length){wm.style.color='#f59e0b';wm.textContent='Brak wyników dla: '+q;return;}wm.textContent='';"
"arr.forEach(function(it){var row=document.createElement('div');row.className='city-item';"
"var info=document.createElement('div');var sub=[it.admin1,it.country_code].filter(Boolean).join(', ');"
"var st=document.createElement('strong');st.textContent=it.name;var sp=document.createElement('span');"
"sp.className='city-meta';sp.textContent=' '+sub+' · '+it.latitude.toFixed(3)+', '+it.longitude.toFixed(3);"
"info.appendChild(st);info.appendChild(sp);"
"var b=document.createElement('button');b.className='btn-sec on';b.textContent='Wybierz';"
"b.onclick=function(){setCity(it.name,it.latitude.toFixed(4),it.longitude.toFixed(4));};"
"row.appendChild(info);row.appendChild(b);res.appendChild(row);});}).catch(function(){sb.disabled=false;wm.style.color='#f87171';wm.textContent='Błąd wyszukiwania';});}"
"loadCity();fetchLogs(true);setInterval(function(){if(autoOn)fetchLogs(false);},1200);"
"</script></body></html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, OTA_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t logs_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char *snap = malloc(OTA_LOG_BUF_SIZE + 1);
    if (!snap) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    }

    size_t out_len = 0;
    portENTER_CRITICAL(&s_log_mux);
    if (s_log_len > 0) {
        size_t start = (s_log_head + OTA_LOG_BUF_SIZE - s_log_len) % OTA_LOG_BUF_SIZE;
        if (start + s_log_len <= OTA_LOG_BUF_SIZE) {
            memcpy(snap, &s_log_buf[start], s_log_len);
        } else {
            size_t first = OTA_LOG_BUF_SIZE - start;
            memcpy(snap, &s_log_buf[start], first);
            memcpy(snap + first, s_log_buf, s_log_len - first);
        }
        out_len = s_log_len;
    }
    portEXIT_CRITICAL(&s_log_mux);
    snap[out_len] = '\0';

    esp_err_t err = httpd_resp_send(req, snap, (ssize_t)out_len);
    free(snap);
    return err;
}

static esp_err_t logs_clear_post_handler(httpd_req_t *req)
{
    portENTER_CRITICAL(&s_log_mux);
    s_log_head = 0;
    s_log_len = 0;
    portEXIT_CRITICAL(&s_log_mux);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "OK");
}

static void delayed_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware");
        return ESP_FAIL;
    }

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int last_pct = -1;
    bool ok = true;
    ESP_LOGI(TAG, "Receiving OTA binary (%d bytes)...", req->content_len);
    if (s_progress_cb) s_progress_cb(0, false, true);

    while (remaining > 0) {
        int to_read = remaining > 4096 ? 4096 : remaining;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ok = false;
            break;
        }
        if (esp_ota_write(ota_handle, buf, (size_t)r) != ESP_OK) {
            ok = false;
            break;
        }
        remaining -= r;
        int pct = (int)(((int64_t)(req->content_len - remaining) * 100LL) / req->content_len);
        if (s_progress_cb && pct != last_pct && (pct - last_pct >= 3 || pct == 100)) {
            last_pct = pct;
            s_progress_cb(pct, false, true);
        }
    }
    free(buf);

    if (!ok) {
        esp_ota_abort(ota_handle);
        if (s_progress_cb) s_progress_cb(0, true, false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Stream write failed");
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        if (s_progress_cb) s_progress_cb(0, true, false);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        if (s_progress_cb) s_progress_cb(0, true, false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update verified! Rebooting...");
    if (s_progress_cb) s_progress_cb(100, true, true);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(delayed_restart_task, "ota_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t weather_city_get_handler(httpd_req_t *req)
{
    char city[32], lat[16], lon[16];
    weather_get_city(city, sizeof(city), lat, sizeof(lat), lon, sizeof(lon));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "city", city);
    cJSON_AddStringToObject(root, "lat", lat);
    cJSON_AddStringToObject(root, "lon", lon);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = json ? httpd_resp_sendstr(req, json)
                         : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    free(json);
    return err;
}

static esp_err_t weather_city_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 256) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }

    char body[257];
    int total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, body + total, req->content_len - total);
        if (r <= 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv error");
        total += r;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    cJSON *jc = root ? cJSON_GetObjectItemCaseSensitive(root, "city") : NULL;
    cJSON *jla = root ? cJSON_GetObjectItemCaseSensitive(root, "lat") : NULL;
    cJSON *jlo = root ? cJSON_GetObjectItemCaseSensitive(root, "lon") : NULL;

    bool ok = false;
    char saved_city[32] = {0};
    if (cJSON_IsString(jc) && cJSON_IsString(jla) && cJSON_IsString(jlo)) {
        if (weather_set_city(jc->valuestring, jla->valuestring, jlo->valuestring) == ESP_OK) {
            snprintf(saved_city, sizeof(saved_city), "%s", jc->valuestring);
            ok = true;
        }
    }
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", ok);
    if (ok) cJSON_AddStringToObject(resp, "city", saved_city);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t err = json ? httpd_resp_sendstr(req, json)
                         : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    free(json);
    return err;
}

static esp_err_t weather_search_get_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char raw_q[96] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "q", raw_q, sizeof(raw_q)) != ESP_OK ||
        !raw_q[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing q");
    }

    char safe_q[128];
    size_t si = 0;
    for (size_t i = 0; raw_q[i] && si + 3 < sizeof(safe_q); i++) {
        unsigned char ch = (unsigned char)raw_q[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '%' || ch == '-' || ch == '_' || ch == '.') {
            safe_q[si++] = (char)ch;
        } else if (ch == '+' || ch == ' ') {
            safe_q[si++] = '%'; safe_q[si++] = '2'; safe_q[si++] = '0';
        }
    }
    safe_q[si] = '\0';

    char url[256];
    snprintf(url, sizeof(url),
             "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=6&language=pl&format=json",
             safe_q);

    const int max_rx = 16384;
    char *buf = malloc((size_t)max_rx);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
    };
    int total = 0;
    int status = 0;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client) {
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        if (esp_http_client_open(client, 0) == ESP_OK) {
            esp_http_client_fetch_headers(client);
            status = esp_http_client_get_status_code(client);
            while (total < max_rx - 1) {
                int r = esp_http_client_read(client, buf + total, max_rx - 1 - total);
                if (r <= 0) break;
                total += r;
            }
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    buf[total] = '\0';

    // przytnij odpowiedz tylko do potrzebnych pol zeby duze miasta z wieloma kodami pocztowymi sie nie urwaly
    cJSON *out = cJSON_CreateObject();
    cJSON *out_arr = cJSON_AddArrayToObject(out, "results");
    if (status == 200 && total > 0) {
        cJSON *parsed = cJSON_Parse(buf);
        cJSON *arr = parsed ? cJSON_GetObjectItemCaseSensitive(parsed, "results") : NULL;
        if (cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++) {
                cJSON *it = cJSON_GetArrayItem(arr, i);
                cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
                cJSON *la = cJSON_GetObjectItemCaseSensitive(it, "latitude");
                cJSON *lo = cJSON_GetObjectItemCaseSensitive(it, "longitude");
                if (!cJSON_IsString(nm) || !cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) continue;

                cJSON *entry = cJSON_CreateObject();
                cJSON_AddStringToObject(entry, "name", nm->valuestring);
                cJSON *ad1 = cJSON_GetObjectItemCaseSensitive(it, "admin1");
                if (cJSON_IsString(ad1)) cJSON_AddStringToObject(entry, "admin1", ad1->valuestring);
                cJSON *cc = cJSON_GetObjectItemCaseSensitive(it, "country_code");
                if (cJSON_IsString(cc)) cJSON_AddStringToObject(entry, "country_code", cc->valuestring);
                cJSON_AddNumberToObject(entry, "latitude", la->valuedouble);
                cJSON_AddNumberToObject(entry, "longitude", lo->valuedouble);
                cJSON_AddItemToArray(out_arr, entry);
            }
        }
        cJSON_Delete(parsed);
    }
    free(buf);

    char *out_json = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = out_json ? httpd_resp_sendstr(req, out_json)
                             : httpd_resp_sendstr(req, "{\"results\":[]}");
    free(out_json);
    return err;
}

esp_err_t ota_server_start(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 10;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler
    };
    const httpd_uri_t uri_logs = {
        .uri = "/logs", .method = HTTP_GET, .handler = logs_get_handler
    };
    const httpd_uri_t uri_logs_clear = {
        .uri = "/logs/clear", .method = HTTP_POST, .handler = logs_clear_post_handler
    };
    const httpd_uri_t uri_update = {
        .uri = "/update", .method = HTTP_POST, .handler = update_post_handler
    };
    const httpd_uri_t uri_w_city_get = {
        .uri = "/weather/city", .method = HTTP_GET, .handler = weather_city_get_handler
    };
    const httpd_uri_t uri_w_city_post = {
        .uri = "/weather/city", .method = HTTP_POST, .handler = weather_city_post_handler
    };
    const httpd_uri_t uri_w_search = {
        .uri = "/weather/search", .method = HTTP_GET, .handler = weather_search_get_handler
    };
    httpd_register_uri_handler(s_httpd, &uri_index);
    httpd_register_uri_handler(s_httpd, &uri_logs);
    httpd_register_uri_handler(s_httpd, &uri_logs_clear);
    httpd_register_uri_handler(s_httpd, &uri_update);
    httpd_register_uri_handler(s_httpd, &uri_w_city_get);
    httpd_register_uri_handler(s_httpd, &uri_w_city_post);
    httpd_register_uri_handler(s_httpd, &uri_w_search);
    ESP_LOGI(TAG, "OTA & Live Logs web server started on port 80");
    return ESP_OK;
}

void ota_server_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGI(TAG, "OTA web server stopped");
    }
}

bool ota_server_is_running(void)
{
    return (s_httpd != NULL);
}
