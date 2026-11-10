# Zmywarka ESP — Opis architektury i protokołów

Dokument opisuje budowę wewnętrzną, podział zadań FreeRTOS, obsługę wyświetlacza i zasilania oraz protokoły sieciowe projektu **Zmywarka ESP** na płytce **Guition JC3248W535EN** (`ESP32-S3`, `8 MB Octal PSRAM`, `16 MB Flash`).

---

## 1. Mapa plików w repozytorium

| Ścieżka | Za co odpowiada |
|:--------|:----------------|
| `src/main.c` | Start aplikacji (`app_main`), wątek roboczy `szpont_worker`, odpytywanie zmywarki, zapis timera w NVS, SNTP, pasek postępu OTA |
| `src/ui/ui.c`, `ui_idle.c`, `ui_work.c`, `ui_overlays.c`, `ui_theme.c` | Podzielony interfejs LVGL 8.4 (`scr_loading`, `scr_main`, nakładki, gradient na łuku w PSRAM), 9 motywów w NVS, obsługa przytrzymania i przesunięć |
| `src/esp_bsp.c`, `lv_port.c` | Sterownik ekranu AXS15231B po QSPI, dotyk I²C (`0x3B`), obrót programowy `LV_DISP_ROT_90`, blokowanie odświeżania podczas snu |
| `src/screen_off.c` | Skalowanie taktowania CPU (`240 <-> 80 MHz`), harmonogram jasności PWM, ręczny tryb jasności w NVS, 10-minutowe przygaszenie po końcu cyklu |
| `src/ota_server.c` | Wbudowany serwer HTTP (`GET /`, `GET /logs`, `POST /logs/clear`, `POST /update`), bufor kołowy `16 KB` na logi z `esp_log_set_vprintf`, zapis partycji OTA |
| `src/mdns_mini.c` | Lekki responder mDNS na LwIP UDP (`224.0.0.251:5353`) rozgłaszający `zmywarka.local` |
| `src/weather.c` | Godzinowe pobieranie pogody dla Wrocławia z API **Open-Meteo** (`HTTP + cJSON`) i pamięć podręczna NVS |
| `components/wifi/` | Zarządzanie Wi-Fi STA z pamięcią BSSID/kanału w `RTC_DATA_ATTR` do szybszego łączenia i wyłączonym oszczędzaniem radia (`WIFI_PS_NONE`) |
| `components/midea_api/` | Lokalny protokół Midea V3 (`0x8370` handshake + ramki V2 AES-ECB na TCP `6444`) i wyszukiwanie UDP (`6445`, `20086`) |

---

## 2. Zadania FreeRTOS i wielowątkowość

1. **`LVGL task`** (`src/lv_port.c`): trzyma rekurencyjny muteks `lvgl_mux`, wywołuje `lv_timer_handler()` co `1–5 ms`, czyta dotyk (`INDEV` co `20 ms`) i wykonuje rysowanie/animacje.
2. **`szpont_worker`** (`src/main.c`, core `0`, stos `8192 B`, priorytet `4`):
   - Odbiera komendy z kolejki `s_cmd_queue` (głębokość `8`).
   - Odpytuje status zmywarki co **`5 s`** (gdy stoi / skończyła / offline) lub co **`8 s`** (podczas mycia).
   - Pilnuje odliczania `s_sched_target_us` (opóźniony start), nawet gdy ekran i Wi-Fi śpią.
   - Gdy `s_is_sleeping && s_sched_target_us == 0`, zasypia na kolejce (`portMAX_DELAY`), nie budząc CPU co kilka milisekund.
3. **`sys_evt` (ESP Event Loop)** (`sdkconfig.defaults`: stos zwiększony do **`4096 B`**):
   - `on_wifi_event()` nigdy nie dotyka bezpośrednio muteksu LVGL ani widżetów — wrzuca tylko lekkie `CMD_WIFI_CONNECTED` / `CMD_WIFI_DISCONNECTED` do kolejki.
