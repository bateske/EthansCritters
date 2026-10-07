"""Put files on the board's SD card over USB, without taking the card out.

    python tools/card_sync.py [FILE ...] [--then release|debug|none]

Uploads the CHGame library's CHSDtoUSB app (from out/upstream, the full
CHGame checkout) so the board becomes a USB card reader, waits for its drive,
copies the files to the card's root (default: out/card/CRITTERS.DAT), checks
each copy, ejects the drive so Windows flushes it, then uploads the game
again (`--then release`, the default; `debug` for a debug build; `none`
leaves CHSDtoUSB running). Only copies: nothing on the card is deleted.

Windows only (the drive is found and ejected through PowerShell). Follows
CLAUDE.md's device rules: it says what it is about to do, and finds the
board by its USB id instead of opening ports.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "out" / "upstream" / "platform" / "board" / "arduino" / "CHGame" / "libraries" / "CHGame" / "examples" / "Apps" / "CHSDtoUSB"
BUILD = ROOT / "out" / "sdtousb_build"
FQBN = "CHGame:ch32v:rev0:opt=osstd"          # CHSDtoUSB is tested with -Os, not LTO (its README)
VIDPID = (0x16C0, 0x27DD)


def ps(script):
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True)
    return r.stdout.strip()


def board_port():
    import serial.tools.list_ports as lp
    for p in lp.comports():
        if (p.vid, p.pid) == VIDPID:
            return p.device
    return None


def reader_drive():
    """The drive letter of the CHSDtoUSB card reader's volume, or None."""
    out = ps("Get-Disk | Where-Object { $_.FriendlyName -like '*CHGame SD Card Reader*' } | "
             "Get-Partition | Where-Object { $_.DriveLetter } | Select-Object -ExpandProperty DriveLetter")
    letters = [c for c in out.split() if len(c) == 1 and c.isalpha()]
    return letters[0] if letters else None


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def upload_reader(port):
    if not APP.is_dir():
        raise SystemExit(f"{APP}: missing (clone bateske/CHGame into out/upstream)")
    print(f"card_sync: building and uploading CHSDtoUSB to {port} (the board becomes a card reader)", flush=True)
    for cmd in (["arduino-cli", "compile", "-b", FQBN, "--build-path", str(BUILD), str(APP)],
                ["arduino-cli", "upload", "-b", FQBN, "-p", port, "--input-dir", str(BUILD), str(APP)]):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:])
            raise SystemExit("card_sync: " + cmd[1] + " failed")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", type=Path, default=[ROOT / "out" / "card" / "CRITTERS.DAT"])
    ap.add_argument("--then", choices=("release", "debug", "none"), default="release")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for the drive")
    a = ap.parse_args(argv)
    for f in a.files:
        if not f.is_file():
            raise SystemExit(f"card_sync: {f}: no such file")

    drive = reader_drive()
    if not drive:
        port = board_port()
        if not port:
            raise SystemExit("card_sync: no CHGame on USB (16C0:27DD)")
        upload_reader(port)
        t0 = time.time()
        while not drive and time.time() - t0 < a.timeout:
            time.sleep(1)
            drive = reader_drive()
        if not drive:
            raise SystemExit("card_sync: the card reader's drive did not appear (is a card in the slot?)")
    root = Path(f"{drive}:/")
    print(f"card_sync: card at {drive}: ({shutil.disk_usage(root).free // (1 << 20)} MB free)", flush=True)

    for f in a.files:
        dst = root / f.name
        print(f"card_sync: copying {f.name} ({f.stat().st_size:,} B)", flush=True)
        shutil.copyfile(f, dst)
        if sha(dst) != sha(f):
            raise SystemExit(f"card_sync: {dst} does not match {f} after copying")

    # Flush, then eject. The shell carries the eject out after InvokeVerb
    # returns, and abandons it if the PowerShell that asked has exited, so
    # that process waits for it; try a few times, and take the volume being
    # gone (not the partition's letter) as the proof.
    print("card_sync: flushing and ejecting", flush=True)
    ps(f"Write-VolumeCache -DriveLetter {drive}")
    for _ in range(5):
        ps(f"(New-Object -ComObject Shell.Application).Namespace(17).ParseName('{drive}:\\').InvokeVerb('Eject'); "
           f"for ($i = 0; $i -lt 20 -and (Test-Path '{drive}:\\'); $i++) {{ Start-Sleep -Milliseconds 250 }}")
        if not root.exists():
            break
    if root.exists():
        raise SystemExit(f"card_sync: {drive}: did not eject; eject it by hand before uploading")

    if a.then != "none":
        print(f"card_sync: uploading the game ({a.then} build)", flush=True)
        cmd = [sys.executable, str(ROOT / "chgame" / "tools" / "chgame.py"), "--sketch", str(ROOT / "EthansCritters"),
               "upload"] + (["--debug"] if a.then == "debug" else [])
        return subprocess.run(cmd).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
