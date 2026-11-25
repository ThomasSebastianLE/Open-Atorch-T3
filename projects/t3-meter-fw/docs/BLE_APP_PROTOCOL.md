# T3 OpenMeter — BLE- & App-Protokoll-Dokumentation

Dieses Dokument beschreibt die Bluetooth-Low-Energy-Schnittstelle (BLE GATT + abschaltbarer BLE-Beacon) von [`t3-meter-fw`](../README.md) als Grundlage für eine spätere Handy-App (Android, iOS oder WebBLE).

---

## 1. Gerätename & Discovery

* **BLE-Gerätename (GAP `0x2A00`):** `T3_BLE` (bzw. `T3_OpenMeter`)
* **Classic SPP-Name:** `T3_SPP` (optional, abwärtskompatibel)
* **Steuerbarkeit am Gerät:** Im Tab **`SET` (Config-Menü)** lässt sich **`BLE Beacon`** jederzeit per Touch oder Taster (`PB07`) ein- und ausschalten (`EIN` / `AUS`). Der Zustand wird im `VM`-Flash gespeichert.

---

## 2. BLE Beacon (Passives Advertising, abschaltbar)

Wenn **`BLE Beacon: EIN`** aktiv ist, sendet der T3 neben dem normalen Connectable-Advertising die aktuellen Live-Messwerte im **Manufacturer Specific Data (`0xFF`)** Block. Eine App (oder Home Assistant / ESPHome) kann die Werte so auch **ohne aktive GATT-Verbindung** passiv mitlesen.

### Payload-Aufbau (16 Bytes nach `0xFF`, Little-Endian)

| Offset | Größe | Typ | Bedeutung | Einheit / Skalierung |
| :--- | :--- | :--- | :--- | :--- |
| `0..1` | 2 B | `uint16` | Company-ID (lokal/custom) | `0x5433` (`"T3"`) |
| `2` | 1 B | `uint8` | Protokoll-Version | `0x01` |
| `3` | 1 B | `uint8` | Status-Flags | Bit 0: `-IBUS` (Rückstrom), Bit 1: Alarm aktiv, Bit 2: Ripple-Warnung |
| `4..5` | 2 B | `uint16` | Spannung (`VBUS`) | mV (`0..50000` mV) |
| `6..7` | 2 B | `int16` | Strom (`IBUS`, vorzeichenbehaftet) | mA (`-16380..+16380` mA) |
| `8..11` | 4 B | `uint32` | Leistung (`P`) | mW |
| `12..13` | 2 B | `int16` | NTC-Temperatur | `0,1 °C` (z. B. `253` = `25,3 °C`) |
| `14..15` | 2 B | `uint16` | VBUS AC-Ripple (`Vpp`) | mVpp |

---

## 3. BLE GATT-Profil (Aktive App-Verbindung)

### A. ATorch-kompatibler Telemetrie- & Kommando-Service (`0xFFE0`)

| Characteristic UUID | Properties | Funktion |
| :--- | :--- | :--- |
| **`0xFFE1`** | `Notify`, `Write`, `WriteNoResp` | 36-Byte-Telemetrie-Stream (TX, kompatibel zu ATorch/E-Test) + RX-Kommandos |
| **`0xFFE2`** | `Write`, `WriteNoResp` | Dedizierter RX-Kommando-Kanal |

#### 36-Byte TX-Telemetrie-Frame (`0xFFE1` Notify, Big-Endian)

| Byte(s) | Bedeutung | Skalierung |
| :--- | :--- | :--- |
| `0..1` | Sync-Header | `0xFF, 0x55` |
| `2` | Paket-Typ | `0x01` (Messdaten-Report) |
| `3` | Gerätetyp | `0x03` (USB-Meter) |
| `4..6` | `VBUS` | Wert / `100` → **V** (`0,01 V`) |
| `7..9` | `IBUS` | Wert / `100` → **A** (`0,01 A`) |
| `10..12` | Kapazität | direkt in **mAh** |
| `13..16` | Energie | Wert / `100` → **Wh** (`0,01 Wh`) |
| `17..18` | `USB D-` | Wert / `100` → **V** |
| `19..20` | `USB D+` | Wert / `100` → **V** |
| `21..22` | Temperatur | direkt in **°C** |
| `23..24` | Laufzeit Stunden | `0..65535` |
| `25` | Laufzeit Minuten | `0..59` |
| `26` | Laufzeit Sekunden | `0..59` |
| `27` | Backlight-Stufe | `10..100` |
| `28..34` | Konstanten/Reserviert | `13 88 00 00 02 8A 00` |
| `35` | Prüfsumme | `(sum(bytes[2..34]) & 0xFF) ^ 0x44` |

#### 10-Byte RX-Kommandos (`FF 55 11 03 <CMD> 00 00 00 00 <CS>`)

| `CMD` | Aktion |
| :--- | :--- |
| `0x01` | Wh-Zähler zurücksetzen |
| `0x02` | mAh-Zähler zurücksetzen |
| `0x03` | Zeitzähler zurücksetzen |
| `0x10` | Aktive Speicherbank wechseln (`A` / `B` / `C`) |
| `0x11` | Nullstrom-Abgleich (`Zero I`) ausführen |
| `0x12` | BLE-Beacon umschalten (`byte[5] = 0/1`) |

---

## 4. Beispiel-Code für WebBLE (Browser / Handy-Prototyp)

```javascript
async function connectT3() {
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [0xffe0] }],
    optionalServices: [0xffe0]
  });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(0xffe0);
  const char = await service.getCharacteristic(0xffe1);

  await char.startNotifications();
  char.addEventListener("characteristicvaluechanged", (e) => {
    const d = new DataView(e.target.value.buffer);
    if (d.getUint8(0) !== 0xFF || d.getUint8(1) !== 0x55) return;
    const v = ((d.getUint8(4) << 16) | (d.getUint8(5) << 8) | d.getUint8(6)) / 100;
    const i = ((d.getUint8(7) << 16) | (d.getUint8(8) << 8) | d.getUint8(9)) / 100;
    const mah = (d.getUint8(10) << 16) | (d.getUint8(11) << 8) | d.getUint8(12);
    const wh = d.getUint32(13, false) / 100;
    console.log({ v, i, mah, wh });
  });
}
```

## 5. USB-Live-Telemetrie (PC-Kabel-Schnittstelle)

Über USB (`VID=0x0483`, `PID=0x5B50`, `WinUSB`) liefert `GET_STATUS` (`wValue=0x00FD`, `wIndex=0x00FE`, `wLength=16`) Live-Rohdaten; für den Blackbox-Ringpuffer kann derselbe Control-Endpunkt um einen Block-Read (`wValue=0x00FC+2..`) erweitert werden.