4. **`mdns_mini`** (stos `3072 B`, priorytet `3`): nasłuchuje UDP `:5353` z `SO_RCVTIMEO = 500 ms` i ponownie dołącza do grupy multicast `224.0.0.251` po każdym powrocie Wi-Fi.
5. **`httpd`** (stos `6144 B`, uruchamiany tylko na żądanie): obsługuje upload OTA i podgląd `/logs`, przekazuje procent postępu do `on_ota_progress()`, a restart robi z osobnego zadania `ota_reboot` po `800 ms`, żeby przeglądarka zdążyła dostać odpowiedź `200 OK`.

---

## 3. Wyświetlacz, dotyk i wybudzanie bez migotania

### Obrót programowy i QSPI DMA
- Matryca fizycznie ma **`320×480`** (pion). `app_main()` ustawia `APP_DISPLAY_ROTATION = LV_DISP_ROT_90` (`480×320` poziom) z pełnoekranowym buforem w PSRAM (`320 * 480 * 2 B`).
- `lvgl_port_flush_callback()` obraca fragment do przemiennych buforów DMA w PSRAM (`trans_buf_1` / `trans_buf_2`) i synchronizuje kolejne paczki przez semafor `trans_done_sem`.

### Rysowanie przed włączeniem podświetlenia
Żeby przy wybudzeniu nie mignęła stara klatka sprzed uśpienia:
1. **Wejście w sen**: `enter_sleep_mode()` od razu gasi podświetlenie (`bsp_display_backlight_off()`), ustawia `s_screen_off = true`, wysyła `CMD_ENTER_SLEEP` i zwalnia blokady CPU (`80 MHz`). Wątek roboczy przygotowuje ekran powrotny (żółty pasek ponownego łączenia), na moment włącza `screen_off_set_sleep_prerender(true)` -> `lv_refr_now(NULL)` -> `screen_off_set_sleep_prerender(false)` — dzięki temu pamięć ekranu odświeża się po cichu, gdy podświetlenie jest zgaszone.
2. **Wybudzenie dotykiem (`lvgl_port_touchpad_read`)**:
   - Wykrywa dotyk, gdy `screen_off_is_off() == true`.
   - Uruchamia **`WAKE_TOUCH_GUARD_MS = 350 ms`** (zwraca `LV_INDEV_STATE_RELEASED`, żeby pierwsze dotknięcie budzące nie wcisnęło kafelka pod spodem).
   - Woła `screen_off_activity()`, co podnosi CPU z powrotem na `240 MHz` + `80 MHz APB` i zapamiętuje docelową jasność (`s_pending_wake_bri`), ale **jeszcze nie zapala podświetlenia**.
   - Wywołuje `ui_prepare_wake_screen()` + `lv_obj_invalidate()` + `lv_refr_now()`. Dopiero gdy ostatnia paczka QSPI DMA przejdzie (`trans_done_sem`, max `80 ms`), `lvgl_port_flush_callback()` odpala `screen_off_complete_wake_backlight()`.
   - Na wypadek wybudzenia nie przez dotyk działa awaryjny timer `350 ms`.

---

## 4. Zarządzanie energią i jasnością

`screen_off_init()` konfiguruje `esp_pm_configure(&pm_cfg)` (`max_freq_mhz = 240`, `min_freq_mhz = 80`, `light_sleep_enable = false` — ciągły zegar APB jest potrzebny dla LEDC PWM).
- **Przy usypianiu:** najpierw gasi LEDC PWM, potem zwalnia `ESP_PM_CPU_FREQ_MAX` i `ESP_PM_APB_FREQ_MAX`.
- **Przy wybudzaniu:** najpierw podnosi zegary PM, dopiero potem włącza LEDC PWM.

| Pora / Stan | Zachowanie w trybie `Auto` | Tryby ręczne |
|:------------|:---------------------------|:-------------|
| **`06:00 – 17:59`** | `100%` jasności, usypia po `45 s` bezczynności (w trakcie mycia nie gaśnie) | `SCREEN_BRI_100` (`100%`) lub `SCREEN_BRI_50` (`50%`) zapisane w NVS `"bri_mode"` |
| **`18:00 – 21:59`** | `60%` jasności, usypia po `45 s` bezczynności (w trakcie mycia nie gaśnie) | Jak wyżej — ręczny tryb nadpisuje jasność |
| **`22:00 – 05:59`** | Tryb nocny: po kliknięciu świeci `35%` przez `15 s` i znów gaśnie (nawet podczas mycia) | Działa także w nocy dla jasności, ale krótkie wygaszanie zostaje |
| **`MIDEA_STATE_FINISH`** | Trzyma podgląd końca przygaszony do **`28%`** przez **`10 minut`** (chyba że jest noc, klikniesz uśpienie, zamkniesz kartę albo otworzysz drzwi) | — |

