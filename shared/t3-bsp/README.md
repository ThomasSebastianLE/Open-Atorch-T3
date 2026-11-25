# `shared/t3-bsp` — Gemeinsames Board-Support-Package (ATORCH / JUWEI T3)

Dieses Verzeichnis bildet die gemeinsame Hardware-Abstraktion für **beide Teilprojekte** ([`projects/t3-meter-fw`](../../projects/t3-meter-fw/README.md) und [`projects/t3-doom`](../../projects/t3-doom/README.md)) auf Basis der statischen Analyse in [`AGENTS.md`](../../AGENTS.md) und der aufgelösten `BR35`-SDK-Definitionen.

## Struktur

* [`include/t3_board.h`](include/t3_board.h) — Pin-, I2C-, GPADC-, Power-Gate- und Flash-Slot-Konstanten des T3 (`AC707N` / `BR35`).
* [`include/t3_bsp.h`](include/t3_bsp.h) & [`src/t3_bsp.c`](src/t3_bsp.c) — Hardware-Bring-up: Soft-`I2C1` (`PB02`/`PB01`) für `INA226`, `SC7A20`, `BMP280`, `ENS220`, Vibrationsmotor (`IO_MT_PG`), LCD-Power-Enable (`PA07`), USB-UBOOT-Hook (`go_mask_usb_updata()`).
* [`include/t3_bringup_ui.h`](include/t3_bringup_ui.h) & [`src/t3_bringup_ui.c`](src/t3_bringup_ui.c) — LVGL-v8 Bring-up-Screen (`jl_gui_init()`) mit Live-`INA226`-Telemetrie, Sensor-Probe-Badges, Touch-Koordinaten, `[BUZZ]` und `[UBOOT]`.

## Hardware-Übersicht (`AC707N` / `BR35`, `pi32v2`)

* **Display (`ST7789V`, 240×240 RGB565 IPS):**
  * Hardware-`JL_DBI` (`0x00fd4320` / `0x00fe2228`), `RST = PC00`, `PA07` als Ausgang, Init-Register `MADCTL = 0x00`, `COLMOD = 0x05`, `INVON = 0x21`.
  * Backlight-PWM: `IO_LCD_PG` (`0xE7`, `power_gate_pwm` @ `10 kHz`).
* **Touchscreen (`CST816D`):**
  * `I2C0` auf `PA06` (SCL) / `PA05` (SDA) @ `100 kHz`, 7-Bit-Adresse `0x15`, `RST = PA02`, `INT = IO_PORTB_08` (`PB08`).
* **Sensor-Bus (`I2C1`):**
  * `PB02` (SCL) / `PB01` (SDA) für `INA226` (`0x40`), `SC7A20` (`0x19`) und `BMP280` (`0x77`) / `ENS220` (`0x20`).
* **Analoge Messkanäle (`JL_GPADC`) & Haptik:**
  * `PB00`: NTC-Thermistor (`AD_CH_IO_PB0 = 0x00100007`)
  * `PC02` / `PC03`: USB `D-` / `D+` (`0x00100009` / `0x0010000a`)
  * `PA10`: AC-gekoppeltes `VBUS`-Ripple (`ADC_CH_IO_PA10 = 0x00100004`)
  * Vibrationsmotor: `IO_MT_PG` (`0xE8`, `power_gate_pwm` @ `10 kHz`)
* **Pflicht-Hook für USB-Reflashing (Security / Recovery):**
  * Jede eigene Firmware hält den JieLi-USB-`user_setup`-Hook (`VID=0x0483`, `PID=0x5750`, `GET_STATUS`, `wValue=0x00FC`, `wIndex=0x00FE`) aus [`sdk/ac707n/SDK/apps/common/device/usb/device/user_setup.c`](../../sdk/ac707n/SDK/apps/common/device/usb/device/user_setup.c) aktiv, damit der T3 jederzeit ohne Öffnen per [`scripts/enter_uboot.py`](../../scripts/enter_uboot.py) (`--reboot`) oder über den `[UBOOT]`-Touch-Button in den ROM-Bootloader (`BR35UBOOT1.00`) versetzt werden kann.
* **Slot-Packaging (`SDFILE`-Pflicht):**
  * [`scripts/pack_t3_slot.py`](../../scripts/pack_t3_slot.py) hängt automatisch das `54.048` B (`0xd320`) große OEM-Ressourcen-`SDFILE` direkt an `app.bin` (`0x100 + app_len`) an, damit `sdfile_init()` / `syscfg_tools_init()` beim Start nicht blockiert.

## Live-Bring-up-Status (24.11.2026)

| Subsystem | Status | Beobachtung am T3-Gerät |
| :--- | :--- | :--- |
| **`ST7789V` (240×240 IPS)** | Verifiziert | Rendert `T3 BSP BRING-UP AC707N` sauber über `JL_DBI` + `IO_LCD_PG` |
| **`CST816D` (Touch)** | Verifiziert | `TP: X=... Y=...` aktualisiert live bei Berührung |
| **`INA226` (`0x40`)** | Verifiziert | `INA:OK`, `V:`/`I:`/`P:`-Werte stimmen |
| **`SC7A20` (`0x19`)** | Verifiziert | `SC7:OK` auf `I2C1` (`PB02`/`PB01`) |
| **`ENS220` (`0x20`) / `BMP280`** | Verifiziert | `ENS:OK`, `BMP:--` → Board nutzt `ENS220` |
| **`IO_MT_PG` (Vibrationsmotor)** | Nachtest offen | Bei `80 ms, 65%` keine Vibration; in `t3_bringup_ui.c` auf `200 ms, 100%` erhöht (gebaut, noch nicht geflasht) |
| **USB Slave (`0483:5750`)** | Nachtest offen | `TCFG_OTG_MODE = OTG_SLAVE_MODE` in `scripts/apply_t3_bsp.py` ergänzt (gebaut, noch nicht geflasht) |
