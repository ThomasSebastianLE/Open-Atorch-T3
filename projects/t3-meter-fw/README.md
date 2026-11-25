# Teilprojekt 1: `t3-meter-fw` — Verbesserte ATORCH-T3-Firmware

Ziel dieses Teilprojekts ist eine offene, aufgeräumte, schnelle und erweiterte Power-Meter-Firmware für das ATORCH / JUWEI T3 auf Basis von `sdk/ac707n` und [`shared/t3-bsp`](../../shared/t3-bsp/README.md).

## Implementierte Features (v1.1)

* **6-Tab LVGL-v8-UI (`MTR`, `CRV`, `RPL`, `CBL`, `SNS`, `SET`) + `PB07`-Taster:**
  * **MTR (Live-Meter):** `VBUS`, bidirektionaler `IBUS`, Leistung `W`, getrennte `IN / OUT`-Zähler (`+mAh / -mAh`), `3.7 V`-Powerbank-Zellkapazität, `Wh`, Peak-Werte (`Pmax`, `Ipeak`), Laufzeit, **3 persistente Speicherbänke (`BANK A/B/C`)** sowie **Big-Numbers-Modus** per Fingertipp auf die Messwert-Fläche.
  * **CRV (Echtzeit-Kurve):** `lv_chart` für `V` & `I`, `Vmin/Vmax`, `Ipeak` und **Brownout-Zähler** (`VBUS < 4,50 V` unter Last).
  * **RPL (AC-Ripple-Oszilloskop):** `PA10` GPADC-Capture, `Vpp`, gleitender Durchschnitt, dominante Schaltfrequenz (`Hz`/`kHz`) und direkte **Netzteil-Qualitätsbewertung** (`SEHR SAUBER < 30 mVpp` / `OK` / `STARKES RAUSCHEN >= 80 mVpp`).
  * **CBL (Kabel- & Droop-Test):** 2-Schritt-Messung von `V0` (Leerlauf) → `R_cable` (`mΩ`), **Spannungseinbruch (`Droop` in `mV` & `%`)** und Qualitätsbewertung.
  * **SNS (Schnelllade-Monitor, Baro & Wasserwaage):** Passive Schnelllade-Erkennung über `D+`/`D-` + `VBUS` (`USB 5V`, `DCP 1.5A`, `Apple 1/2A / 2.4A`, `Samsung AFC/FCP`, `QC 2.0/3.0`, `PD/QC 9V/12V/15V/20V`), `BMP280`/`ENS220` Luftdruck & Höhe (`HOEHE = 0m`), `SC7A20` Wasserwaage + Auto-Rotation.
  * **SET (Config-Menü & Kalibrierung):** Helligkeit (`10..100 %`), Haptik (`EIN/AUS`), Auto-Rotation (`EIN/AUS`), **`BLE Beacon: EIN/AUS`**, **Alarme (`EIN/AUS`)**, `V`/`I`-Gain-Kalibrierung, `ZERO I`, `SAVE VM` (dauerhafte Speicherung in `VM` via `syscfg_write`) und `UBOOT`-Trigger (1 s gedrückt halten).
* **Alarme mit Haptik & Warnbanner:**
  * Ladeende (`< 50 mA` nach vorheriger Last), Überspannung (`> 21 V`), Überstrom (`> 5 A`) und Überhitzung (`> 60 °C`).
* **Offline-Blackbox-Ringpuffer:**
  * Speichert alle 10 s `(V, I, W, °C)` im RAM (60 Einträge = 10 min).
* **Bluetooth / BLE Beacon & WebBLE-App:**
  * Über `SET` deaktivierbarer **BLE Beacon** (Company ID `0x5433`, 16-Byte-Telemetrie-Payload, Gerätename `T3_MTR`) sowie ATorch-kompatibles GATT (`0xFFE0`/`0xFFE1`/`0xFFE2`).
  * Siehe [`docs/BLE_APP_PROTOCOL.md`](docs/BLE_APP_PROTOCOL.md) für die vollständige App-Protokoll-Dokumentation und [`webble/index.html`](webble/index.html) für das Browser-Dashboard.

## Build & Flash

```powershell
.\scripts\build.ps1 -Project meter
.\scripts\flash-slot.ps1 -SlotPath .\firmware-builds\t3-meter-fw-slot.bin
```
