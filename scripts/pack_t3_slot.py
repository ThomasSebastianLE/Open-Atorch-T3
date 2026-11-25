#!/usr/bin/env python3
"""
Pack & scramble an AC707N `app.bin` into a flashable 0x5000..0x138000 slot image
for the ATORCH / JUWEI T3 using `T3 Firmware Version-UP 2.0.4.bin` (read-only) as
the `app_dir_head` template.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_TEMPLATE = REPO_ROOT / "T3 Firmware Version-UP 2.0.4.bin"

APP_SLOT_OFFSET = 0x5000
APP_SLOT_MAX_SIZE = 0x133000  # 0x138000 - 0x5000
APP_HEAD_SIZE = 0x100
APP_SCRAMBLE_KEY = 0x8D07
ENTRY_FMT = "<HHIIBBH16s"


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def jl_lfsr_crypt(data: bytes, key: int) -> bytes:
    s = key & 0xFFFF
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ (s & 0xFF)
        s = (((s << 1) ^ 0x1021) & 0xFFFF) if (s & 0x8000) else ((s << 1) & 0xFFFF)
    return bytes(out)


def descramble_app_slot(scrambled: bytes) -> bytes:
    out = bytearray()
    for rel in range(0, len(scrambled), 32):
        chunk = scrambled[rel : rel + 32]
        out.extend(jl_lfsr_crypt(chunk, (APP_SCRAMBLE_KEY ^ (rel >> 2)) & 0xFFFF))
    return bytes(out)


def scramble_app_slot(plain: bytes) -> bytes:
    if len(plain) % 32 != 0:
        raise ValueError(f"Plain slot length {len(plain)} must be 32-byte aligned")
    out = bytearray()
    for rel in range(0, len(plain), 32):
        chunk = plain[rel : rel + 32]
        out.extend(jl_lfsr_crypt(chunk, (APP_SCRAMBLE_KEY ^ (rel >> 2)) & 0xFFFF))
    return bytes(out)


def verify_plain_slot(plain: bytes, label: str = "slot") -> tuple[int, int]:
    if len(plain) < APP_HEAD_SIZE:
        raise ValueError(f"{label}: too small ({len(plain)} B)")

    hcrc0, dcrc0, addr0, len0, attr0, res0, idx0, name0 = struct.unpack_from(ENTRY_FMT, plain, 0)
    hcrc1, dcrc1, addr1, len1, attr1, res1, idx1, name1 = struct.unpack_from(ENTRY_FMT, plain, 32)

    if crc16_xmodem(plain[2:32]) != hcrc0:
        raise ValueError(f"{label}: entry[0] hcrc mismatch")
    if crc16_xmodem(plain[34:64]) != hcrc1:
        raise ValueError(f"{label}: entry[1] hcrc mismatch")
    if addr1 != APP_HEAD_SIZE:
        raise ValueError(f"{label}: unexpected entry[1] addr 0x{addr1:x}")
    if len0 != APP_HEAD_SIZE + len1:
        raise ValueError(f"{label}: entry[0] len 0x{len0:x} != 0x100 + 0x{len1:x}")

    app_bytes = plain[APP_HEAD_SIZE : APP_HEAD_SIZE + len1]
    if len(app_bytes) != len1:
        raise ValueError(f"{label}: truncated app.bin ({len(app_bytes)} != {len1})")

    calc_dcrc1 = crc16_xmodem(app_bytes)
    if calc_dcrc1 != dcrc1:
        raise ValueError(f"{label}: entry[1] dcrc 0x{calc_dcrc1:04x} != 0x{dcrc1:04x}")

    calc_dcrc0 = crc16_xmodem(plain[0x20 : APP_HEAD_SIZE + len1])
    if calc_dcrc0 != dcrc0:
        raise ValueError(f"{label}: entry[0] dcrc 0x{calc_dcrc0:04x} != 0x{dcrc0:04x}")

    return len1, dcrc1


def self_test(template_path: pathlib.Path) -> None:
    raw = template_path.read_bytes()
    if len(raw) < APP_SLOT_OFFSET + 0x100:
        raise ValueError(f"Template too short: {template_path}")
    plain = descramble_app_slot(raw[APP_SLOT_OFFSET:])
    app_len, dcrc1 = verify_plain_slot(plain, label=template_path.name)
    rescrambled = scramble_app_slot(plain)
    if rescrambled != raw[APP_SLOT_OFFSET : APP_SLOT_OFFSET + len(rescrambled)]:
        raise AssertionError("Round-trip scramble mismatch against stock firmware!")
    print(f"[pack_t3_slot] Self-test OK: {template_path.name} (app_len=0x{app_len:x}, app dcrc=0x{dcrc1:04x})")


def pack_slot(app_path: pathlib.Path, template_path: pathlib.Path, out_path: pathlib.Path) -> None:
    app_bin = app_path.read_bytes()
    app_len = len(app_bin)
    if app_len == 0:
        raise ValueError(f"Empty app binary: {app_path}")
    if APP_HEAD_SIZE + app_len > APP_SLOT_MAX_SIZE:
        raise ValueError(
            f"app.bin too large for 0x5000..0x138000 slot: "
            f"0x{APP_HEAD_SIZE + app_len:x} > 0x{APP_SLOT_MAX_SIZE:x}"
        )

    tmpl_raw = template_path.read_bytes()
    tmpl_plain = descramble_app_slot(tmpl_raw[APP_SLOT_OFFSET:])
    tmpl_app_len, _ = verify_plain_slot(tmpl_plain, label=template_path.name)
    tmpl_head = bytearray(tmpl_plain[0:APP_HEAD_SIZE])
    res_blob = tmpl_plain[APP_HEAD_SIZE + tmpl_app_len :]
    # Trim trailing 0xFF padding from resource blob so we only embed the actual SDFILE payload
    res_end = len(res_blob)
    while res_end > 0 and res_blob[res_end - 1] == 0xFF:
        res_end -= 1
    res_end = ((res_end + 31) // 32) * 32
    res_blob = res_blob[:res_end]

    used_app = APP_HEAD_SIZE + app_len
    used_total = used_app + len(res_blob)
    if used_total > APP_SLOT_MAX_SIZE:
        raise ValueError(
            f"app.bin + resource SDFILE too large for 0x5000..0x138000 slot: "
            f"0x{used_total:x} > 0x{APP_SLOT_MAX_SIZE:x}"
        )

    # Align total payload cleanly to 4 KiB sector boundary (or max slot size) padded with 0xFF
    padded_size = ((used_total + 4095) // 4096) * 4096
    if padded_size > APP_SLOT_MAX_SIZE:
        padded_size = ((used_total + 31) // 32) * 32
    if padded_size > APP_SLOT_MAX_SIZE:
        raise ValueError(f"Aligned slot {padded_size} > {APP_SLOT_MAX_SIZE}")

    plain = bytearray(b"\xFF" * padded_size)
    plain[0:APP_HEAD_SIZE] = tmpl_head
    plain[APP_HEAD_SIZE : APP_HEAD_SIZE + app_len] = app_bin
    if res_blob:
        plain[used_app : used_app + len(res_blob)] = res_blob

    # Update entry[1] ("app.bin")
    _, _, addr1, _, attr1, res1, idx1, name1 = struct.unpack_from(ENTRY_FMT, plain, 32)
    dcrc1 = crc16_xmodem(app_bin)
    ent1_wo_hcrc = struct.pack("<HIIBBH16s", dcrc1, addr1, app_len, attr1, res1, idx1, name1)
    hcrc1 = crc16_xmodem(ent1_wo_hcrc)
    struct.pack_into("<H", plain, 32, hcrc1)
    plain[34:64] = ent1_wo_hcrc

    # Update entry[0] ("app_area_head")
    _, _, addr0, _, attr0, res0, idx0, name0 = struct.unpack_from(ENTRY_FMT, plain, 0)
    dcrc0 = crc16_xmodem(bytes(plain[0x20 : APP_HEAD_SIZE + app_len]))
    ent0_wo_hcrc = struct.pack("<HIIBBH16s", dcrc0, addr0, APP_HEAD_SIZE + app_len, attr0, res0, idx0, name0)
    hcrc0 = crc16_xmodem(ent0_wo_hcrc)
    struct.pack_into("<H", plain, 0, hcrc0)
    plain[2:32] = ent0_wo_hcrc

    scrambled = scramble_app_slot(bytes(plain))
    roundtrip = descramble_app_slot(scrambled)
    verify_plain_slot(roundtrip, label=out_path.name)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(scrambled)
    pct = (used_total * 100.0) / APP_SLOT_MAX_SIZE
    print(
        f"[pack_t3_slot] Packed {out_path} | app=0x{app_len:x} ({app_len} B) | "
        f"res=0x{len(res_blob):x} ({len(res_blob)} B) | "
        f"slot=0x{len(scrambled):x} ({pct:.1f}% of 0x{APP_SLOT_MAX_SIZE:x}) | "
        f"dcrc0=0x{dcrc0:04x} dcrc1=0x{dcrc1:04x}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack AC707N app.bin for T3 0x5000 slot.")
    parser.add_argument("--app", type=pathlib.Path, help="Input app.bin path")
    parser.add_argument("--template", type=pathlib.Path, default=DEFAULT_TEMPLATE, help="OEM firmware template")
    parser.add_argument("--out", type=pathlib.Path, help="Output scrambled slot binary")
    parser.add_argument("--self-test", action="store_true", help="Verify pack/unpack logic against OEM template")
    args = parser.parse_args()

    if args.self_test:
        self_test(args.template)
        if not (args.app and args.out):
            return 0

    if not args.app or not args.out:
        parser.error("--app and --out are required unless only --self-test is given")

    pack_slot(args.app, args.template, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
