# Open-Atorch-T3 — Custom Firmware & Tools (JieLi AC707N / BR35)

🌐 **Project Website & Installation Guide:** https://thomassebastianle.github.io/Open-Atorch-T3/  
📦 **GitHub Repository:** https://github.com/ThomasSebastianLE/Open-Atorch-T3

This repository contains the hardware/firmware reverse-engineering documentation of the **ATORCH / JUWEI T3** (`AGENTS.md`), a reproducible **Docker-based Linux build environment** for the `pi32v2` architecture of the **JieLi AC707N**, the shared Board Support Package (`shared/t3-bsp`), and the open-source multimeter firmware (`projects/t3-meter-fw`).

> [!CAUTION]
> **Protect Stock Firmware & Factory Calibration:**
> - **Never** overwrite or modify [`T3 Firmware Version-UP 2.0.4.bin`](T3%20Firmware%20Version-UP%202.0.4.bin) locally.
> - Before flashing any custom firmware for the first time, **always pull a full 2 MB flash backup** (including `VM` calibration, `BTIF`, `EXIF`, `key_mac`) using `tools/jl-uboot-tool`!

---

## Included Components

* **Docker Builder (`Dockerfile`, `docker-compose.yml`):**
  * `ubuntu:22.04` (`linux/amd64`) with 32-bit multiarch libs (`libc6:i386`, `libstdc++6:i386`, `zlib1g:i386`).
  * Official JieLi **`pi32v2` LLVM/Clang toolchain** at `/opt/jieli/pi32v2/bin`.
  * JieLi Linux **post-build tools** (`isd_download`, `ufw_maker`, `fw_add`, `packres`, `json_to_res`, `fat_comm`, `remove_tailing_zeros`) at `/opt/jieli/postbuild` and `sdk/ac707n/SDK/tools/linux/`.
  * Configured file descriptor limit `ulimit -n 65536` (required for `pi32v2` LTO linking).
* **SDK & Tools:**
  * `sdk/ac707n` → `e_badge_707_sdk_200` (JieLi AC707N / BR35 SDK with LVGL, downloaded locally via [`scripts/setup-sdk.ps1`](scripts/setup-sdk.ps1))
  * `tools/jl-uboot-tool` → Git submodule for USB bootloader flashing/dumping with BR35/AC707N support
* **PowerShell & Python Scripts:**
  * [`scripts/setup-sdk.ps1`](scripts/setup-sdk.ps1) — Idempotent download & extraction of the AC707N SDK to `sdk/ac707n`
  * [`scripts/apply_t3_bsp.py`](scripts/apply_t3_bsp.py) — Idempotent SDK overlay for `shared/t3-bsp` (creates `.t3orig` backups)
  * [`scripts/build.ps1`](scripts/build.ps1) — Build via Docker container (`-Project`, `-Clean`, `-Jobs`, `-MakeArgs`)
  * [`scripts/pack_t3_slot.py`](scripts/pack_t3_slot.py) — Packs `app.bin` + OEM resource `SDFILE` (`0xd320` B) into the scrambled `0x5000` slot (`--self-test`)
  * [`scripts/flash-slot.ps1`](scripts/flash-slot.ps1) — Flashes a packed `0x5000` slot via `tools/jl-uboot-tool/jluboottool.py --chip br35` with readback verification
  * [`scripts/atorch_iap_flash.py`](scripts/atorch_iap_flash.py) — Python flasher for the built-in USB-HID IAP bootloader (`VID=0x0483`, `PID=0x5750`, `libusbk`)
  * [`scripts/shell.ps1`](scripts/shell.ps1) — Interactive Linux shell inside the container

---

## Usage

### 1. Provision the SDK (one-time / if `sdk/ac707n` is missing)
```powershell
.\scripts\setup-sdk.ps1
```

### 2. Pull a Full Device Backup (mandatory before first flash!)
```powershell
# Passive test to check if USB EP0 UBOOT trigger (0483:5750) is reachable
.\.venv\Scripts\python.exe .\scripts\enter_uboot.py --probe

# Pull full 2 MB backup (run as Administrator)
.\scripts\backup-device.ps1
```
See [`backups/README.md`](backups/README.md) for details.

### 3. Build, Pack & Flash Custom Firmware
```powershell
# 1. Apply SDK overlay and compile multimeter firmware
.\scripts\build.ps1 -Project meter

# 2. Flash packed 0x5000 slot (in BR35UBOOT1.00 mode)
.\scripts\flash-slot.ps1 -SlotPath .\firmware-builds\t3-meter-fw-slot.bin
```

### 4. Interactive Build Shell
```powershell
.\scripts\shell.ps1
```
Inside the container, `clang` (`pi32v2`), `make`, `isd_download`, and `ufw_maker` are available directly on `PATH` under `/workspace`.

* **Recovery Paths (unbrickable at any time):**
  1. **JieLi ROM Bootloader (`BR35UBOOT1.00`):** Via `.\.venv\Scripts\python.exe .\scripts\enter_uboot.py --reboot` or the `[UBOOT]` UI button.
  2. **Hardware Recovery (USB-HID IAP `ATORCH T3 V1.0.1`):** Hold both physical buttons while plugging in the USB cable, then restore e.g. the stock image with [`scripts/atorch_iap_flash.py`](scripts/atorch_iap_flash.py):
     ```powershell
     .\.venv\Scripts\python.exe .\scripts\atorch_iap_flash.py "T3 Firmware Version-UP 2.0.4.bin"
     ```

---

## Project Structure

* [`backups/`](backups/README.md) — Full 2 MB flash dumps of your device (excluded in `.gitignore`)
* [`shared/t3-bsp/`](shared/t3-bsp/README.md) — Shared hardware abstraction (`ST7789V`, `CST816D`, `SC7A20`, PWM backlight/motor, USB UBOOT hook)
* [`projects/t3-meter-fw/`](projects/t3-meter-fw/README.md) — Enhanced USB-C power meter firmware (`INA226`, `BMP280`/`ENS220`, ripple FFT, LVGL, BLE/USB) + WebBLE companion app (`projects/t3-meter-fw/webble/`)
* [`docs/index.html`](docs/index.html) — GitHub Pages project website with feature/test matrix and setup guide

---

## License

* **Original Source Code (`shared/t3-bsp/`, `projects/t3-meter-fw/`, `scripts/`, `Dockerfile`, `docs/`, documentation):** Licensed under the **GNU General Public License v3.0** (see [`LICENSE`](LICENSE)).
  * **JieLi SDK Linking Exception:** As an additional permission under Section 7 of the GPLv3, you are permitted to compile and link this work with the proprietary libraries and headers of the locally obtained JieLi AC707N / BR35 SDK (`sdk/ac707n/`), and distribute the resulting firmware binaries, provided all original and derivative GPLv3 components remain under the GPLv3.
* **Submodule `tools/jl-uboot-tool/`:** Licensed under the **MIT License** (see [`tools/jl-uboot-tool/LICENSE`](tools/jl-uboot-tool/LICENSE)).
