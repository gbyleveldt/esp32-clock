# DEVELOPMENT.md — esp32-clock

Developer-facing context for maintainers picking up this project cold. For build instructions and usage see README.md. For reusable patterns see the relevant skill files in `gbyleveldt/claude-skills`.

---

## Architecture Overview

Single-core ESP32-C3. No RTOS task split — everything runs on the single core via the `esp_lvgl_port` task scheduler and the ESP-IDF event loop. WiFi events, NTP, and LVGL all coexist through the event-driven model without explicit task management.

Five logical modules, each a `.cpp/.h` pair in `main/`:

| Module | Responsibility |
|--------|----------------|
| `main.cpp` | `app_main` only — init sequence, WiFi state callback |
| `display.cpp` | SPI bus, GC9A01 panel, LVGL port init, LEDC backlight |
| `clock_face.cpp` | All LVGL UI — clock face, settings screen, AP screen, gestures |
| `wifi_manager.cpp` | WiFi STA/AP switching, NTP, HTTP config portal |
| `config.cpp` | NVS namespace read/write for all persisted settings |

---

## Display Initialisation — Non-Obvious Decisions

### LCD_RST = -1
No dedicated reset pin is wired on the ESP32-2424S012. Hardware reset happens via the EN pin (power cycle). `LCD_RST = -1` tells the driver not to toggle a reset GPIO — this is correct for this board, not an oversight.

### SPI at 40MHz
`LCD_SPI_CLOCK = 40MHz`. The GC9A01 supports higher, and the ESP32-C3 SPI peripheral can go faster, but 40MHz was stable and the display refresh rate is not a bottleneck for a clock face. No reason to push it.

### rgb_endian = LCD_RGB_ENDIAN_BGR
This field in `panel_cfg` is set to BGR but is effectively ignored by the GC9A01 driver in this ESP-IDF version — the actual colour byte swap is handled via menuconfig ("Swap 2 bytes of RGB565 color"). The BGR value is a leftover from colour debugging and is harmless. Do not remove it expecting the colours to change — they won't.

See `components/gc9a01.md` for the full colour swap story.

### esp_lcd_panel_mirror(true, false) — Mirror X
The display rendered horizontally mirrored without this call. Mirror X = true, Mirror Y = false was found correct for this board. This is hardware-specific — do not remove it.

### esp_lcd_panel_invert_color(true)
Required for the GC9A01 to render correct colours. The panel inverts its own output; this call compensates. Required on this display, not optional.

### Single render buffer, 40 rows
```cpp
.buffer_size = LCD_WIDTH * 40,  // 9600 pixels = ~19KB
.double_buffer = false,
```
Not double-buffered. For a clock face updating at 100ms intervals this is adequate — there is no animation complex enough to cause visible tearing. Double-buffering would use ~38KB of RAM, which is not worth it here.

### Backlight minimum 50%
`BACKLIGHT_MIN_PCT 50`. Below 50%, the display looks very dim on this hardware due to the MOSFET (AO3402) gate drive characteristics at low PWM duty cycles. The 50% floor is deliberate, not arbitrary. The LEDC frequency is 5kHz — above the threshold for visible flicker with the human eye.

---

## Clock Face Rendering — Non-Obvious Decisions

### Hand shape: hollow V using three lv_line objects per hand
LVGL v8 does not have a filled polygon primitive. The hollow outline hand style is achieved with three `lv_line` objects per hand:
- Left edge: pivot base-left → tip
- Right edge: pivot base-right → tip  
- Base: base-left → base-right (closes the open end)

The hollow look comes from the clock face background showing through the centre of the V — there is no filled area. This is a constraint of LVGL v8's drawing model, not a stylistic preference for the wireframe look.

### Smooth seconds hand: gettimeofday + 100ms timer
`gettimeofday()` provides microsecond resolution. The fractional second is added to `tm_sec` to give smooth angular interpolation:
```cpp
float smooth_sec = timeinfo.tm_sec + (tv.tv_usec / 1000000.0f);
```
The LVGL timer fires every 100ms. The seconds hand therefore moves in 10 visible steps per second rather than jumping once per second. The hour and minute hands are updated on the same 100ms tick but their motion is imperceptible at that cadence — no separate slow timer is needed.

### dot_pts[60][2] must be static
```cpp
static lv_point_t dot_pts[60][2];
```
`lv_line` stores a **pointer** to the points array, not a copy. If this array were stack-allocated inside `create_clock_screen()`, all 60 dot lines would hold dangling pointers after the function returns and would render garbage or crash. The `static` is load-bearing — do not change it.

