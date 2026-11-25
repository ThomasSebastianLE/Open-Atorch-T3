# Open-Atorch-T3 — Custom Firmware & Tools (JieLi AC707N / BR35)

🌐 **Projekt-Webseite & Installationsanleitung:** https://thomassebastianle.github.io/Open-Atorch-T3/  
📦 **GitHub-Repository:** https://github.com/ThomasSebastianLE/Open-Atorch-T3

Dieser Workspace enthält die Hardware-/Firmware-Analyse des **ATORCH / JUWEI T3** (`AGENTS.md`) sowie eine reproduzierbare **Docker-basierte Linux-Build-Umgebung** für die `pi32v2`-Architektur des **JieLi AC707N**.

> [!CAUTION]
> **Originale Firmware & Werkskalibrierung schützen:**
> - [`T3 Firmware Version-UP 2.0.4.bin`](T3%20Firmware%20Version-UP%202.0.4.bin) **niemals** überschreiben oder verändern.
> - Vor dem ersten eigenen Flash-Vorgang **unbedingt ein vollständiges 2-MB-Flash-Backup** (inkl. `VM`-Kalibrierung, `BTIF`, `EXIF`, `key_mac`) mit `tools/jl-uboot-tool` ziehen!

---

## Enthaltene Komponenten

* **Docker-Builder (`Dockerfile`, `docker-compose.yml`):**
  * `ubuntu:22.04` (`linux/amd64`) inkl. 32-Bit-Multiarch-Libs (`libc6:i386`, `libstdc++6:i386`, `zlib1g:i386`).
  * Offizielle JieLi **`pi32v2` LLVM/Clang-Toolchain** unter `/opt/jieli/pi32v2/bin`.
  * JieLi Linux **Post-Build-Tools** (`isd_download`, `ufw_maker`, `fw_add`, `packres`, `json_to_res`, `fat_comm`, `remove_tailing_zeros`) unter `/opt/jieli/postbuild` sowie in `sdk/ac707n/SDK/tools/linux/`.
  * Konfiguriertes File-Descriptor-Limit `ulimit -n 65536` (zwingend erforderlich für `pi32v2` LTO-Linking).
* **SDK & Tools:**
  * `sdk/ac707n` → `e_badge_707_sdk_200` (JieLi AC707N / BR35 SDK mit LVGL, geladen über [`scripts/setup-sdk.ps1`](scripts/setup-sdk.ps1))
  * `tools/jl-uboot-tool` → Git-Submodule für USB-Bootloader-Flasher/Dumper mit BR35/AC707N-Support
* **PowerShell- & Python-Skripte:**
  * [`scripts/setup-sdk.ps1`](scripts/setup-sdk.ps1) — Idempotenter Download & Entpacken des AC707N-SDK nach `sdk/ac707n`
  * [`scripts/apply_t3_bsp.py`](scripts/apply_t3_bsp.py) — Idempotentes SDK-Overlay für `shared/t3-bsp` (legt `.t3orig`-Backups an)
  * [`scripts/build.ps1`](scripts/build.ps1) — Kompilierung über den Docker-Container (`-Clean`, `-Jobs`, `-MakeArgs`)
  * [`scripts/pack_t3_slot.py`](scripts/pack_t3_slot.py) — Verpackt `app.bin` + OEM-Ressourcen-`SDFILE` (`0xd320` B) in den gescrambelten `0x5000`-Slot (`--self-test`)
  * [`scripts/flash-slot.ps1`](scripts/flash-slot.ps1) — Flasht einen gepackten `0x5000`-Slot über `tools/jl-uboot-tool/jluboottool.py --chip br35` inkl. Rücklese-Prüfung
  * [`scripts/atorch_iap_flash.py`](scripts/atorch_iap_flash.py) — Python-Flasher für den eingebauten USB-HID-IAP-Bootloader (`VID=0x0483`, `PID=0x5750`, `libusbk`)
  * [`scripts/shell.ps1`](scripts/shell.ps1) — Interaktive Linux-Shell im Container

---

## Nutzung

### 1. SDK bereitstellen (einmalig / falls `sdk/ac707n` fehlt)
```powershell
.\scripts\setup-sdk.ps1
```

### 2. Build ausführen
```powershell
# Standard-Build mit allen CPU-Kernen
.\scripts\build.ps1

# Sauberer Rebuild
.\scripts\build.ps1 -Clean
```

