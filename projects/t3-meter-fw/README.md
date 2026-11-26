# `t3-meter-fw` — Enhanced ATORCH T3 Power Meter Firmware

An open, clean, fast, and feature-rich power meter firmware for the ATORCH / JUWEI T3 built on `sdk/ac707n` and [`shared/t3-bsp`](../../shared/t3-bsp/README.md).

## Implemented Features (v1.1)

* **6-Tab LVGL v8 UI (`LIVE`, `CURVE`, `RIPPLE`, `CABLE`, `PROTO`, `SETUP`) with 2-button (`PB07` right + `PA11` left) & `SC7A20` tilt navigation** *(touch pointer input is disabled by default pending retest on an untouched CST816 panel; the `PB08` falling-edge IRQ driver in `shared/t3-bsp` is ready and SDK `cst816d_upgrade()` is permanently disabled)*:
  * **MTR (Live Meter):** `VBUS`, bidirectional `IBUS`, power `W`, separate `IN / OUT` counters (`+mAh / -mAh`), `3.7 V` powerbank cell capacity, `Wh`, peak values (`Pmax`, `Ipeak`), runtime, **3 persistent memory banks (`BANK A/B/C`)**, and **Big-Numbers mode** by tapping the measurement area.
  * **CRV (Real-Time Curve):** `lv_chart` for `V` & `I`, `Vmin/Vmax`, `Ipeak`, and **brownout counter** (`VBUS < 4.50 V` under load).
  * **RPL (AC Ripple Oscilloscope):** `PA10` GPADC capture, `Vpp`, moving average, dominant switching frequency (`Hz`/`kHz`), and **PSU quality rating** (`VERY CLEAN < 30 mVpp` / `OK` / `HIGH NOISE >= 80 mVpp`).
  * **CBL (Cable & Droop Test):** 2-step measurement of `V0` (no load) → `R_cable` (`mΩ`), **voltage droop (`mV` & `%`)**, and quality rating.
  * **SNS (Fast-Charge Monitor, Barometer & Level):** Passive fast-charge detection via `D+`/`D-` + `VBUS` (`USB 5V`, `DCP 1.5A`, `Apple 1/2A / 2.4A`, `Samsung AFC/FCP`, `QC 2.0/3.0`, `PD/QC 9V/12V/15V/20V`), `BMP280`/`ENS220` pressure & altitude (`ALT = 0m`), `SC7A20` spirit level + auto-rotation.
  * **SET (Config Menu & Calibration):** Brightness (`10..100 %`), haptics (`ON/OFF`), auto-rotation (`ON/OFF`), **`BLE Beacon: ON/OFF`**, **alerts (`ON/OFF`)**, `V`/`I` gain calibration, `ZERO I`, `SAVE VM` (persistent storage in `VM` via `syscfg_write`), and `UBOOT` trigger (hold 1 s).
* **Alerts with Haptics & Warning Banner:**
  * Charge end (`< 50 mA` after prior load), overvoltage (`> 21 V`), overcurrent (`> 5 A`), and overtemperature (`> 60 °C`).
* **Offline Blackbox Ring Buffer:**
  * Stores `(V, I, W, °C)` every 10 s in RAM (60 entries = 10 min).
* **Bluetooth / BLE GATT & WebBLE App:**
  * Native RCSP BLE GATT (`T3_MTR`, ATorch-compatible service `0xFFE0` with `0xFFE1` Notify/RX and `0xFFE2` RX, 36-byte telemetry frames every 1 s) — verified live in Chrome WebBLE.
  * See [`docs/BLE_APP_PROTOCOL.md`](docs/BLE_APP_PROTOCOL.md) for the full app protocol spec and [`webble/index.html`](webble/index.html) for the browser dashboard.

## Build & Flash

```powershell
.\scripts\build.ps1 -Project meter
.\scripts\flash-slot.ps1 -SlotPath .\firmware-builds\t3-meter-fw-slot.bin
```