### Z-ordering: date labels before hands
LVGL renders objects in creation order — later objects appear on top. The date labels (`label_day`, `label_date`) are created **before** the hand objects so that the clock hands sweep visually over the date text, exactly as on a physical watch dial. Changing creation order will put the date on top of the hands.

### Dot border: lv_line with 1px length instead of lv_obj
See `components/lvgl.md` for the explanation — the lv_line approach was the fix for LVGL's hidden minimum-size enforcement on `lv_obj`. The `dot_pts` array being static applies here for the same pointer-ownership reason as the hands.

### Radial gradient: 10 concentric lv_obj circles
LVGL v8 has no native radial gradient. The effect is approximated with 10 concentric circles ranging from `0x0D1428` (darkest, outermost) to `0x1F2647` (lightest, innermost). The step between each ring is small enough that no visible banding occurs on the display. The outer-to-inner draw order matters — later circles are drawn on top, so the darkest must be drawn first.

---

## WiFi and Config Portal — Non-Obvious Decisions

### Both STA and AP netif created at startup
```cpp
s_sta_netif = esp_netif_create_default_wifi_sta();
s_ap_netif  = esp_netif_create_default_wifi_ap();
```
Both network interfaces are created during `wifi_manager_init()` regardless of which mode is used. This allows live switching between STA and AP mode without tearing down and reinitialising the network stack. If only one were created, switching modes would require a restart.

### AP mode: live switch, no reboot, no credential erase
The original design erased NVS credentials before entering AP mode, which meant the config portal always started blank. This was wrong — it made re-configuration unnecessarily destructive.

The current design calls `wifi_manager_start_ap()` directly from the confirmation dialog callback. This switches the WiFi hardware to AP mode while leaving NVS untouched. The config portal reads `s_cfg` (already loaded at boot) and pre-populates all fields. Only the password field is left blank intentionally — blank = keep existing password.

Do not reintroduce credential erasing before AP mode. It was deliberately removed.

### Config portal: dynamic HTML with snprintf
The config page is not a static string — it is built with `snprintf` using `s_cfg` values to pre-populate the form fields. CSS `%` characters must be written as `%%` inside the format string or `snprintf` will misinterpret them as format specifiers.

The response buffer is heap-allocated (`malloc`) because a 3KB local variable on the httpd task stack causes a stack overflow panic. See `platforms/esp32-idf.md`.

### DEFAULT_UTC_OFFSET 2
Default UTC offset is +2, which is South Africa Standard Time (SAST/CAT). There is no DST in South Africa. This default only applies on first boot before a timezone is configured via the portal.

---

## Arduino Component — Historical Note

This project uses the Arduino component (`espressif/arduino-esp32`) via `idf_component.yml` and calls `initArduino()` in `app_main`. This was the first ESP32 project and Arduino was used as a stepping stone for the initial setup.

**Do not replicate this pattern in future projects.** All future ESP32 projects use pure ESP-IDF. The Arduino dependency requires FreeRTOS at 1000Hz (see menuconfig) and causes `Serial.println()` to conflict with the ESP-IDF console on ESP32-C3.

---

## Skill File References

The following patterns in this codebase are documented in skill files — see those files for detail rather than repeating the explanation here:

- **Colour byte swap for SPI displays** — `components/gc9a01.md`, `components/lvgl.md`
- **lv_conf.h ignored on ESP-IDF, use menuconfig** — `components/lvgl.md`
- **LVGL theme must remain enabled** — `components/lvgl.md`
- **lv_line trick for small circular indicators** — `components/lvgl.md`
- **Gesture bubble flag on sliders** — `components/cst816s.md`
- **lvgl_port_add_touch requires valid display handle** — `components/cst816s.md`
- **POSIX timezone sign inversion** — `platforms/esp32-idf.md`
- **HTTP handler heap allocation** — `platforms/esp32-idf.md`
- **NVS patterns (namespace, commit discipline)** — `platforms/esp32-idf.md`
- **Custom partition table** — `platforms/esp32-idf.md`
- **CMake Tools extension conflict** — `platforms/esp32-idf.md`
- **erase-flash bootloader recovery** — `platforms/esp32-idf.md`

---

*Created: April 2026. ESP-IDF 5.5.3, ESP32-C3-MINI-1U, LVGL v8, esp_lvgl_port v1.*
