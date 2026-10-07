"""CHG packages for the SD game menu: `chgame-upload pack`.

A package is a 512-byte header plus the program image as `flash` would write
it (docs/chg-format.md in the CHGame repository). The
board package runs this after every build, so *Sketch > Export Compiled
Binary* leaves a `.chg` beside the `.bin`: copy it into the card's GAMES
folder and the menu lists it.

The constants mirror platform/bootloader/shared/chg_format.h, as
tools/chgpack.py does; test/protocol checks the three agree.
"""
from __future__ import annotations

import os
import struct
import zlib

from . import layout as L

MAGIC = 0x31474843          # "CHG1"
FORMAT_VERSION = 1
HEADER_BYTES = 512
TARGET_ID = 0x35335843      # "CX35": CHGame, CH32X035G8U6
LAYOUT_ID = 0x003000F7      # app at 0x3000, metadata page at 0xF700
BOOT_SIG = 0x4C424843       # "CHBL" at payload offset 8 marks a bootloader image
TITLE_LEN, AUTHOR_LEN, VERSTR_LEN = 32, 16, 8


def _text(s: str, n: int, what: str) -> bytes:
    b = s.encode("ascii", "replace")
    if len(b) >= n or any(c < 32 or c > 126 for c in b):
        raise ValueError(f"{what} must be printable ASCII, at most {n - 1} characters")
    return b.ljust(n, b"\0")


def default_title(path: str) -> str:
    """`MyGame.ino.bin` -> `MYGAME`: the sketch's name, as the menu's capitals."""
    base = os.path.basename(path).split(".", 1)[0]
    return base.upper()[:TITLE_LEN - 1] or "PROGRAM"


def default_output(path: str) -> str:
    """`MyGame.ino.bin` -> `MyGame.ino.chg`, beside it (Export Compiled Binary
    copies every `<project>.*` file from the build folder)."""
    root, ext = os.path.splitext(path)
    return (root if ext.lower() == ".bin" else path) + ".chg"


def pack(image: bytes, title: str, author: str = "", version: str = "", app_version: int = 0) -> bytes:
    payload = image + bytes([L.ERASED]) * (-len(image) % 4)
    if not payload or len(payload) > L.APP_MAX_SIZE:
        raise ValueError(f"image is {len(payload)} B; the limit is {L.APP_MAX_SIZE} B")
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
