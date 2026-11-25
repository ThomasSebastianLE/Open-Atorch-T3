# `shared/t3-bsp` — Shared Board Support Package (ATORCH / JUWEI T3)

This directory provides the shared hardware abstraction for [`projects/t3-meter-fw`](../../projects/t3-meter-fw/README.md) based on the static analysis in [`AGENTS.md`](../../AGENTS.md) and the resolved `BR35` SDK definitions.

## Structure

* [`include/t3_board.h`](include/t3_board.h) — Pin, I2C, GPADC, power-gate, and flash-slot constants for the T3 (`AC707N` / `BR35`).
* [`include/t3_bsp.h`](include/t3_bsp.h) & [`src/t3_bsp.c`](src/t3_bsp.c) — Hardware bring-up: soft `I2C1` (`PB02`/`PB01`) for `INA226`, `SC7A20`, `BMP280`, `ENS220`, vibration motor (`IO_MT_PG`), LCD power enable (`PA07`), USB UBOOT hook (`go_mask_usb_updata()`).
* [`include/t3_bringup_ui.h`](include/t3_bringup_ui.h) & [`src/t3_bringup_ui.c`](src/t3_bringup_ui.c) — LVGL v8 bring-up screen (`jl_gui_init()`) with live `INA226` telemetry, sensor probe badges, touch coordinates, `[BUZZ]`, and `[UBOOT]`.

## Hardware Overview (`AC707N` / `BR35`, `pi32v2`)

* **Display (`ST7789V`, 240×240 RGB565 IPS):**
  * Hardware `JL_DBI` (`0x00fd4320` / `0x00fe2228`), `RST = PC00`, `PA07` as output, init registers `MADCTL = 0x00`, `COLMOD = 0x05`, `INVON = 0x21`.
  * Backlight PWM: `IO_LCD_PG` (`0xE7`, `power_gate_pwm` @ `10 kHz`).
* **Touchscreen (`CST816D`):**
  * `I2C0` on `PA06` (SCL) / `PA05` (SDA) @ `100 kHz`, 7-bit address `0x15`, `RST = PA02`, `INT = IO_PORTB_08` (`PB08`).
* **Sensor Bus (`I2C1`):**
  * `PB02` (SCL) / `PB01` (SDA) for `INA226` (`0x40`), `SC7A20` (`0x19`), and `BMP280` (`0x77`) / `ENS220` (`0x20`).
* **Analog Channels (`JL_GPADC`) & Haptics:**
  * `PB00`: NTC thermistor (`AD_CH_IO_PB0 = 0x00100007`)
  * `PC02` / `PC03`: USB `D-` / `D+` (`0x00100009` / `0x0010000a`)
  * `PA10`: AC-coupled `VBUS` ripple (`ADC_CH_IO_PA10 = 0x00100004`)
  * Vibration motor: `IO_MT_PG` (`0xE8`, `power_gate_pwm` @ `10 kHz`)
* **Mandatory USB Reflashing Hook (Security / Recovery):**
  * Every custom firmware keeps the JieLi USB `user_setup` hook (`VID=0x0483`, `PID=0x5750`, `GET_STATUS`, `wValue=0x00FC`, `wIndex=0x00FE`) from `sdk/ac707n/SDK/apps/common/device/usb/device/user_setup.c` active so the T3 can always enter the ROM bootloader (`BR35UBOOT1.00`) via [`scripts/enter_uboot.py`](../../scripts/enter_uboot.py) (`--reboot`) or the `[UBOOT]` touch button without opening the case.
* **Slot Packaging (`SDFILE` Requirement):**
  * [`scripts/pack_t3_slot.py`](../../scripts/pack_t3_slot.py) automatically appends the `54,048` B (`0xd320`) OEM resource `SDFILE` directly after `app.bin` (`0x100 + app_len`) so `sdfile_init()` / `syscfg_tools_init()` does not block at boot.

## Live Bring-up Status (2026-11-24)

| Subsystem | Status | Observation on T3 Hardware |
| :--- | :--- | :--- |
| **`ST7789V` (240×240 IPS)** | Verified | Renders `T3 BSP BRING-UP AC707N` cleanly via `JL_DBI` + `IO_LCD_PG` |
| **`CST816D` (Touch)** | Verified | `TP: X=... Y=...` updates live on touch |
| **`INA226` (`0x40`)** | Verified | `INA:OK`, `V:`/`I:`/`P:` readings accurate |
| **`SC7A20` (`0x19`)** | Verified | `SC7:OK` on `I2C1` (`PB02`/`PB01`) |
| **`ENS220` (`0x20`) / `BMP280`** | Verified | `ENS:OK`, `BMP:--` → test board uses `ENS220` |
| **`IO_MT_PG` (Vibration Motor)** | Retest pending | No vibration at `80 ms, 65%`; increased to `200 ms, 100%` in `t3_bringup_ui.c` (built, not yet flashed) |
| **USB Slave (`0483:5750`)** | Retest pending | Added `TCFG_OTG_MODE = OTG_SLAVE_MODE` in `scripts/apply_t3_bsp.py` (built, not yet flashed) |
