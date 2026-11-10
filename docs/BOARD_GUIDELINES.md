# Guition JC3248W535EN — Piny płytki i wskazówki programistyczne

Sprawdzone piny sprzętowe oraz zasady pracy z ESP-IDF + LVGL na płytce **JC3248W535EN** (`ESP32-S3`, ekran QSPI AXS15231B `320×480` + dotyk I²C, `16 MB` QIO Flash, `8 MB` Octal PSRAM).

---

## 1. Zweryfikowane piny na płytce (`config.h` / `esp_bsp.h`)

### Wyświetlacz (AXS15231B po QSPI / `SPI2_HOST`)
| Sygnał | GPIO | Uwagi |
|:-------|:----:|:------|
| `QSPI_CS` | `45` | Chip select |
| `QSPI_PCLK` | `47` | Zegar QSPI |
| `QSPI_D0` .. `QSPI_D3` | `21, 48, 40, 39` | 4 linie danych QSPI |
| `QSPI_TE` | `38` | Sygnał Tearing Effect (z podciągnięciem `Pullup`) |
| `QSPI_BL` | `1` | Podświetlenie LEDC PWM (w uśpieniu stan niski `LOW`) |
| `QSPI_RST` | `-1` | Połączone z `EN` modułu |

### Panel dotykowy (I²C @ `0x3B`, `400 kHz`)
| Sygnał | GPIO | Uwagi |
|:-------|:----:|:------|
| `TOUCH_SDA` | `4` | `BSP_I2C_NUM = I2C_NUM_0` |
| `TOUCH_SCL` | `8` | 400 kHz master |
| `TOUCH_INT` | `3` | Przerwanie RTC GPIO 3 |
| `TOUCH_RST` | `-1` | Brak osobnego pinu resetu |

### Złącza audio i rozszerzeń z tyłu płytki
| Złącze | Rola | Piny / uwagi |
|:-------|:-----|:-------------|
| **`Speak`** (`JST 1.25 2P`) | Wbudowany wzmacniacz mono **NS4168** (I²S Class-D) | `BCK = GPIO 42`, `LRCK/WS = GPIO 2`, `DIN = GPIO 41` (można wpiąć mały głośnik `4Ω/8Ω`) |
| **`JST 1.25 8P IO`** | Wolne piny cyfrowe / ADC | **`GPIO 5, 6, 7, 15, 16, 46, 9, 14`** *(unikaj `GPIO 5` — dzielnik baterii i `GPIO 46` — pin rozruchowy/strapping)* |
| **`JST 1.25 4P` / `HC1.0 4P`** | Zasilanie pomocnicze + I²C/UART | Wyprowadza `3.3V`, `GND` oraz linie sygnałowe (przed podpięciem zawsze sprawdź opis na laminacie) |

> **Konsola USB (`COM3`)**: działa przez wbudowany **USB-Serial-JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Nie używaj **`GPIO 19`** ani **`GPIO 20`** (linie USB D-/D+) do innych celów.

---

## 2. Zasady pracy z ekranem i LVGL

1. **Poza wątkiem LVGL zawsze blokuj `bsp_display_lock()`:**
   - Nigdy nie wywołuj `lv_*` ani `ui_*` z `worker_task`, serwera HTTP, timerów czy handlerów Wi-Fi bez objęcia parą `bsp_display_lock(timeout_ms)` / `bsp_display_unlock()`.
2. **Obrót programowy i bufory w PSRAM:**
   - Pionowy ekran `320×480` obracany jest przez `LV_DISP_ROT_90` do `480×320` w `lv_port.c` na pełnoekranowym buforze PSRAM (`CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`). Nie rób pętli opóźniających w `flush_cb` — tempem przesyłu steruje `trans_done_sem`.
3. **Nie psuj wybudzania starym obrazem:**
   - Bramkuj `lvgl_port_flush_callback()` funkcją `screen_off_is_flush_paused()`, a **nie** samym `screen_off_is_off()`.
   - Przy wybudzaniu włącz podświetlenie dopiero po `lv_refr_now()` i zakończeniu DMA (`trans_done_sem`).
   - Zachowaj `WAKE_TOUCH_GUARD_MS` (`350 ms`), żeby pierwsze dotknięcie budzące nie uruchomiło przycisku pod palcem.
