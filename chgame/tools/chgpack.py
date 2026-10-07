#!/usr/bin/env python3
"""Make, check and list CHGame packages (.CHG) for the SD game menu.

A package is a 512-byte header plus the program image exactly as
chgame-upload would write it (docs/chg-format.md). Copy packages into the
card's GAMES folder; the menu shows the title from the header.

    python tools/chgpack.py pack build/release/CHFour.ino.bin FOURROW.CHG --title "FOUR IN A ROW"
    python tools/chgpack.py verify FOURROW.CHG [more.CHG ...]
    python tools/chgpack.py info E:\\              # a mounted card (or any folder)
    python tools/chgpack.py info card.img          # a FAT image: also reports fragmentation

Every constant mirrors platform/bootloader/shared/chg_format.h and
src/chgame_map.h; the bootloader's native tests check that they agree.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import zlib

MAGIC = 0x31474843          # "CHG1"
FORMAT_VERSION = 1
HEADER_BYTES = 512
TARGET_ID = 0x35335843      # "CX35": CHGame, CH32X035G8U6
LAYOUT_ID = 0x003000F7      # app at 0x3000, metadata page at 0xF700
APP_MAX_SIZE = 50944        # CHGAME_APP_MAX_SIZE
BOOT_SIG = 0x4C424843       # "CHBL" at payload offset 8 marks a bootloader image
TITLE_LEN, AUTHOR_LEN, VERSTR_LEN = 32, 16, 8

ERRORS = {1: "not a CHG package", 2: "header damaged", 3: "unknown format version",
          4: "built for another board or layout", 5: "bad payload size", 6: "payload damaged",
          7: "a bootloader image, not a program"}


class ChgError(Exception):
    def __init__(self, code: int, detail: str = ""):
        self.code = code
        super().__init__(ERRORS[code] + (f": {detail}" if detail else ""))


def _text(s: str, n: int, what: str) -> bytes:
    b = s.encode("ascii")
    if len(b) >= n or any(c < 32 or c > 126 for c in b):
        raise ValueError(f"{what} must be printable ASCII, at most {n - 1} characters")
    return b.ljust(n, b"\0")


def pad_image(image: bytes) -> bytes:
    """The chgame-upload rule: pad with 0xFF to a multiple of 4."""
    return image + b"\xff" * (-len(image) % 4)


def pack(image: bytes, title: str, author: str = "", version: str = "", app_version: int = 0) -> bytes:
    payload = pad_image(image)
    if not payload or len(payload) > APP_MAX_SIZE:
        raise ValueError(f"image is {len(payload)} B; the limit is {APP_MAX_SIZE} B")
    if len(payload) >= 12 and struct.unpack_from("<I", payload, 8)[0] == BOOT_SIG:
        raise ValueError("this is a bootloader image, not a program")
    h = bytearray(HEADER_BYTES)
    struct.pack_into("<IHHIIIIII", h, 0, MAGIC, FORMAT_VERSION, HEADER_BYTES, TARGET_ID, LAYOUT_ID,
                     len(payload), zlib.crc32(payload) & 0xFFFFFFFF, app_version & 0xFFFFFFFF, 0)
    h[0x20:0x40] = _text(title, TITLE_LEN, "title")
    h[0x40:0x50] = _text(author, AUTHOR_LEN, "author")
    h[0x50:0x58] = _text(version, VERSTR_LEN, "version")
    struct.pack_into("<I", h, 0x1FC, zlib.crc32(bytes(h[:0x1FC])) & 0xFFFFFFFF)
    return bytes(h) + payload


def _cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def parse(data: bytes, check_payload: bool = True) -> dict:
    """Checks a package the way the bootloader does (same order, same codes)."""
    if len(data) < HEADER_BYTES or struct.unpack_from("<I", data, 0)[0] != MAGIC:
        raise ChgError(1)
    h = data[:HEADER_BYTES]
    if zlib.crc32(h[:0x1FC]) & 0xFFFFFFFF != struct.unpack_from("<I", h, 0x1FC)[0]:
        raise ChgError(2)
    magic, ver, hb, target, layout, n, crc, appver, flags = struct.unpack_from("<IHHIIIIII", h, 0)
    if ver != FORMAT_VERSION or hb != HEADER_BYTES:
        raise ChgError(3, f"version {ver}, header {hb} B")
    if target != TARGET_ID or layout != LAYOUT_ID:
        raise ChgError(4, f"target 0x{target:08X}, layout 0x{layout:08X}")
    if not n or n > APP_MAX_SIZE or n & 3 or len(data) < HEADER_BYTES + n:
        raise ChgError(5, f"{n} B in a {len(data)} B file")
    info = {"title": _cstr(h[0x20:0x40]), "author": _cstr(h[0x40:0x50]), "version": _cstr(h[0x50:0x58]),
            "payload_bytes": n, "payload_crc32": crc, "app_version": appver, "file_bytes": len(data)}
    if check_payload:
        payload = data[HEADER_BYTES:HEADER_BYTES + n]
        if zlib.crc32(payload) & 0xFFFFFFFF != crc:
            raise ChgError(6)
        if struct.unpack_from("<I", payload, 8)[0] == BOOT_SIG:
            raise ChgError(7)
    return info


def _describe(name: str, data: bytes) -> tuple[bool, str]:
    try:
        i = parse(data)
    except ChgError as e:
        return False, f"{name:14s} BAD  {e}"
    extra = " ".join(x for x in (i["author"], i["version"]) if x)
    return True, f"{name:14s} ok   {i['payload_bytes']:6d} B  {i['title']}" + (f"  ({extra})" if extra else "")


def cmd_pack(a) -> int:
    image = pathlib.Path(a.bin).read_bytes()
    out = pack(image, a.title, a.author, a.version, a.app_version)
    pathlib.Path(a.out).write_bytes(out)
    print(f"{a.out}: {a.title!r}, {len(out) - HEADER_BYTES} B payload, crc 0x{zlib.crc32(out[HEADER_BYTES:]) & 0xFFFFFFFF:08X}")
    return 0


def cmd_verify(a) -> int:
    bad = 0
    for f in a.files:
        ok, line = _describe(pathlib.Path(f).name, pathlib.Path(f).read_bytes())
        print(line)
        bad += not ok
    return 1 if bad else 0


def cmd_info(a) -> int:
    p = pathlib.Path(a.where)
    bad = 0
    if p.is_dir():
        games = p / "GAMES" if (p / "GAMES").is_dir() else p
        files = sorted(f for f in games.iterdir() if f.suffix.upper() == ".CHG")
        print(f"{games}: {len(files)} package(s)")
        for f in files:
            ok, line = _describe(f.name, f.read_bytes())
            print("  " + line)
            bad += not ok
        return 1 if bad else 0
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "platform" / "board" / "arduino" / "CHGame" / "libraries" / "CHSd" / "tools"))
    import fatimg  # noqa: E402
    vol = fatimg.FatVolume(str(p))
    games, _, _ = vol.find("GAMES", is_dir=True)
    names = []
    for raw, attr, _, _ in vol.listdir(games):
        if attr & (fatimg.ATTR_DIR | fatimg.ATTR_HIDDEN | fatimg.ATTR_SYS) or raw[8:11] != b"CHG":
            continue
        base = raw[:8].decode("ascii", "replace").rstrip()
        names.append(base + ".CHG")
    print(f"{p}: {len(names)} package(s) in GAMES")
    for n in sorted(names):
        path = "GAMES/" + n
        runs = vol.runs(path)
        ok, line = _describe(n, vol.read_file(path))
        frag = f"  [{len(runs)} fragment{'s' if len(runs) != 1 else ''}]"
        print("  " + line + frag)
        bad += not ok
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack", help="wrap a program image in a package")
    p.add_argument("bin")
    p.add_argument("out")
    p.add_argument("--title", required=True, help="shown in the menu (31 characters at most; 20 fit on screen)")
    p.add_argument("--author", default="")
    p.add_argument("--version", default="")
    p.add_argument("--app-version", type=lambda s: int(s, 0), default=0)
    p.set_defaults(fn=cmd_pack)
    p = sub.add_parser("verify", help="check packages")
    p.add_argument("files", nargs="+")
    p.set_defaults(fn=cmd_verify)
    p = sub.add_parser("info", help="list the packages on a card, a folder or a FAT image")
    p.add_argument("where")
    p.set_defaults(fn=cmd_info)
    a = ap.parse_args(argv)
    try:
        return a.fn(a)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
