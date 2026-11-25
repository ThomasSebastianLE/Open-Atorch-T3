#!/usr/bin/env python3
"""
Poll live touch diagnostics from the T3 over USB control transfer
(GET_STATUS wValue=0x00FD wIndex=0x00FE wLength=16).
"""
from __future__ import annotations

import struct
import sys
import time

import libusb_package
import usb.core


def find_t3() -> usb.core.Device:
    backend = libusb_package.get_libusb1_backend()
    for pid in (0x5B50, 0x5750):
        dev = usb.core.find(idVendor=0x0483, idProduct=pid, backend=backend)
        if dev is not None:
            return dev
    raise RuntimeError("Kein T3 USB-Geraet (0483:5B50 / 0483:5750) gefunden.")


def read_diag(dev: usb.core.Device) -> tuple:
    raw = bytes(dev.ctrl_transfer(0x83, 0x00, 0x00FD, 0x00FE, 16, timeout=1000))
    if len(raw) != 16 or raw[15] != 0xA5:
        raise RuntimeError(f"Unerwartete Antwort: {raw.hex()}")
    raw5 = raw[:5]
    addr, int_lvl, chip_id, ack_cnt, lx, ly, pb07, magic = struct.unpack_from("<BBBHhhBB", raw, 5)
    return raw5, addr, int_lvl, chip_id, ack_cnt, lx, ly, pb07


def main() -> int:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
    dev = find_t3()
    print(f"[touch_diag] Verbunden mit {dev.idVendor:04x}:{dev.idProduct:04x}, polle {seconds:.0f}s...")
    t0 = time.time()
    last_line = None
    while time.time() - t0 < seconds:
        raw5, addr, int_lvl, chip_id, ack_cnt, lx, ly, pb07 = read_diag(dev)
        line = (
            f"raw=[{raw5.hex(' ')}] addr=0x{addr:02x} chip=0x{chip_id:02x} "
            f"INT={int_lvl} PB07={pb07} xy=({lx},{ly})"
        )
        if line != last_line:
            print(f"[{time.time() - t0:5.2f}s] {line} (ack={ack_cnt})", flush=True)
            last_line = line
        time.sleep(0.05)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
