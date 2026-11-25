"""Build, upload and drive a CHGame sketch on the attached board.

    chgame build [--debug]                 compile (release by default)
    chgame upload [--debug] [--arduino]    compile + flash
    chgame run --device SCRIPT OUTDIR      debug build, upload, run a chdrive script
    chgame shot OUT.png                    screenshot of a running debug build

(or `python tools/device.py [--sketch DIR] build|upload|run|shot`, the same
without the entry point). The sketch is DIR (a folder, or the name of a
game or app: CHFour, CHSDtoUSB), else the current folder.

Builds use the CHGfx, CHGame and CHSd libraries in
platform/board/arduino/CHGame/libraries (the copies the simulator uses
too). Both use opt=oslto (Tools > Optimize > "Smallest + LTO": -Os -flto,
about 3.9 KB smaller than plain -Os) and periph=game (the default
Peripherals setting). Release builds add usb=uploadonly (Tools > USB >
"Upload only": compiles out Serial, which release code never uses, but
keeps the 1200-baud upload handshake, so uploading still needs no button
press). Debug builds keep USB Serial, which the debug protocol talks over,
and turn the protocol on with -DCHGAME_DEBUG=1 in build.extra_flags (empty
on this platform). A build ends with tools/check_size.py's report: flash,
the image against the save pages, and RAM.

Uploads go through the Python uploader (platform/bootloader/host/py), which
does the 1200-baud touch, the flash, the verify and the restart itself;
`--arduino` uses `arduino-cli upload` and the board package's Go tool
instead, the path an IDE user takes. `run` and `shot` talk to the sketch
through its tools/chsim/chdrive.py. Needs the CHGame board package 0.2.4+.

The board (Ethan's Critters patch P6, from upstream's Rev1 work): `--board
rev0|rev1`, else $CHGAME_BOARD, else rev0. A build goes to
<sketch>/build/<board>/release (or debug), upstream's layout, which its
`chgame export --no-build` reads. rev1 needs board package 0.4.0, and its
uploads always go through `arduino-cli upload` with the rev1 FQBN, so the
board package passes `-device rev1` and the wrong board is refused.
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
import paths  # noqa: E402

LIBRARIES = paths.LIBRARIES
import os  # noqa: E402

BOARDS = ("rev0", "rev1")
FQBN_DEBUG = "CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game"
FQBN_RELEASE = FQBN_DEBUG + ",usb=uploadonly"


def board(b=None):
    """`b`, else $CHGAME_BOARD, else rev0."""
    b = b or os.environ.get("CHGAME_BOARD") or "rev0"
    if b not in BOARDS:
        raise SystemExit(f"{b}: not a board (the boards: {', '.join(BOARDS)})")
    return b


def fqbn(debug=False, b=None):
    """The FQBN for `b`: rev0's with the board swapped, options kept."""
    f = FQBN_DEBUG if debug else FQBN_RELEASE
    return f.replace(":rev0:", f":{board(b)}:", 1)


def build_dir(sketch, debug=False, b=None):
    """Where a build goes: <sketch>/build/<board>/<release|debug>."""
    return paths.sketch(sketch) / "build" / board(b) / ("debug" if debug else "release")


def build(sketch, debug=False, flags="", b=None):
    """Compile for `b` (board()); returns the build folder (build_dir())."""
    sketch = paths.sketch(sketch)
    out = build_dir(sketch, debug, b)
    cmd = ["arduino-cli", "compile", "-b", fqbn(debug, b), "--build-path", str(out)]
    extra = ("-DCHGAME_DEBUG=1 " if debug else "") + flags
    if extra.strip():
        cmd += ["--build-property", "build.extra_flags=" + extra.strip()]
    # All three every time: Arduino links only the ones a sketch includes.
    for lib in ("CHGfx", "CHGame", "CHSd"):
        cmd += ["--library", str(LIBRARIES / lib)]
    cmd.append(str(sketch))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout[-3000:] + r.stderr[-3000:])
        raise SystemExit("compile failed")
    subprocess.run([sys.executable, str(REPO / "tools" / "check_size.py"), str(out), "--top", "0"])
    return out


def image(sketch, out):
    """The .bin arduino-cli wrote in the build folder."""
    return Path(out) / f"{paths.sketch(sketch).name}.ino.bin"


def upload(sketch, out, port=None, arduino=False, b=None):
    sketch = paths.sketch(sketch)
    arduino = arduino or board(b) != "rev0"    # the board package checks the board (-device)
    from serialcap import find_port
    port = port or find_port()
    if not port:
        raise SystemExit("no CHGame found on USB (VID 16C0:27DD): plug it in, or pass --port")
    if arduino:
        r = subprocess.run(["arduino-cli", "upload", "-b", fqbn(False, b), "-p", port,
                            "--input-dir", str(out), str(sketch)], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("upload failed")
        print(r.stdout.strip().splitlines()[-1])
        return
    from chgame_upload.upload import flash_file
    try:
        flash_file(image(sketch, out), port=port, run=True)
    except Exception as e:                  # NoDevice, StatusError, TimeoutError ...
        raise SystemExit(f"upload failed: {e}")


def run(sketch, script, outdir, port=None, flags=""):
    """Debug build, upload, then the sketch's chdrive.py --device on the script."""
    sketch = paths.sketch(sketch)
    upload(sketch, build(sketch, True, flags), port)
    chdrive = sketch / "tools" / "chsim" / "chdrive.py"
    if not chdrive.exists():
        chdrive = REPO / "tools" / "chsim" / "chdrive.py"
    cmd = [sys.executable, str(chdrive), "--device", str(script), str(outdir)]
    if port:
        cmd[3:3] = ["--port", port]
    return subprocess.run(cmd, cwd=sketch).returncode


def shot(sketch, out, port=None):
    sketch = paths.sketch(sketch)
    sys.path.insert(0, str(REPO / "tools" / "chsim"))
    from chdrivelib import Driver, SerialTransport
    from fbimage import to_image
    d = Driver(SerialTransport(port))
    d.handshake()
    to_image(d.shot(), 3).save(out)
    print(out)


def main(sketch=None, flags=""):
    """flags: extra build.extra_flags for this sketch (both builds)."""
    ap = argparse.ArgumentParser()
    if sketch is None:
        ap.add_argument("--sketch", type=Path, default=Path.cwd(), help="the sketch's folder")
    ap.add_argument("--board", choices=BOARDS, help="the board (default: $CHGAME_BOARD, else rev0)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("build", "upload"):
        p = sub.add_parser(name)
        p.add_argument("--debug", action="store_true")
        p.add_argument("--port")
        if name == "upload":
            p.add_argument("--arduino", action="store_true", help="through arduino-cli and the Go tool")
    p = sub.add_parser("run")
    p.add_argument("script")
    p.add_argument("outdir")
    p.add_argument("--port")
    p = sub.add_parser("shot")
    p.add_argument("out")
    p.add_argument("--port")
    a = ap.parse_args()
    if a.board:
        os.environ["CHGAME_BOARD"] = a.board
    sketch = paths.sketch(sketch or a.sketch)
    if a.cmd == "build":
        build(sketch, a.debug, flags)
    elif a.cmd == "upload":
        upload(sketch, build(sketch, a.debug, flags), a.port, a.arduino)
    elif a.cmd == "run":
        raise SystemExit(run(sketch, a.script, a.outdir, a.port, flags))
    elif a.cmd == "shot":
        shot(sketch, a.out, a.port)


if __name__ == "__main__":
    main()
