"""Copy the parts of the CHGame repository this project needs into chgame/.

    python tools/vendor_chgame.py <CHGame checkout> [--force]

The copy keeps the repository's relative layout (tools/ beside platform/),
so the shared tools resolve their paths exactly as they do upstream and run
unchanged. Only the paths listed in SUBSET are touched: chgame/VENDORED.md is
rewritten, while chgame/PATCHES.md and chgame/patches/ (our changes to the
libraries, kept for upstreaming) are left alone.

Re-vendoring over patched files would lose the patches, so it refuses when
chgame/PATCHES.md lists any unless --force is given (then re-apply them from
chgame/patches/ afterwards).
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "chgame"
LIBS = "platform/board/arduino/CHGame/libraries"

# Folders are copied whole (minus build output), files one by one.
SUBSET = [
    "LICENSE", "NOTICE",
    "tools/LICENSE", "tools/NOTICE", "tools/README.md", "tools/requirements.txt",
    "tools/chgame.py", "tools/paths.py", "tools/device.py", "tools/check.py", "tools/check_size.py",
    "tools/gamecfg.py", "tools/hosttests.py", "tools/readme_gif.py", "tools/serialcap.py", "tools/chgpack.py",
    "tools/chsim", "tools/audio",
    f"{LIBS}/CHGame/src", f"{LIBS}/CHGame/library.properties", f"{LIBS}/CHGame/LICENSE",
    f"{LIBS}/CHGame/NOTICE", f"{LIBS}/CHGame/README.md", f"{LIBS}/CHGame/examples/Hello",
    f"{LIBS}/CHGfx/src", f"{LIBS}/CHGfx/extras", f"{LIBS}/CHGfx/library.properties", f"{LIBS}/CHGfx/LICENSE",
    f"{LIBS}/CHGfx/LICENSE.Apache-2.0", f"{LIBS}/CHGfx/README.md", f"{LIBS}/CHGfx/FONTS.md",
    f"{LIBS}/CHGfx/keywords.txt",
    f"{LIBS}/CHSd/src", f"{LIBS}/CHSd/host", f"{LIBS}/CHSd/tools", f"{LIBS}/CHSd/tests",
    f"{LIBS}/CHSd/library.properties", f"{LIBS}/CHSd/LICENSE", f"{LIBS}/CHSd/NOTICE", f"{LIBS}/CHSd/README.md",
    "platform/bootloader/host/py/chgame_upload", "platform/bootloader/host/py/pyproject.toml",
    "platform/bootloader/LICENSE",
]
IGNORE = shutil.ignore_patterns("build", "__pycache__", "*.pyc", "out")


def git(src, *args):
    return subprocess.run(["git", "-C", str(src), *args], capture_output=True, text=True).stdout.strip()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("checkout", type=Path)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    src = a.checkout.resolve()
    if not (src / "tools" / "chsim" / "chsim.py").exists():
        raise SystemExit(f"{src}: not a CHGame checkout")
    patches = DEST / "PATCHES.md"
    if patches.exists() and "| P" in patches.read_text(encoding="utf-8") and not a.force:
        raise SystemExit("chgame/PATCHES.md lists patches: re-vendoring would drop them (use --force, then re-apply)")

    for rel in SUBSET:
        s, d = src / rel, DEST / rel
        if not s.exists():
            raise SystemExit(f"missing upstream: {rel}")
        if d.is_dir():
            shutil.rmtree(d)
        elif d.exists():
            d.unlink()
        d.parent.mkdir(parents=True, exist_ok=True)
        if s.is_dir():
            shutil.copytree(s, d, ignore=IGNORE)
        else:
            shutil.copy2(s, d)

    commit = git(src, "rev-parse", "HEAD")
    date = git(src, "log", "-1", "--format=%cI")
    subject = git(src, "log", "-1", "--format=%s")
    lines = [
        "# Vendored from bateske/CHGame",
        "",
        f"- Upstream: https://github.com/bateske/CHGame",
        f"- Commit: `{commit}` ({date})",
        f"- Subject: {subject}",
        "",
        "A subset of the repository, with its relative layout kept so the shared",
        "tools (`tools/chgame.py`, `tools/chsim`, ...) find the libraries the way",
        "they do upstream. Copied by `tools/vendor_chgame.py`; do not edit these",
        "files except as one of the patches listed in [PATCHES.md](PATCHES.md).",
        "",
        "Copied paths:",
        "",
        *[f"- `{rel}`" for rel in SUBSET],
        "",
    ]
    (DEST / "VENDORED.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"vendored {len(SUBSET)} paths from {commit[:8]} into {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