4. **Zakres znaków we własnych fontach (`src/ui/fonts/`):**
   - Zanim użyjesz wygenerowanego fontu, sprawdź tablicę `cmaps[]`. **`pl_font_18.c` ma tylko spację (`32`) oraz wielkie litery `A–Z` (`65–90`)** (wycięty pod `ZATRZYMAJ`) — brakuje w nim cyfr, małych liter, kropek/dwukropków i polskich znaków! Do mieszanych tekstów bierz **`&pl_font_16`**, **`&pl_font_22`**, **`&pl_font_30`**, **`&pl_font_52`** lub **`&pl_font_104`**.
5. **Dotyk, paski postępu i zaokrąglenia:**
   - **Przyciski z paskiem wypełnienia** (`START`, `ZATRZYMAJ`): ustaw `pad_all = 0` i `lv_obj_set_style_clip_corner(btn, true, 0)`, utwórz pasek wypełnienia *przed* etykietami/ikoną, w `LV_EVENT_PRESSING` sprawdzaj współrzędne palca (`lv_obj_get_coords`) i reaguj na `LV_EVENT_SHORT_CLICKED` zamiast `LV_EVENT_CLICKED`, jeśli rozróżniasz klik od przytrzymania.
   - **Propagacja gestów:** wyłącz `LV_OBJ_FLAG_GESTURE_BUBBLE` na podkafelkach z własnym przesunięciem (`on_prog_slot`), żeby gest nie uciekał wyżej.
   - **Małe kółka (`16×16`):** wyłącz `LV_OBJ_FLAG_SCROLLABLE`, ustaw `LV_SCROLLBAR_MODE_OFF`, wyzeruj padding i stosuj `border_width = 2` (albo `0`) zamiast `1`, żeby nie pojawiały się artefakty narożników w RGB565.

---

## 3. RTOS, pamięć i stosy

1. **Chroń stos zadania `sys_evt` (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096`):**
   - Domyślne `2304 B` łatwo przepełnić. Z `on_wifi_event()` wrzucaj tylko lekką komendę `app_cmd_t` do kolejki `worker_task` (`8192 B` stosu).
2. **Hook `esp_log_set_vprintf` musi być bardzo lekki:**
   - Wykonuje się na stosie każdego zadania wołającego `ESP_LOG*` (także `sys_evt` i `httpd`). Trzymaj małe bufory na stosie, ignoruj przerwania (`!xPortInIsrContext()`) i kopiuj log do bufora kołowego pod krótką sekcją krytyczną (`portMUX_TYPE`).
3. **Zmiany Kconfig rób w `sdkconfig.defaults`, nie w `sdkconfig.jc3248w535`:**
   - PlatformIO ignoruje zmiany w `sdkconfig.defaults`, dopóki istnieje wygenerowany `sdkconfig.jc3248w535`. Po edycji `sdkconfig.defaults` usuń raz `sdkconfig.jc3248w535`, żeby przebudował się na nowo.

---

## 4. Zasilanie, Wi-Fi i wgrywanie po kablu vs OTA

1. **Kolejność blokad częstotliwości (`screen_off.c`):**
   - `light_sleep_enable` musi zostać `false`, gdy używasz PWM na `GPIO 1`.
   - Zwalniaj `ESP_PM_CPU_FREQ_MAX` i `ESP_PM_APB_FREQ_MAX` *dopiero po* zgaszeniu podświetlenia i podnoś je *przed* jego ponownym włączeniem.
2. **Opóźnienia Wi-Fi:**
   - Trzymaj `esp_wifi_set_ps(WIFI_PS_NONE)` przy aktywnym ekranie, żeby komunikacja TCP (`6444`) ze zmywarką nie przycinała na oszczędzaniu DTIM. Przy uśpieniu zatrzymuj radio (`wifi_manager_stop()`), jeśli nie czeka timer ani serwer OTA.
3. **Wgrywanie po USB po wcześniejszym OTA (`partitions.csv`):**
   - OTA przełącza aktywny slot startowy między `ota_0` (`0x10000`) a `ota_1` (`0x650000`) w partycji `otadata` (`0xe000`).
   - Gdy potem wracasz do wgrywania przez USB, odpal pełne **`pio run -t upload`** (bez `-t nobuild`), żeby wyczyścić `otadata` (`0xe000`) obrazem `ota_data_initial.bin` i ustawić start z powrotem na `ota_0`.
   - Po udanym starcie w `app_main()` zawsze wołaj `esp_ota_mark_app_valid_cancel_rollback()`.