### 3. Interaktive Build-Shell öffnen
```powershell
.\scripts\shell.ps1
```
Im Container stehen u. a. `clang` (`pi32v2`), `make`, `isd_download` und `ufw_maker` direkt im `PATH` unter `/workspace`.

### 4. Vollständiges Device-Backup sichern (vor jedem eigenen Flash!)
```powershell
# Passiver Test, ob USB-EP0-UBOOT-Trigger (0483:5750) erreichbar ist
.\.venv\Scripts\python.exe .\scripts\enter_uboot.py --probe

# Vollständiges 2-MB-Backup ziehen (als Administrator ausführen)
.\scripts\backup-device.ps1
```
Details dazu stehen in [`backups/README.md`](backups/README.md).

### 5. Custom-Firmware bauen, verpacken & flashen / recovern
```powershell
# 1. SDK-Overlay anwenden und kompilieren
.\scripts\build.ps1

# 2. Gepackten 0x5000-Slot flashen (im BR35UBOOT1.00-Modus)
.\scripts\flash-slot.ps1 -SlotBin .\firmware-builds\t3-bsp-bringup-slot.bin
```

* **Recovery-Wege (jederzeit unbrickable):**
  1. **JieLi ROM-Bootloader (`BR35UBOOT1.00`):** Via `.\.venv\Scripts\python.exe .\scripts\enter_uboot.py --reboot` oder Touch-Button `[UBOOT]` im Bring-up-UI.
  2. **Hardware-Recovery (USB-HID-IAP `ATORCH T3 V1.0.1`):** Beide physischen Taster beim Einstecken des USB-Kabels gedrückt halten und mit [`scripts/atorch_iap_flash.py`](scripts/atorch_iap_flash.py) z. B. das Original-Image wiederherstellen:
     ```powershell
     .\.venv\Scripts\python.exe .\scripts\atorch_iap_flash.py "T3 Firmware Version-UP 2.0.4.bin"
     ```

### 6. Git-Identität setzen & ersten Commit anlegen
Falls noch keine globale Git-Identität gesetzt ist:
```powershell
git config --global user.name "Dein Name"
git config --global user.email "dein@email.de"
git add .
git commit -m "Initial T3 AC707N build environment"
```

---

## Projektstruktur

* [`backups/`](backups/README.md) — Vollständige 2-MB-Flash-Dumps des eigenen Geräts (in `.gitignore` ausgeschlossen)
* [`shared/t3-bsp/`](shared/t3-bsp/README.md) — Gemeinsame Hardware-Abstraktion (`ST7789V`, `CST816D`, `SC7A20`, PWM-Backlight/Motor, USB-UBOOT-Hook)
* [`projects/t3-meter-fw/`](projects/t3-meter-fw/README.md) — Verbesserte USB-C-Power-Meter-Firmware (`INA226`, `BMP280`/`ENS220`, Ripple-FFT, LVGL, BLE/USB) + WebBLE-Companion-App (`projects/t3-meter-fw/webble/`)

---

## Lizenz

* **Eigener Quellcode (`shared/t3-bsp/`, `projects/t3-meter-fw/`, `scripts/`, `Dockerfile`, Dokumentation):** Steht unter der **GNU General Public License v3.0** (siehe [`LICENSE`](LICENSE)).
  * **JieLi-SDK Linking-Exception:** Als zusätzliche Erlaubnis nach Abschnitt 7 der GPLv3 darf dieses Werk mit den proprietären Bibliotheken und Headern des lokal bezogenen JieLi AC707N / BR35 SDK (`sdk/ac707n/`) kompiliert und verlinkt sowie in dieser Form als Firmware-Binary weitergegeben werden, solange alle eigenen bzw. abgeleiteten GPLv3-Bestandteile weiterhin unter der GPLv3 stehen.
* **Submodul `tools/jl-uboot-tool/`:** Steht unter der **MIT-Lizenz** (siehe [`tools/jl-uboot-tool/LICENSE`](tools/jl-uboot-tool/LICENSE)).
* **Nicht im Repository enthalten:** Das JieLi-SDK (`sdk/ac707n/`) sowie die proprietäre Original-Firmware (`T3 Firmware Version-UP 2.0.4.bin`) sind über `.gitignore` ausgeschlossen und werden nicht mitverteilt.
