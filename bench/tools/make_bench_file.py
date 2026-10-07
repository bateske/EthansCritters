"""Write ECBench's card file: out/bench/BENCH.DAT (32 MB by default).

    python bench/tools/make_bench_file.py [--mb N] [--out PATH] [--check]

Copy it to the root of the board's card as BENCH.DAT (8.3 name); in the
simulator ECBench's tools/chsim/chdrive.py hands it to the card itself.

Layout, so that any block can be checked where it lands (ECBench's B2-B4
and B8 verify every block they read):
  block 0       the header: "ECBN" "DAT1", then u32 blocks (the file's
                length in blocks, header included), u32 megabytes; zeros
  block i >= 1  word 0 = i; words 1..127 = xorshift32 (13, 17, 5) run from
                the seed i * 2654435761 + 1 (mod 2^32), each word the state
                after one more step. Little-endian u32s.
The same generator is in bench/ECBench/Report.cpp (patternOk).

numpy, when installed, makes 32 MB a second's work; without it this takes
a minute or so.
"""
import argparse
import struct
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
DEFAULT = PROJECT / "out" / "bench" / "BENCH.DAT"
K = 2654435761
MAGIC = b"ECBNDAT1"
WORDS = 128


def seed(i):
    s = (i * K + 1) & 0xFFFFFFFF
    return s or 1


def block_py(i):
    x = seed(i)
    out = [i]
    for _ in range(WORDS - 1):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.append(x)
    return struct.pack("<128I", *out)


def header(blocks, mb):
    h = MAGIC + struct.pack("<II", blocks, mb)
    return h + bytes(512 - len(h))


def blocks_np(first, count):
    import numpy as np
    idx = np.arange(first, first + count, dtype=np.uint64)
    x = ((idx * K + 1) & 0xFFFFFFFF).astype(np.uint32)
    x[x == 0] = 1
    out = np.empty((count, WORDS), dtype="<u4")
    out[:, 0] = idx.astype(np.uint32)
    for w in range(1, WORDS):
        x ^= x << np.uint32(13)
        x ^= x >> np.uint32(17)
        x ^= x << np.uint32(5)
        out[:, w] = x
    return out.tobytes()


def write(path, mb):
    blocks = mb * 2048
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        import numpy  # noqa: F401
        have_np = True
    except ImportError:
        have_np = False
    with open(path, "wb") as f:
        f.write(header(blocks, mb))
        step = 4096
        for first in range(1, blocks, step):
            count = min(step, blocks - first)
            if have_np:
                f.write(blocks_np(first, count))
            else:
                f.write(b"".join(block_py(i) for i in range(first, first + count)))
    return blocks


def check(path, samples=64):
    data = Path(path).read_bytes()
    assert data[:8] == MAGIC, "bad magic"
    blocks, mb = struct.unpack_from("<II", data, 8)
    assert len(data) == blocks * 512, "length does not match the header"
    for j in range(samples):
        i = 1 + (j * 104729) % (blocks - 1)
        assert data[i * 512:(i + 1) * 512] == block_py(i), f"block {i} differs"
    return blocks, mb


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mb", type=int, default=32, help="size in MB (1-1024, default 32)")
    ap.add_argument("--out", type=Path, default=DEFAULT)
    ap.add_argument("--check", action="store_true", help="only check an existing file against the generator")
    a = ap.parse_args(argv)
    if a.check:
        blocks, mb = check(a.out)
        print(f"{a.out}: ok, {blocks} blocks ({mb} MB)")
        return 0
    if not 1 <= a.mb <= 1024:
        ap.error("--mb: 1 to 1024")
    blocks = write(a.out, a.mb)
    check(a.out)
    print(f"{a.out}: {blocks} blocks ({a.mb} MB); copy it to the card's root as BENCH.DAT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
