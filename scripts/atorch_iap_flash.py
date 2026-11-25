#!/usr/bin/env python3
"""
Flash firmware to an ATORCH / JUWEI T3 in USB-HID-IAP mode (`ATORCH T3 V1.0.1`,
VID=0x0483, PID=0x5750) over libusb (EP 0x02 OUT / EP 0x82 IN).

Protocol reverse-engineered from DcTester.exe (V1.0.1):
  - 64-byte USB interrupt packets prefixed with report_id 0x55.
  - CRC-16/MODBUS (poly 0xA001, init 0xFFFF, little-endian) over pkt[0:pkt_len-2].
  - Start packet: [0xAA, 0x06, total_chunks_lo, total_chunks_hi, crc_lo, crc_hi]
  - Device ACKs start with resp[1] == 0xBB.
  - Data packet:  [0xCC, 6 + len(chunk), idx_lo, idx_hi, chunk(<=58B), crc_lo, crc_hi]
  - Device ACKs chunk idx with resp[1] == 0xDD, resp[2..3] == idx (LE).
  - Device signals write/verify phase with resp[1] == 0xEE, completion with 0xFF, error with 0x11.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

import libusb_package
import usb.core
from tqdm import tqdm

VID = 0x0483
PID = 0x5750
REPORT_ID = 0x55
EP_OUT = 0x02
EP_IN = 0x82
CHUNK_SIZE = 58  # 0x3A


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def verify_resp_crc(pkt: bytes) -> bool:
    if len(pkt) < 4:
        return False
    plen = pkt[1]
    if plen < 4 or plen > len(pkt):
        return False
    expected = pkt[plen - 2] | (pkt[plen - 1] << 8)
    return crc16_modbus(pkt[: plen - 2]) == expected


def build_report(payload: bytes) -> bytes:
    buf = bytearray(64)
    buf[0] = REPORT_ID
    n = min(len(payload), 63)
    buf[1 : 1 + n] = payload[:n]
    return bytes(buf)


def build_start_packet(total_chunks: int) -> bytes:
    pkt = bytearray(64)
    pkt[0] = 0xAA
    pkt[1] = 0x06
    pkt[2] = total_chunks & 0xFF
    pkt[3] = (total_chunks >> 8) & 0xFF
    crc = crc16_modbus(bytes(pkt[:4]))
    pkt[4] = crc & 0xFF
    pkt[5] = (crc >> 8) & 0xFF
    return build_report(bytes(pkt))


def build_data_packet(idx: int, chunk: bytes) -> bytes:
    if not (1 <= len(chunk) <= CHUNK_SIZE):
        raise ValueError(f"Invalid chunk length {len(chunk)}")
    pkt = bytearray(64)
    pkt_len = 6 + len(chunk)
    pkt[0] = 0xCC
    pkt[1] = pkt_len
    pkt[2] = idx & 0xFF
    pkt[3] = (idx >> 8) & 0xFF
    pkt[4 : 4 + len(chunk)] = chunk
    crc = crc16_modbus(bytes(pkt[: 4 + len(chunk)]))
    pkt[4 + len(chunk)] = crc & 0xFF
    pkt[5 + len(chunk)] = (crc >> 8) & 0xFF
    return build_report(bytes(pkt))


def open_iap_device() -> usb.core.Device:
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=libusb_package.get_libusb1_backend())
    if dev is None:
        raise RuntimeError(f"Geraet {VID:04x}:{PID:04x} nicht gefunden.")
    try:
        cfg = dev.get_active_configuration()
    except usb.core.USBError:
        cfg = None
    if cfg is None:
        dev.set_configuration()
    usb.util.claim_interface(dev, 0)
    # Drain any stale input reports
    while True:
        try:
            dev.read(EP_IN, 64, timeout=50)
        except usb.core.USBError:
            break
    return dev


def read_packet(dev: usb.core.Device, timeout_ms: int = 5000) -> bytes:
    raw = bytes(dev.read(EP_IN, 64, timeout=timeout_ms))
    if len(raw) < 2:
        raise RuntimeError(f"Short HID packet: {raw.hex()}")
    return raw[1:]  # strip report_id byte


def flash_iap(fw_path: pathlib.Path, dry_run: bool = False) -> int:
    fw = fw_path.read_bytes()
    total_chunks = (len(fw) + CHUNK_SIZE - 1) // CHUNK_SIZE
    if total_chunks > 0xFFFF:
        raise ValueError(f"Firmware too large for 16-bit chunk counter: {total_chunks}")

    print(f"[atorch_iap] Firmware: {fw_path} ({len(fw)} Bytes, {total_chunks} Chunks a {CHUNK_SIZE} B)")
    if dry_run:
        _ = build_start_packet(total_chunks)
        _ = build_data_packet(0, fw[:CHUNK_SIZE])
        _ = build_data_packet(total_chunks - 1, fw[(total_chunks - 1) * CHUNK_SIZE :])
        print("[atorch_iap] Dry-run OK (Start-, Chunk-0- & Last-Chunk-Paket geprueft).")
        return 0

    dev = open_iap_device()
    print("[atorch_iap] Sende Start-Handshake (0xAA)...")
    dev.write(EP_OUT, build_start_packet(total_chunks), timeout=3000)

    while True:
        resp = read_packet(dev, timeout_ms=10000)
        if resp[0] == 0x11:
            raise RuntimeError(f"Geraet meldet Fehler (0x11) beim Start: {resp[:16].hex()}")
        if resp[0] == 0xBB:
            if not verify_resp_crc(resp):
                raise RuntimeError(f"Ungueltige CRC in 0xBB-Antwort: {resp[:16].hex()}")
            break

    print("[atorch_iap] Handshake OK (0xBB). Uebertrage Firmware...")
    with tqdm(total=len(fw), unit="B", unit_scale=True, unit_divisor=1024) as bar:
        for idx in range(total_chunks):
            chunk = fw[idx * CHUNK_SIZE : (idx + 1) * CHUNK_SIZE]
            pkt = build_data_packet(idx, chunk)
            dev.write(EP_OUT, pkt, timeout=3000)

            while True:
                resp = read_packet(dev, timeout_ms=10000)
                cmd = resp[0]
                if cmd == 0x11:
                    raise RuntimeError(f"Geraet meldet Fehler (0x11) bei Chunk {idx}: {resp[:16].hex()}")
                if cmd == 0xDD:
                    ack_idx = resp[2] | (resp[3] << 8)
                    if ack_idx == idx:
                        break
                elif cmd in (0xEE, 0xFF) and idx == total_chunks - 1:
                    break

            bar.update(len(chunk))

    print("[atorch_iap] Alle Chunks gesendet. Warte auf Abschluss (0xEE / 0xFF)...")
    while True:
        resp = read_packet(dev, timeout_ms=30000)
        cmd = resp[0]
        if cmd == 0x11:
            raise RuntimeError(f"Geraet meldet Fehler (0x11) nach Transfer: {resp[:16].hex()}")
        if cmd == 0xEE:
            print("[atorch_iap] Geraet schreibt/verifiziert (0xEE)...")
        elif cmd == 0xFF:
            print("[atorch_iap] ERFOLG (0xFF)! Update abgeschlossen, T3 startet neu.")
            return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Flash ATORCH T3 via USB HID-IAP bootloader (0483:5750).")
    parser.add_argument("firmware", type=pathlib.Path, help="Path to firmware .bin (e.g. T3 Firmware Version-UP 2.0.4.bin)")
    parser.add_argument("--dry-run", action="store_true", help="Validate file & packet framing without touching USB")
    args = parser.parse_args()
    if not args.firmware.exists():
        print(f"Datei nicht gefunden: {args.firmware}", file=sys.stderr)
        return 1
    try:
        return flash_iap(args.firmware, dry_run=args.dry_run)
    except Exception as exc:
        print(f"[atorch_iap] FEHLER: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
