# AGENTS.md — ATORCH / JUWEI T3 USB-C Power Meter Firmware Analysis

Dieses Dokument fasst die vollständige statische Reverse-Engineering-Analyse der Firmware [`T3 Firmware Version-UP 2.0.4.bin`](file:///c:/Users/thoma/Documents/Antigravity-Projekte/T3-USB-Firmware/T3%20Firmware%20Version-UP%202.0.4.bin) zusammen und dient als technische Referenz sowie als Projektkontext für nachfolgende Agent-Sessions.

---

## 1. Projekt- & Geräte-Überblick

* **Analysierte Datei:** `T3 Firmware Version-UP 2.0.4.bin`
  * **Größe:** `1.277.952` Bytes (`0x138000`)
  * **SHA-256:** `d5d1a7ea095b9e7c5e560469b09fa0557eb10986dd2858a27fd83219636250c6`
  * **Entropie:** `7,9927` (durchgehend gescrambled bis auf eine Klartext-Lücke bei `0x4000..0x5000`; REA `inspect_byte_entropy` Evidence-IDs `ev_391c51ef7ce625caf730c464e90d2968040e79cd97e4bdcae686cbd2a95576b3` / `ev_8cf850d4debbfe598d4897e2eb85cec56e8a67aac90d84baa2b7e35de62e93ef`)
* **Gerät:** **ATORCH / JUWEI T3** USB-C-Power-Meter mit 1,54"-IPS-Touchscreen (Strings in `app.bin`: `T3 V2.0.4`, `ATORCH`, `Power by JUWEI`, `t3_app`, `T3_BLE`, `T3_SPP`).
* **Haupt-SoC:** **Zhuhai JieLi (珠海杰理) `AC707N`** (Chip-Familie **`BR35`**; in Hardware-Teardowns als `AC7076A6` bestückt; Flash-Header bei `0x0010`: `AC707N`, `cfg_tool.bin`: `AC707N-demo` / `AC707N_watch`).
* **CPU & Subsysteme:**
  * Proprietärer 32-Bit RISC/DSP-Kern **`pi32v2`** (Blackfin-verwandte ISA mit variabler 16-/32-/48-Bit-Befehlslänge, bis `288 MHz`).
  * **2D-GPU** (`jlgpu`), genutzt von **LVGL v8** (`LV_USE_GPU2D_JL`).
  * **Low-Power-Coprozessor `P11`** (eigene Firmware `p11_code.bin`, `10.592` Bytes bei Flash-Offset `0x133e5e`).
  * **Bluetooth 5.4 Dual-Mode:** BLE (`T3_BLE`, inkl. `JLOTA`/RCSP) + Classic SPP (`T3_SPP`).
  * **Flash-Speicher:** **2 MB (16 Mbit) SPI NOR Flash** (`0x000000..0x200000`), DTR-Modus aktiv (`NORFLASH_DTR_EN=1`, `NORFLASH_WPS_EN=0`).

---

## 2. Flash-Layout, Entscrambling & Memory-Map

### A. Flash-Partitionierung (Root-SDFILE `0x0020` & `app_dir_head` `0x5000`)

| Bereich / Datei | Flash-Offset | Größe | Inhalt / Funktion |
| :--- | :--- | :--- | :--- |
| Root-Header & Verzeichnis | `0x000000` | `0x00C0` | Chip-ID `AC707N`, Format `0.01`, SDFILE-Tabelle (`0x20..0xc0`) |
| `uboot.boot` | `0x0000C0` | 17.904 B | Second-Stage-Bootloader (SRAM-Ladeadresse `0x00102600`, `dcrc = 0x75a6`, Header-CRC `0x3a67`) |
| `isd_config.ini` | `0x0046B0` | 135 B | Klartext-Config: `SPI=4_3_1_0`, `RESET=PB07_08_0`, `NORFLASH_DTR_EN=1`, `NORFLASH_WPS_EN=0` |
| `otp_cfg` | `0x004737` | 256 B | **Komplett `0xFF`** (keine OTP-Key-Konfiguration gesetzt) |
| `app_dir_head` + `app.bin` | `0x005000` | 1.203.424 B | `app_dir_head` (`0x5000`, 256 B) + `app.bin` (`0x5100`, `1.203.168` B, gemappt nach VA `0x0c000100`, `dcrc = 0x4262`) |
| Ressourcen-SDFILE | `0x12ACE0` | ~54 KB | `tone_en`, `tone_zh`, `cfg_tool.bin`, `p11_code.bin`, `stream.bin`, `config.dat` |
| `VM` *(nicht in `.bin`)* | `0x138000` | 761.856 B | Nichtflüchtiger Speicher (Nutzer-Einstellungen, Kalibrierung, Zähler) |
| `BTIF` / `EXIF` / `DATA` / `key_mac` *(nicht in `.bin`)* | `0x1F2000..0x200000` | 56 KB | BT-Pairing, Geräte-Info, MAC-Adresse |

### B. Verlustfreies Entscrambling (JieLi Galois-LFSR `poly = 0x1021`)

* **Root-SDFILE (`0x20..0xc0`) und `uboot.boot`:** JieLi-Werksschlüssel `0xFFFF`.
* **`app_dir_head` + `app.bin` (`0x5000..Ende`):** 32-Byte-Blöcke mit `key = (0x8d07 ^ ((off - 0x5000) >> 2)) & 0xFFFF`.
* Alle Integritätsprüfungen wurden über CRC-16/XMODEM exakt verifiziert (`uboot.boot`: `dcrc = 0x75a6`, `app.bin`: `dcrc = 0x4262`). Für den Container-Eintrag `app_area_head` (`entry[0]`) berechnet sich `dcrc` exakt über `crc16_xmodem(full[0x20 : 0x100 + app_len])` (`0x4810` bei der `.bin`-Datei, `0xc516` im Live-Dump).
* **Live-Device-Verifikation (24.11.2026):** Zwei bit-identische 2-MB-Full-Dumps (`backups/t3-oem-dump-20261124-140322.bin`, SHA-256 `6d32ad3885a880888b682494156db6c9da2f8bcd8306f40275396076d8df1099`) bestätigen, dass kein benutzerdefinierter OTP-Chipkey aktiv ist (`0x5000..` entscrambled direkt mit `0x8d07`), und der USB-ROM-Bootloader-Trigger (`VID=0x0483`, `PID=0x5750`, `GET_STATUS wValue=0x00FC wIndex=0x00FE` via `scripts/enter_uboot.py --probe`) auf der Serienfirmware aktiv ist.
* **Hardware-Recovery-Tastenkombi (verifiziert 24.11.2026):** Beide physischen Taster beim Einstecken des USB-Kabels gleichzeitig gedrückt halten startet zuverlässig den fest verbauten **USB-HID-IAP-Bootloader** (Display: `"product updating"` / `"chann usb hid"` / `Model t3`, USB `VID=0x0483`, `PID=0x5750`, Hersteller `IAP`, Produkt `ATORCH T3 V1.0.1`, Serial `987672`, `IF 0` HID `EP 0x82 IN / 0x02 OUT`, 64 B) — unabhängig vom `0x5000`-App-Slot.
  * **Protokoll (aus `DcTester.exe` reverse-engineert & mit [`scripts/atorch_iap_flash.py`](file:///c:/Users/thoma/Documents/Antigravity-Projekte/T3-USB-Firmware/scripts/atorch_iap_flash.py) live verifiziert):** 64-Byte-Interrupt-Reports mit Report-ID `0x55` und CRC-16/MODBUS (`poly=0xA001`, `init=0xFFFF`, Little-Endian) über `pkt[0 : pkt_len - 2]`. Start-Handshake `0xAA, 0x06, total_chunks_lo, total_chunks_hi, crc_lo, crc_hi` → ACK `0xBB`; Datenpakete `0xCC, 6 + len(chunk), idx_lo, idx_hi, chunk (<=58 B), crc_lo, crc_hi` → ACK `0xDD, ..., idx_lo, idx_hi`; Abschluss `0xEE` (Schreiben/Prüfen) → `0xFF` (Erfolg + Reboot) bzw. `0x11` (Fehler).
* **OEM-Ressourcen-`SDFILE`-Pflicht im `0x5000`-Slot:** Direkt nach `app.bin` (ab `0x100 + app_len` im entscrambelten Slot) **muss** das `54.048` B (`0xd320`) große OEM-Ressourcen-`SDFILE` (`EXT_RESERVED`, `DATA`, `tone_en`, `cfg_tool.bin`, `p11_code.bin`, `stream.bin`, `config.dat`) liegen; fehlt es, hängt `sdfile_init()` / `syscfg_tools_init()` beim Boot vor Display- und USB-Init. Dies wird durch [`scripts/pack_t3_slot.py`](file:///c:/Users/thoma/Documents/Antigravity-Projekte/T3-USB-Firmware/scripts/pack_t3_slot.py) automatisch eingebettet.

```python
def jl_lfsr_crypt(data: bytes, key: int) -> bytes:
    s = key & 0xFFFF
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ (s & 0xFF)
        s = (((s << 1) ^ 0x1021) & 0xFFFF) if (s & 0x8000) else ((s << 1) & 0xFFFF)
    return bytes(out)

def descramble_app(raw: bytes) -> bytes:
    full = bytearray()
    for rel in range(0, len(raw) - 0x5000, 32):
        chunk = raw[0x5000 + rel : 0x5000 + rel + 32]
        full.extend(jl_lfsr_crypt(chunk, (0x8d07 ^ (rel >> 2)) & 0xFFFF))
    # app_dir_head = full[0x000:0x100], app.bin (VA 0x0c000100) = full[0x100 : 0x100 + 0x125be0]
    return bytes(full[0x100 : 0x100 + 0x125be0])
```

### C. Memory-Map & SFR-Basisadressen (`app.bin`)

* **Flash XIP-Mapping (`app.bin`):** `0x0c000100 .. 0x0c125ce0` (Länge `0x125be0`)
* **Startup `.data`-Kopie (`app + 0x0034`):** Flash `0x0c121bc0` (Länge `0x19e0`) → SRAM `0x001228a0 .. 0x00124280`
* **Startup `.ram_code`-Kopie (`app + 0x0052`):** Flash `0x0c1235a0` (Länge `0x2740`) → SRAM `0x00124280 .. 0x001269c0`
* **Peripherie-SFRs:**
  * `JL_PORTA = 0x00fd3000`, `JL_PORTB = 0x00fd3100`, `JL_PORTC = 0x00fd3200`
  * `JL_OMAP = 0x00fd3a00`, `JL_IMAP = 0x00fd3e00`
  * `JL_GPADC = 0x00fd0e00` (`0x00fd0e24`)
  * `JL_IIC = 0x00fd0c00`
  * `JL_MCPWM = 0x00fd1700`
  * `JL_DBI = 0x00fd4320` / `0x00fe2228`
* **`gpio2port`-Tabelle (`0x0c119d2c .. 0x0c119d49`):**
  * `0 = PA` (`0x00fd3000`), `1 = PB` (`0x00fd3100`), `2 = PC` (`0x00fd3200`), `5 = P33/PP` (`0x00fe1000`), `0xD` (`0x00fd3400`), `0xE` (`0x00fd3500`).
  * Standard-Pin-IDs: `0x00..0x0F = PA00..PA15`, `0x10..0x1F = PB00..PB15`, `0x20..0x2F = PC00..PC15`, `0x30..0x3F = PD00..PD15`.

### D. Arbeitsregeln für zukünftige Agents

* Die originale Firmware-Datei [`T3 Firmware Version-UP 2.0.4.bin`](file:///c:/Users/thoma/Documents/Antigravity-Projekte/T3-USB-Firmware/T3%20Firmware%20Version-UP%202.0.4.bin) **niemals verändern**.
* Auf diesem Windows-Host unterstützt Ghidra (P0) keine rohen `pi32v2`-Binaries nativ (es akzeptiert nur PE und rohes 6502). Für statische Code-Analysen das obige Python-Entscrambling in Kombination mit einem `pi32v2`-Disassembler oder externen Ghidra-SLEIGH-Modulen (`kagaimiq/ghidra-jieli`, `quarkslab/ghidra-jieli`, `jvitkauskas/pi32v2-isa`) nutzen.

---

## 3. Eigene Firmware: Machbarkeit, Tooling & Risiken

**Fazit:** Technisch ist eine eigene Firmware **machbar**, jedoch aufwendiger als bei STM32 oder ESP32.

### Was dafür spricht
1. **Keine benutzerdefinierte Flash-Verschlüsselung im Image:**
   * Root-Verzeichnis und `uboot.boot` verwenden ausschließlich den JieLi-Werksschlüssel `0xFFFF`.
   * `otp_cfg` (`0x4737`) ist komplett `0xFF`.
   * `app.bin` nutzt das reguläre blockbasierte JieLi-SFC-Scrambling (`0x8d07 ^ ((off - 0x5000) >> 2)`).
   * Alle Image-Integritätsprüfungen basieren auf reinem **CRC-16/XMODEM** — es wurde **keine** RSA-/ECDSA-Signaturprüfung in `uboot.boot` oder den OTA-Metadaten beobachtet.
   * *Vorbehalt:* Ob im Chip-internen OTP des physischen Geräts nachträglich Fuses gesetzt wurden, lässt sich statisch aus der `.bin`-Datei nicht ausschließen.
2. **SDK, Compiler & Community-Tooling:**
   * **Toolchain:** Öffentlicher JieLi LLVM/Clang-Fork für `pi32v2` (`jieli-linux-toolchains`) sowie das **AC707N / BR35 SDK** (`watch_sdk` / `707_lite_lvgl`).
   * **Open-Source-Referenzprojekt für exakt diesen Chip:** **`ElectronicCats/jieli-ble-badge-research`** (AC707N / BR35 mit LVGL) dokumentiert eine komplette Build-Pipeline (inkl. Linux/Docker-Build und `.ufw`-Packaging) und liefert Python-Tools zum Bauen und Flashen eigener Firmware.
   * **Disassembly/RE:** Ghidra-SLEIGH-Module (`kagaimiq/ghidra-jieli` / `quarkslab/ghidra-jieli`) und `jvitkauskas/pi32v2-isa`.
3. **Flashing-Wege:**
   * **BLE OTA (`JLOTA` / RCSP):** Aktiv in der Firmware enthalten (`T3_BLE`, `JLOTA`, `rcsp`, `ble_app_ota.bin`).
   * **USB-Update / ROM-Bootloader:** `usb_update2.bin` / `usb_hid_ota.bin` im OTA-Subsystem gelistet; zusätzlich besitzt der AC707N/BR35 den eingebauten USB-UBOOT/Download-Modus (z. B. via `jl-uboot-tool`).

### Hürden & Pflichtmaßnahmen
* **Proprietäre Architektur (`pi32v2`):** Keine Arduino-/PlatformIO-/ARM-GCC-Unterstützung; BT-Stack, GPU-Treiber und Teile des SDKs liegen nur als vorkompilierte statische Bibliotheken (`.a`) vor.
* **Board-Support-Package:** Pin-Mappings für `ST7789V` (DBI + Backlight), Touch, Vibrationsmotor und GPADC-Kanäle müssen aus Abschnitt 5 übernommen bzw. für die offenen BR35-Port-`0xE`-Pins am Board verifiziert werden.
* **Pflicht-Backup vor dem Flashen:** Vor jedem ersten eigenen Flash-Vorgang **unbedingt** den gesamten 2-MB-Flash dumpen, damit `VM` (Werkskalibrierung), `BTIF` und `key_mac` nicht verloren gehen.

---

## 4. Verbaute Sensoren & Peripherie-ICs

Aus der zentralen Hardware-Initialisierung (`0x0c0d0f70..0x0c0d12d4`), den Treibertabellen (`0x03666c..0x0368ac`, `0x122058`) und den I2C-/Treiber-Wrappern (`0x0c0060e4..0x0c00618c`, `0x0c0015fc`, `0x0c0d480e`) sind folgende Bauteile eindeutig nachgewiesen:

| Funktion | Chip / Baustein | Schnittstelle & Adresse | Nachweis in `app.bin` |
| :--- | :--- | :--- | :--- |
| **USB-Spannung & -Strom (16-Bit ADC)** | **Texas Instruments `INA226`** | I2C1 (`PB02`/`PB01`), 7-Bit **`0x40`** (8-Bit `0x80`/`0x81`) | Schreibt `CONFIG (0x00) = 0x4527` (`0x0c0d10b6`), liest Die-ID `0xFF` und prüft exakt auf **`0x2260`** (`0x0c0d10cc..0x0c0d10d8`). |
| **3-Achsen-Beschleunigungssensor** (Auto-Rotation & Wasserwaage) | **Silan `SC7A20`** | I2C1 (`PB02`/`PB01`), 7-Bit **`0x19`** (8-Bit `0x32`/`0x33`) | Liest `WHO_AM_I (0x0F)` und prüft exakt auf **`0x11`** (`0x0c0d10fc..0x0c0d110a`); konfiguriert `0x20`/`0x23`; liest 6 Bytes ab `0xA8` (`0x28 \| 0x80` Auto-Inc, `0x0c008fd4`). |
| **Barometer / Höhenmesser (Variante A)** | **Bosch `BMP280`** | I2C1 (`PB02`/`PB01`), 7-Bit **`0x77`** (8-Bit `0xEE`/`0xEF`) | Liest Chip-ID `0xD0` und prüft exakt auf **`0x58`** (`0x0c0d1186..0x0c0d11a0`), liest 24 B Kalibrierung ab `0x88`, Soft-Reset `0xE0 = 0xB6`, `0xF4 = 0xB3`, `0xF5 = 0x14`, liest Druck/Temp aus `0xF7..0xFC` (`0x0c0090c8..0x0c009144`). |
| **Barometer / Höhenmesser (Variante B, Zweitbestückung)** | **ScioSense `ENS220`** | I2C1 (`PB02`/`PB01`), 7-Bit **`0x20`** (8-Bit `0x40`/`0x41`) | Init (`0x0c0d1226..0x0c0d12a6`): `MODE_CFG (0x06) = 0x08` (Reset) → `0x80` (Enable) → `0x07=0x08`, `0x08=0x00`, `0x09=0x22`, `0x06=0x13` (Continuous), liest `0x02..0x05`; im Betrieb Status `0x14` und 24-Bit-Druck `0x17..0x19` geteilt durch **`6400.0`** (= 64 LSB/Pa → hPa, `0x0c0092de..0x0c00930e`). |
| **Kapazitiver Touchscreen** | **Hynitron `CST816D`** | I2C0 (`PA06`/`PA05`), 7-Bit **`0x15`** (8-Bit `0x2A`/`0x2B`) | Treiber `tp_cst816d` (`0x036684`), liest `ChipID (0xA7)` und `FwVersion (0xA9)` (`0x0c001610..0x0c001634`) sowie 5-Byte-Touch-Frames (`0x0c0016fc`). |
| **Display (240×240 IPS)** | **Sitronix `ST7789V`** | Hardware-`JL_DBI` + PWM-Backlight | Treiber `st7789v` (`0x00122e38` / `0x122058`, String `0x0c105381`), 274-Byte-Init-Tabelle @ `0x0c0fc094` (`MADCTL 0x36 = 0x00`, `COLMOD 0x3A = 0x05` RGB565, `INVON 0x21`, Standard-Porch/Power/Gamma + `0x11` SLPOUT, `0x29` DISPON). Backlight-Modul `lv_lcd_backlight` (`0x03669c`). |
| **Temperatursensor (NTC)** | **NTC-Thermistor** | Interner GPADC auf **`PB00`** (`0x00100007`) | `0x0c0b785e..0x0c0b787c`, 1000-ms-Timer `0x0c0b7692`, 150-Einträge-Float-LUT @ `0x0c0fb588`, Bereich **`-35 °C .. +131 °C`**. |
| **Mikrofon** (Dezibel-Meter & Musik-Spektrum) | **Interner Audio-ADC des AC707N** + Elektret/MEMS-Mic | Intern (`aud_adc_demo` / `DBG_AUDADC`) | Audio-Graph `stream.bin` (`MIC534362`, `AiADC`), speist `Decibel Meter` und `Music Spectrum`. |
| **Haptik** | **Vibrationsmotor** | `JL_MCPWM` CH1 (`10 kHz`) | Im UI abschaltbar (`4.Motor switch:`), Haptik-Impulse via `0x0c00326e` / `0x0c008714`. |

* **Barometer-Höhenformel (`0x0c009338..0x0c009384`, gemeinsam für BMP280 & ENS220):**

$$h = \frac{298{,}15}{0{,}0065} \cdot \left(\left(\frac{P_0}{P}\right)^{0{,}190263} - 1\right)$$

* **Kein USB-PD-/QC-Trigger-IC:** Es wurden weder ein dedizierter PD-Controller (wie FUSB302, HUSB238, CH224K, SC8721) noch CC1/CC2- oder Schnelllade-Trigger-Routinen gefunden. Der T3 misst USB-Protokolle rein passiv über `D+`/`D-` und `VBUS`.

---

## 5. Vollständige GPIO-, Bus-, PWM- & ADC-Belegung

| Funktion / Bauteil | Signal | Pin / Kanal | Nachweis in `app.bin` / `config.dat` |
| :--- | :--- | :--- | :--- |
| **Sensor-I2C (`I2C1` / Soft-I2C)** | `SCL` / `SDA` | **`PB02`** / **`PB01`** | `0x0c0fba64`, `0x0c0d480e` (`400 kHz`, Pull-ups aktiv). Bus für `INA226`, `SC7A20`, `BMP280`/`ENS220`. |
| **Touch CST816D (`I2C0`)** | `SCL` / `SDA` | **`PA06`** / **`PA05`** | `0x0c0fba48` (`100 kHz`, Pull-ups aktiv, Adresse `0x15`). |
| **Touch CST816D** | `RST` | **`PA02`** | `0x0c0015c0..0x0c0015ce` (`JL_PORTA` Bit 2 = `0x04`, 10 ms H → L → H Reset-Puls). |
| **Touch CST816D** | `INT` | **`PB08`** (`IO_PORTB_08`) | `0x0c001662..0x0c0016ac`, `0x0c0b9ee4` (`JL_PORTB` Bit 8, SDK `TCFG_TP_INT_IO IO_PORTB_08`), IRQ-Callback `0x0c001030`. |
| **ST7789V Display** | `RST` | **`PC00`** (`0x20`) | `struct lcd_spi_platform_data` @ `0x0c0fba2c` (`+0x00 = 0x20`), gepulst in `0x0c008364..0x0c0083be` (10 ms H → L → H). |
| **ST7789V Display** | `CLK` / `D0` / `CS` / `DC` | **Hardware-`JL_DBI`** + **`PA07`** | `pin_cs`/`pin_dc`/`pin_te` in `0x0c0fba30..0x0c0fba3c` sind `0xFFFFFFFF` (Hardware-DBI `0x00fd4320` / `0x00fe2228`); `0x0c008074` setzt `PA07` (`0x00fd3008` Bit 7 = `0x80`) als Ausgang (vermutl. LCD-Power-Enable oder CS/DC). |
| **Display-Backlight** | `PWM` | **`IO_LCD_PG`** (Pin-ID `0xE7`) | `0x0c0fba38` (`0x000000e7` = `IO_LCD_PG`, `power_gate_pwm`, **`10 kHz`**), Duty aus `0x00123060`. |
| **Vibrationsmotor** | `PWM` | **`IO_MT_PG`** (Pin-ID `0xE8`) | `0x0c0d0f4a`, `0x003212` (`0xE8` = `IO_MT_PG`, `power_gate_pwm`, **`10 kHz`**), Impulse via `0x0c00326e` / `0x0c008714`. |
| **NTC-Thermistor** | `ADC` | **`PB00`** (`AD_CH_IO_PB0` = `0x00100007`) | `0x0c0b785e..0x0c0b787c` (`JL_PORTB` Bit 0 = `0x01`, Pull-up aktiv). |
| **USB Datenleitungen** | `D-` / `D+` | **`PC02`** / **`PC03`** | `0x0c0d0f7a..0x0c0d1024`, `0x0c0087c4` (`JL_PORTC` Bit 2/3, GPADC `0x00100009` = `D-`, `0x0010000a` = `D+`). |
| **VBUS-Ripple (AC)** | `ADC` | **`PA10`** (`ADC_CH_IO_PA10` = `0x00100004`) | `0x0c0d1050..0x0c0d1090` (`JL_PORTA` Bit 10, GPADC-Kanal `0x00100004`). |
| **System-Reset / Wake** | `RESET` | **`PB07`** | `isd_config.ini` (`RESET=PB07_08_0`, 8 s Low-aktiv) & `cfg_tool.bin`. |
| **LED / Pilot-Lamp** | `GPIO` | **`PB03`** | `config.dat` (SDFILE-Ressource). |
| **Power-IO** | `GPIO` | **`PC03_1`** | `config.dat` (SDFILE-Ressource). |

---

## 6. Messarchitektur (Spannung, Strom, Ripple, D+/D-, Temperatur)

### A. DC-Spannung (`VBUS`) & Bidirektionaler DC-Strom (`IBUS`) — `INA226` (`0x40`)

* **Initialisierung (`0x0c0d10b6`):** `CONFIG (0x00) = 0x4527` (`AVG = 16` Samples, `VBUSCT = 1,1 ms`, `VSHCT = 1,1 ms`, `MODE = 111` Shunt & Bus kontinuierlich).
* **Abfrage-Timer:** `0x0c0d10dc` registriert `0x0c008848` auf einen **`200 ms`-Timer** (`0xc8`).

1. **`VBUS`-Messung (`0x0c008882..0x0c0088fc`):**
   * Liest 16-Bit-Register `0x02` (*Bus Voltage*) via `ina226_read` (`0x0c0060e4`, Big-Endian → Little-Endian) und multipliziert mit **`0,0016123`** (`0x3ad353d0` @ `0x0c00888c`):
     $$V_{\text{BUS,raw}} = \text{Reg}_{0x02} \times 0{,}0016123\,\text{V/LSB}$$
   * **Hardware-Ableitung:** Da der `INA226` nativ `1,25 mV/LSB` (max. 36 V) besitzt, beweist `0,0016123 / 0,00125 = 1,28984` einen **externen Spannungsteiler (~1,29 : 1)** am VBUS-Pin für den Bereich **`1,2 .. 50,0 V`** (Vollausschlag ~52,8 V). Danach folgt der Multiplikator aus `Item:10V Calibration` (`%.4fV x %.4f = %.4fV`).

2. **`IBUS`-Messung (`0x0c008928..0x0c0089c6`):**
   * Liest vorzeichenbehaftetes 16-Bit-Register `0x01` (*Shunt Voltage*, Zweierkomplement → **bidirektional**; bei negativem Wert wird das `-IBUS`-Flag gesetzt und der Betrag gebildet).
   * Multipliziert mit **`0,0025`** (`0x3b23d70a` @ `0x0c008964`) und **`0,2`** (`0x3e4ccccd` @ `0x0c00896e`):
     $$I_{\text{BUS,raw}} = |\text{Reg}_{0x01}| \times 0{,}0025\,\text{mV/LSB} \times 0{,}2\,\text{A/mV} = |\text{Reg}_{0x01}| \times 0{,}0005\,\text{A/LSB}$$
   * **Hardware-Ableitung:** `0,0025 mV` (2,5 µV) entspricht 1 LSB des INA226-Shunt-ADCs; `0,2 A/mV` (`200 A/V`) beweist einen **`5 mΩ`-Shunt** (`0,5 mA/LSB` Auflösung, **±16,38 A** theoretischer Vollausschlag).
   * Nachverarbeitung: Abzug des Nullpunkt-Offsets (`2.Zero no-load current`), Multiplikation mit `Item:2A Calibration` (`%.4fA x %.4f = %.4fA`), Deadband-Unterdrückung unter **`0,001 A`** (`0x3a83126f` @ `0x0c0089a6`).

3. **Leistung, Energie & Kabelwiderstand (`0x0c0089c0..0x0c008b6e`):**
   * $P = V \cdot I$; Integration von `mAh` (bis `99999`) und `Wh` über Faktor **`3600.0`** (`0x40ac200000000000` @ `0x0c0089f4` & `0x0c008a3a`).
   * **Kabeltest (`Cable Evaluation`):** $R_{\text{cable}} = (V_0 - V_1) / I_1$ aus Leerlaufspannung $V_0$ und Lastmessung $(V_1, I_1)$.

### B. AC-Restwelligkeit (`VBUS Ripple` & Frequenz) — Interner `JL_GPADC`

* `0x0c0d1050..0x0c0d10b0`: Richtet **`PA10`** (`IO_PORTA_10`, `ADC_CH_IO_PA10 = 0x00100004`, hardwareseitig AC-gekoppelt an `VBUS`) als schnellen GPADC-Eingang ein.
* `0x0c008c74..0x0c008f30`: Pollt `JL_GPADC` (`0x00fd0e00`) in einer engen Schleife, erfasst **256 Samples**, skaliert mit **`0,781738 mV/LSB`** (`0x3f482000` @ `0x0c008d70`, ~3,2 V / 4096) für `Tim-Ripple` und `Avg-Ripple` und ermittelt per **256-Punkt-FFT** (`0x0c008df0` → `0x0c006a58`) die dominante Ripple-Frequenz (`%.1fkHz`).

### C. USB `D+` / `D-` & NTC-Temperatur — Interner `JL_GPADC`

* **`D-` / `D+`:** `PC02` (`0x00100009`) & `PC03` (`0x0010000a`), **`500 ms`-Timer** (`0x01f4`) ruft `0x0c0087c4` auf, liest mV vom GPADC und teilt durch **`1000.0`** (`0x408f400000000000` @ `0x0c0087f6`).
* **NTC-Temperatur:** `PB00` (`0x00100007`), **`1000 ms`-Timer** `0x0c0b7692`, 150-Einträge-Float-LUT @ `0x0c0fb588`, Bereich `-35 °C .. +131 °C`.

---

## 7. Bluetooth-Protokoll (`T3_BLE` & Classic `T3_SPP`)

Der T3 spricht das **ATorch / E-Test-Protokoll** parallel über **BLE GATT** (`T3_BLE`) und **Bluetooth Classic SPP** (`T3_SPP`) sowie JieLis RCSP/JLOTA-Protokoll für OTA-Updates.

### A. BLE GATT-Profil (`0x0c0fc2f2`, registriert in `0x0c0df2b4`)

| Service UUID | Characteristic UUID | Value Handle | CCCD Handle | Properties | Funktion |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x1800` (GAP) | `0x2A00` | `0x0003` | – | Read | Gerätename **`T3_BLE`** |
| `0xAE00` (JieLi) | `0xAE01` | `0x0006` | – | WriteNoResp | JieLi RCSP / **JLOTA** RX |
| `0xAE00` (JieLi) | `0xAE02` | `0x0008` | `0x0009` | Notify | JieLi RCSP / **JLOTA** TX |
| `0xFEE7` | `0xFEC9` / `0xFEA1` | `0x000C` / `0x000E` | `0x000F` | Read / Notify | WeChat/JieLi Auth-Service |
| **`0xFFE0`** *(ATorch)* | **`0xFFE1`** | **`0x0052`** | **`0x0053`** | **WriteNoResp + Write + Notify** | **Telemetrie-Stream (TX) & Kommandos (RX)** |
| **`0xFFE0`** *(ATorch)* | **`0xFFE2`** | **`0x0055`** | – | **WriteNoResp + Write** | **Kommando-Kanal (RX)** |

### B. TX-Telemetrie-Paket — 36 Bytes (`0x0c009778..0x0c0098d6`, Versand via `0x0c0dffaa`)

Identisch gesendet per BLE Notify auf **`0xFFE1` (Handle `0x0052`)** und über **`T3_SPP`**. Alle Mehrbyte-Werte sind **Big-Endian (unsigned)**:

| Byte(s) | Größe | Bedeutung | Skalierung / Wert |
| :--- | :--- | :--- | :--- |
| `0..1` | 2 B | Sync-Header | `0xFF, 0x55` |
| `2` | 1 B | Paket-Typ | `0x01` (Messdaten-Report) |
| `3` | 1 B | Gerätetyp | `0x03` (USB-Meter) |
| `4..6` | 3 B | **Spannung (VBUS)** | Wert / `100` → **V** (`0,01 V` Auflösung) |
| `7..9` | 3 B | **Strom (IBUS)** | Wert / `100` → **A** (`0,01 A` Auflösung) |
| `10..12` | 3 B | **Kapazität** | direkt in **mAh** |
| `13..16` | 4 B | **Energie** | Wert / `100` → **Wh** (`0,01 Wh` Auflösung) |
| `17..18` | 2 B | **USB D-** | Wert / `100` → **V** |
| `19..20` | 2 B | **USB D+** | Wert / `100` → **V** |
| `21..22` | 2 B | **Temperatur** | direkt in **°C** (aus NTC auf `PB00`) |
| `23..24` | 2 B | **Laufzeit: Stunden** | `sekunden / 3600` |
| `25` | 1 B | **Laufzeit: Minuten** | `0..59` |
| `26` | 1 B | **Laufzeit: Sekunden** | `0..59` |
| `27` | 1 B | **Backlight-Timeout / -Stufe** | aktueller Wert |
| `28..29` | 2 B | Konstante | `0x13, 0x88` (`5000`) |
| `30..31` | 2 B | Reserviert | `0x00, 0x00` |
| `32..33` | 2 B | Konstante | `0x02, 0x8A` (`650`) |
| `34` | 1 B | Reserviert | `0x00` |
| `35` | 1 B | **Prüfsumme** | `(sum(bytes[2..34]) & 0xFF) ^ 0x44` |

*(Hinweis: Barometer-/Höhendaten aus `BMP280`/`ENS220` und AC-Ripple-Werte werden nicht über diesen 36-Byte-Frame übertragen, sondern nur lokal am Display angezeigt.)*

### C. RX-Kommando-Routing & unterstützte Befehle (`0x0c0e0000`, `0x0c0e07d8`, `0x0c0150f6`)

Eingehende Daten auf BLE (`0xFFE1`/`0xFFE2`) bzw. SPP werden zweistufig geroutet:
1. **Präfix `0xFE 0xDC 0xBA`** (`0x0c0e07d8` vergleicht gegen `0x0c119dfd`): Weiterleitung an den JieLi **RCSP/JLOTA**-Stack (`0x0c0e1b56`).
2. **Sonst (`0x0c0b6d5c` → `0x0c0150f6`):** ATorch-Kommandoparser für **10-Byte-Frames** (`FF 55 11 03 <CMD> 00 00 00 00 <CS>`, Prüfung auf `buf[0]==0xFF`, `buf[1]==0x55`, Länge `>= 10`):
   * `CMD = 0x01`: **Wh-Zähler zurücksetzen** (`[0x00101bd0 + 0x22f] = 0`)
   * `CMD = 0x02`: **mAh-Zähler zurücksetzen** (`[0x00101bd0 + 0x230] = 0`)
   * `CMD = 0x03`: **Zeitzähler zurücksetzen** (`[0x00101bd0 + 0x237] = 0`)
   * `CMD = 0x31 .. 0x34`: **UI-Tasten-/Navigations-Emulation** — sendet interne UI-Nachrichten via `0x0c0b692c` an die Task `"ui"`:
     * `0x31` → Event `(0xAF, 0x9E)` *(Setup / Menü)*
     * `0x32` → Event `(0xAF, 0xA5)` *(Weiter / Plus)*
     * `0x33` → Event `(0xAF, 0xA6)` *(Zurück / Minus)*
     * `0x34` → im selben `0x31..0x34`-Branch (`0x0c015116..0x0c015178`)

---

## 8. Offene Punkte (Unknowns — statisch nicht aus der `.bin` allein beweisbar)

1. **Analoge Bauteile der AC-Ripple-Stufe:** Exakte R/C-Werte der AC-Kopplung zwischen `VBUS` und dem GPADC-Pin **`PA10` (`ADC_CH_IO_PA10`)**.
2. *(Gelöst am Live-Gerät 24.11.2026)* **Konkrete Barometer-Bestückung:** Auf dem vorliegenden T3-Board ist ein **ScioSense `ENS220`** (`0x20`) bestückt (`INA226 = OK`, `SC7A20 = OK`, `BMP280 = --`, `ENS220 = OK`). Ebenso live im Bring-up verifiziert: `ST7789V` Display, `CST816D` Touch (`TP: X/Y`) und `INA226` V/I/P-Messung. Noch offen im Nachtest: `IO_MT_PG`-Motor (Duty auf `100 %` / `200 ms` erhöht) und USB-Slave-Enumeration (`TCFG_OTG_MODE = OTG_SLAVE_MODE`).
3. *(Gelöst über AC707N-SDK & Live-Backup)* **BR35-Pin-IDs & Chipkey:** `0xE7 = IO_LCD_PG`, `0xE8 = IO_MT_PG`, `0x00100004 = ADC_CH_IO_PA10` (`PA10`), Touch-`INT = IO_PORTB_08` (`PB08`); Live-Dump bestätigt keinen Custom-OTP-Chipkey (`0x8d07` gilt direkt).
4. **GPIOs / ADKEY der beiden physischen Taster:** Bisher ist nur `RESET = PB07` (`isd_config.ini`) bekannt. Die genaue Pin-/ADKEY-Zuordnung der beiden Front-/Seitentaster muss noch per statischer Analyse von `app.bin` oder Live-GPIO-Scan ermittelt werden (benötigt u. a. für `FIRE`/`USE` in `projects/t3-doom`).