---

## 5. Lokalny protokół Midea V3 (`components/midea_api`)

- **Połączenie:** TCP na porcie **`6444`** (nieblokujące `connect` z limitem `1500 ms` + timeouty gniazda).
- **Sesja (`v3_handshake`):** wysyła ramkę `0x83 0x70` z `MIDEA_TOKEN` (`64 B`), odszyfrowuje odpowiedź kluczem z `MIDEA_KEY` (SHA-256), dostaje `32 B` klucza sesji TCP i dalej wymienia paczki AES-CBC opakowujące klasyczne ramki V2 (`0x5A 0x5A` + AES-ECB + podpis MD5).
- **Komendy (`msg_type = 0x02`):**
  - `body[0] = 0x08`, `body[1] = 0x03` (start mycia) lub `0x01` (stop / czuwanie), `body[2] = kod programu`, `body[3] = opcje`.
  - Mapowanie programów (`midea_program_mode_code`):
    - `MIDEA_PROG_1H` -> `0x09` (`add_flags = 0x01` dla Extra Dry, inaczej `0x00`)
    - `MIDEA_PROG_30MIN` -> `0x07`
    - `MIDEA_PROG_ECO` -> `0x04`
    - `MIDEA_PROG_INTENSIVE` -> `0x02`
  - Jeśli zmywarka jest całkiem wyłączona (`cs == 0x00`), `midea_start_program()` najpierw ją wybudza (`0x01`), czeka `750 ms` i dopiero wysyła start programu; `execute_start_cmd()` dodatkowo sprawdza status i w razie potrzeby ponawia raz.
- **Odczyt statusu (`msg_type = 0x03`):** wyciąga `body[1]` (stan pracy), `body[2]` (program), `body[5]` (bity: `0x01` drzwi zamknięte, `0x02` brak nabłyszczacza, `0x04` brak soli), `body[6]` / `body[32]` (pozostałe minuty), `body[9]` (etap `1..5`), `body[10]` (kod błędu) i `body[11]` (temperatura `°C`).

---

## 6. Sieć, SNTP, OTA, logi i klucze w pamięci NVS

- **Szybki SNTP:** `CONFIG_LWIP_SNTP_MAX_SERVERS=3` z bezpośrednimi adresami anycast `162.159.200.1`, `216.239.35.0` oraz zapasowym `pool.ntp.org`. Callback `esp_sntp_set_time_sync_notification_cb()` od razu zleca `CMD_SYNC_CLOCK` (a po wybudzeniu / powrocie Wi-Fi, jeśli zegar jest sprzed `2024`, woła `esp_sntp_restart()`).
- **Partycje (`partitions.csv`):** `nvs` (`0x9000`), `otadata` (`0xe000`), `ota_0` (`0x10000`, `6,25 MB`), `ota_1` (`0x650000`, `6,25 MB`). Na starcie `app_main()` zatwierdza działający obraz przez `esp_ota_mark_app_valid_cancel_rollback()`.
- **Logi w przeglądarce (`ota_server.c`):** `ota_log_init()` podpina lekki hook `esp_log_set_vprintf`, usuwa kody kolorów ANSI i zapisuje tekst do `16 KB` bufora kołowego pod `portMUX_TYPE`, skąd przeglądarka pobiera je z `/logs`.
- **Klucze w NVS (`NVS_NAMESPACE = "szpont_esp"`):**
  - `dish_ip` (`str`), `ui_theme` (`u8`), `bri_mode` (`u8`), `sched_ep` (`i64` — czas Unix startu), `sched_pr` (`u8`), `sched_dr` (`u8`), `w_ep` / `w_tc` / `w_code` / `w_day` / `w_dcnt` / `w_days` / `w_city` / `w_lat` / `w_lon` (pogoda 7-dniowa i wybrane miasto) oraz dane Wi-Fi.
