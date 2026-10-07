"""Drive ECBench - in the simulator or on the board - with a script.

    python chgame/tools/chgame.py --sketch bench/ECBench run tools/scripts/all.txt out/bench_all
    python bench/ECBench/tools/chsim/chdrive.py --device [--port COMx] <script> <outdir>

(`chgame run --device` builds the debug image, uploads it and runs this with
--device.) The CHGame tools' tools/chsim/chdrivelib.py does the driving and
has the common script commands (wait, tap, snap, say, perf ...). ECBench adds:
    bench Z<n> [args]   run a benchmark (the sketch's Z commands, ECBench.ino)
                        and keep its "ECBN ..." lines: printed, and appended
                        to <outdir>/results.txt
    summary             the kept lines as tables, printed and written to
                        <outdir>/summary.txt
In the simulator the card is $CHSD_CARD, or else out/bench/BENCH.DAT at the
project root (bench/tools/make_bench_file.py makes it when it is missing).
The simulator's card timing comes from $CHSD_SIM_* (CHSd's host/sd_host.cpp).
"""
import os
import subprocess
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parents[2]
PROJECT = SKETCH.parents[1]


def _tools():
    """The CHGame tools: $CHGAME_ROOT/tools, else chgame/tools (or tools/) above this sketch."""
    root = os.environ.get("CHGAME_ROOT")
    cands = [Path(root) / "tools"] if root else []
    for up in Path(__file__).resolve().parents:
        cands += [up / "chgame" / "tools", up / "tools"]
    for c in cands:
        if (c / "chsim" / "chdrivelib.py").exists():
            return c
    raise SystemExit(f"{Path(__file__).name}: the CHGame tools (chgame/tools/chsim/chdrivelib.py) were not found; "
                     "set CHGAME_ROOT to a CHGame checkout")


sys.path.insert(0, str(_tools() / "chsim"))
from chdrivelib import Driver, SerialTransport, SimTransport, main, mask_of  # noqa: E402,F401


def fields(line):
    """'ECBN B2 mode=rnd n=1 p50=770' -> ('B2', {'mode': 'rnd', 'n': '1', 'p50': '770'})"""
    words = line.split()[1:]
    tag = " ".join(w for w in words if "=" not in w)
    return tag, dict(w.split("=", 1) for w in words if "=" in w)


class BenchDriver(Driver):
    lines = None

    def op(self, name, args, outdir):
        if name == "bench":
            self.bench(" ".join(args), Path(outdir))
            return True
        if name == "summary":
            text = summary(self.lines or [])
            print(text, flush=True)
            (Path(outdir) / "summary.txt").write_text(text + "\n", encoding="utf-8", newline="\n")
            return True
        return False

    def bench(self, command, outdir):
        if self.lines is None:
            self.lines = []
            (outdir / "results.txt").write_text("", encoding="utf-8")
        self.t.send(command)
        got = []
        for _ in range(100000):
            line = self.t.readline()
            if line.startswith("HELD") or (line.startswith("OK ") and line[3:].strip().isdigit()):
                self.t.send("N 1")             # a frame ack (lockstep), still waiting
                continue
            if line.startswith("OK"):
                break
            if line.startswith("ERR"):
                raise SystemExit(f"ECBench refused: {command}")
            print(line, flush=True)
            if line.startswith("ECBN "):
                got.append(line)
        self.lines += got
        with open(outdir / "results.txt", "a", encoding="utf-8", newline="\n") as f:
            f.write("".join(g + "\n" for g in got))


