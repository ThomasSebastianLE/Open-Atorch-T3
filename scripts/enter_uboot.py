#!/usr/bin/env python3
"""Passive USB probe & JieLi UBOOT entry trigger for ATORCH T3 V2.0.4 (VID 0x0483, PID 0x5750).

Verified from T3 Firmware Version-UP 2.0.4.bin (0x0c0b5920..0x0c0b59da / user_setup.c:setup_other):
  - bmRequestType = 0x83 (DIR=IN, TYPE=STANDARD, RECIP=OTHER)
  - bRequest      = 0x00 (USB_REQ_GET_STATUS)
  - wValue        = 0x00FC
  - wIndex        = 0x00FE
  - wLength = 8   -> passive probe: returns 8x 0xFF if the JieLi UBOOT jump hook is supported
  - wLength = 0   -> triggers go_mask_usb_updata() -> nvram_set_boot_state(2) + cpu_reset()
                     so the device reboots into BR35UBOOT1.00 (VID 0x4C4A, PID 0x2942).
"""

import argparse
import sys

import libusb_package
import usb.core
import usb.util

T3_VID = 0x0483
T3_PIDS = (0x5750, 0x5B50)

BM_REQUEST_TYPE = 0x83
B_REQUEST = 0x00
W_VALUE = 0x00FC
W_INDEX = 0x00FE


def print_zadig_help(err: Exception) -> None:
    print(f"\n[enter_uboot] USB-Zugriff durch Windows-Treiber blockiert: {err}", file=sys.stderr)
    print(
        "\nHinweis (Windows hidusb -> WinUSB für USB-Backup):\n"
        "  1. Zadig (https://zadig.akeo.ie/) starten -> 'Options -> List All Devices'.\n"
        "  2. 'ATORCH T3 V2.0.4' (USB ID: 0483 5750) auswählen.\n"
        "  3. Temporär auf 'WinUSB' umstellen und 'Replace Driver' klicken.\n"
        "  4. Danach erneut testen bzw. Backup ausführen.\n"
        "  (Nach dem Backup kann im Windows Geräte-Manager über 'Treiber deinstallieren'\n"
        "   jederzeit wieder der Standard-HID-Treiber aktiviert werden.)",
        file=sys.stderr,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="ATORCH T3 USB UBOOT probe/trigger")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--probe", action="store_true", help="Passive Probe (Standard, kein Reboot)")
    mode.add_argument("--reboot", action="store_true", help="Probe + Reboot in BR35UBOOT1.00")
    args = parser.parse_args()

    backend = libusb_package.get_libusb1_backend()
    if backend is None:
        print("[enter_uboot] FEHLER: libusb1-Backend nicht verfügbar.", file=sys.stderr)
        return 2

    dev = None
    for pid in T3_PIDS:
        dev = usb.core.find(idVendor=T3_VID, idProduct=pid, backend=backend)
        if dev is not None:
            break
    if dev is None:
        print(
            f"[enter_uboot] Kein ATORCH T3 im Normalbetrieb (VID=0x{T3_VID:04X}, PID=0x5750/0x5B50) gefunden.\n"
            "  Falls das Gerät bereits im Bootloader ist, prüfe VID=0x4C4A PID=0x2942 (BR35UBOOT1.00).",
            file=sys.stderr,
        )
        return 1

    try:
        mfr = usb.util.get_string(dev, dev.iManufacturer) or ""
        prod = usb.util.get_string(dev, dev.iProduct) or ""
    except Exception:
        mfr, prod = "", ""

    if mfr.strip() == "IAP" or "V1.0.1" in prod:
        print(
            f"[enter_uboot] Gerät befindet sich im ATORCH HID-IAP-Bootloader ({mfr} / {prod}), "
            "nicht im Normalbetrieb (ATORCH T3 V2.0.4).\n"
            "  -> Für BR35UBOOT1.00 (flash-slot.ps1): USB kurz abziehen und ohne Tasten neu einstecken.\n"
            "  -> Oder direkt im IAP-Modus mit scripts\\atorch_iap_flash.py ein Voll-Image flashen.",
            file=sys.stderr,
        )
        return 5

    try:
        resp = bytes(
            dev.ctrl_transfer(
                BM_REQUEST_TYPE,
                B_REQUEST,
                W_VALUE,
                W_INDEX,
                8,
                timeout=2000,
            )
        )
    except (NotImplementedError, usb.core.USBError) as exc:
        print_zadig_help(exc)
        return 3

    print(f"[enter_uboot] Probe-Antwort: {resp.hex(' ')}")
    if resp != b"\xff" * 8:
        print(
            "[enter_uboot] Unerwartete Antwort (erwartet: 8x ff). Abbruch ohne Reboot.",
            file=sys.stderr,
        )
        return 4

    print("[enter_uboot] JieLi-Update-Hook bestätigt (8x 0xFF).")
    if not args.reboot:
        return 0

    print("[enter_uboot] Sende Reboot-Trigger (wLength=0 -> go_mask_usb_updata)...")
    try:
        dev.ctrl_transfer(BM_REQUEST_TYPE, B_REQUEST, W_VALUE, W_INDEX, 0, timeout=2000)
    except usb.core.USBError as exc:
        # Gerät resettet sich unmittelbar in den ROM-Bootloader, sodass Pipe/Disconnect normal sein kann
        print(f"[enter_uboot] USB-Reset während Trigger (erwartet beim Reboot): {exc}")
    finally:
        try:
            usb.util.dispose_resources(dev)
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
