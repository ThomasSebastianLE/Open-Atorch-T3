# Device Backups (`backups/`)

This directory stores **device-specific 2 MB full-flash dumps** of your physical ATORCH / JUWEI T3 (including factory calibration `VM`, `BTIF`, `EXIF`, and `key_mac`).

> [!CAUTION]
> - `*.bin`, `*.sha256`, `*.fw`, and `*.ufw` in this folder are excluded via `.gitignore` — **never** commit a personal flash backup publicly!
> - Always pull a full backup before flashing custom firmware for the first time.

## Create a Backup (PowerShell as Administrator)

1. First check in normal operation whether USB access works without `hidusb` blocking:
   ```powershell
   .\.venv\Scripts\python.exe .\scripts\enter_uboot.py --probe
   ```
   *(If `hidusb` blocks access: temporarily install `WinUSB` for `ATORCH T3 V2.0.4` `0483:5750` using Zadig.)*

2. Pull the full 2 MB backup (`0x200000` bytes):
   ```powershell
   .\scripts\backup-device.ps1
   ```

## Restore a Backup (Recovery via USB-UBOOT)

If a flash fails or you want to return to the exact stock state:
```powershell
cd tools\jl-uboot-tool
..\..\.venv\Scripts\python.exe jluboottool.py --chip br35 "write 0x0 ..\..\backups\YOUR-DUMP.bin" "reset"
```