def summary(lines):
    """The results as tables (the last run of each benchmark wins)."""
    out = []
    by = {}
    for ln in lines:
        tag, kv = fields(ln)
        by.setdefault(tag, []).append(kv)

    def last(tag):
        return (by.get(tag) or [{}])[-1]

    b1 = [kv for kv in by.get("B1", [])]
    if b1:
        a = [kv for kv in b1 if "init_us" in kv]
        t = [kv for kv in b1 if "type" in kv]
        for kv in a:
            out.append(f"B1 {kv['when']:5}: card={kv['card']} init={kv['init_us']} us, mount={kv['mount_us']} us, "
                       f"find+runs={kv['open_us']} us, header={kv['header_us']} us, runs={kv['runs']}, "
                       f"blocks={kv['blocks']}")
        if t:
            kv = t[-1]
            out.append(f"   card: {kv['type']}, maker {kv['mid']}, product {kv['product']}, {kv['card_mb']} MB")
    mbs = by.get("B3 mb", [])
    for b3 in by.get("B3", []):
        if "mbs" not in b3:
            continue
        out.append(f"B3 {b3['when']}: {b3['mbs']} MB, warm {b3['warm_us']} us; first reads p50 {b3['p50']} "
                   f"p95 {b3['p95']} max {b3['max']} us, {b3['over10ms']} over 10 ms, fail {b3['fail']}, "
                   f"bad {b3['bad']}")
        these, mbs = mbs[:int(b3["mbs"])], mbs[int(b3["mbs"]):]
        out.append("   us by MB: " + " ".join(kv["us"] + ("" if kv["ok"] == "1" else "!") for kv in these))
    b2 = by.get("B2", [])
    rows = [kv for kv in b2 if "p50" in kv]
    if rows:
        out.append("B2 stream (us)     random offsets               warm 128 KB window")
        out.append("     n      p50     p95     max  err      p50     p95     max  err")
        rnd = {kv["n"]: kv for kv in rows if kv["mode"] == "rnd"}
        warm = {kv["n"]: kv for kv in rows if kv["mode"] == "warm"}
        for n in ("1", "2", "4", "8", "17", "32"):
            r, w = rnd.get(n, {}), warm.get(n, {})

            def cols(kv):
                if not kv:
                    return " " * 33
                err = int(kv["fail"]) + int(kv["bad"])
                return f"{kv['p50']:>8}{kv['p95']:>8}{kv['max']:>8}{err:>5}"
            out.append(f"  {n:>4} {cols(r)} {cols(w)}")
        for kv in rows:
            if kv["mode"] == "split":
                r17 = rnd.get("17", {})
                extra = f", {int(kv['p50']) - int(r17['p50'])} us more than in one run" if r17 else ""
                out.append(f"   17 blocks across a run boundary: p50 {kv['p50']} p95 {kv['p95']} max {kv['max']}"
                           f"{extra}")
        for kv in by.get("B2 fit", []):
            out.append(f"   fit {kv['mode']:4}: {kv['cmd_us']} us per command + {kv['blk_us']} us per block")
        sp = [kv for kv in b2 if "run_splits" in kv]
        if sp:
            out.append(f"   reads split across the file's runs: {sp[-1]['run_splits']}")
    b4 = [kv for kv in by.get("B4", [])]
    if b4:
        a = [kv for kv in b4 if "blocks" in kv][-1:]
        t = [kv for kv in b4 if "avg_us" in kv][-1:]
        if a:
            kv = a[0]
            out.append(f"B4 soak: {kv['frames']} frames, {kv['blocks']} blocks, bad {kv['bad']}, failed {kv['fail']} "
                       f"(recovered {kv['recovered']}, init {kv['inits']})")
        if t:
            kv = t[0]
            out.append(f"   17 blocks: avg {kv['avg_us']} us, max {kv['max_us']} us, {kv['over_budget']} over "
                       f"{kv['budget_us']} us (timeout {kv['timeout_us']} us)")
    b5 = by.get("B5", [])
    if b5:
        out.append("B5 drawing (us per call; *_screen per screen)")
        for kv in b5:
            out.append(f"   {kv['what']:16} {kv['us']:>9}")
        ck = last("B5 column_check")
        if ck:
            out.append(f"   column copy check: {'ok' if ck['bad_px'] == '0' else ck['bad_px'] + ' wrong pixels'}")
    c, fo, co = last("B8 cold"), last("B8 forced"), last("B8 cost")
    if c:
        out.append(f"B8 never-read blocks, {c['timeout_us']} us timeout: {c['timed_out']}/{c['mbs']} timed out, "
                   f"{c['lost']} lost, tries max {c['tries_max']}, to data p50 {c['to_data_p50']} max "
                   f"{c['to_data_max']} us")
    if fo:
        out.append(f"   forced timeouts: {fo['n']}, recovered without init {fo['recovered']}, re-read ok {fo['reread_ok']}")
    if co:
        out.append(f"   recover() {co['recover_us']} us (max {co['recover_max']}, idle {co['recover_idle']}), "
                   f"init() {co['init_us']} us (max {co['init_max']})")
    return "\n".join(out)


def default_card():
    if os.environ.get("CHSD_CARD") or "--device" in sys.argv:
        return
    card = PROJECT / "out" / "bench" / "BENCH.DAT"
    if not card.exists():
        subprocess.run([sys.executable, str(PROJECT / "bench" / "tools" / "make_bench_file.py")], check=True)
    os.environ["CHSD_CARD"] = str(card)


DRIVER, IDENT = BenchDriver, "ECBN"     # what the shared tools load from this file

if __name__ == "__main__":
    default_card()
    main(DRIVER, ident=IDENT)
