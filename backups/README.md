# Geräte-Backups (`backups/`)

Hier liegen **gerätespezifische 2-MB-Full-Flash-Dumps** deines physischen ATORCH / JUWEI T3 (inkl. Werkskalibrierung `VM`, `BTIF`, `EXIF` und `key_mac`).

> [!CAUTION]
> - `*.bin`, `*.sha256`, `*.fw` und `*.ufw` in diesem Ordner sind per `.gitignore` ausgeschlossen — **niemals** ein persönliches Flash-Backup öffentlich committen!
> - Vor dem ersten Flashen einer eigenen Firmware zwingend ein vollständiges Backup ziehen.

## Backup erstellen (PowerShell als Administrator)

1. Prüfe zunächst im normalen Betrieb, ob USB-Zugriff ohne `hidusb`-Blockade möglich ist:
   ```powershell
   .\.venv\Scripts\python.exe .\scripts\enter_uboot.py --probe
   ```
   *(Falls `hidusb` blockiert: mit Zadig temporär `WinUSB` für `ATORCH T3 V2.0.4` `0483:5750` installieren.)*

2. Ziehe das vollständige 2-MB-Backup (`0x200000` Bytes):
   ```powershell
   .\scripts\backup-device.ps1
   ```

## Backup wiederherstellen (Recovery über USB-UBOOT)

Falls ein Flash fehlschlägt oder du exakt auf die Stock-Firmware zurück willst:
```powershell
cd tools\jl-uboot-tool
..\..\.venv\Scripts\python.exe jluboottool.py --chip br35 "write 0x0 ..\..\backups\DEIN-DUMP.bin" "reset"
```
