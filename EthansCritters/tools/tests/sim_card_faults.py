"""Ethan's Critters: a card that stalls, in the simulator (M8a; `ec check`
runs it, tools/game.py's SIM_TESTS; or `python tools/tests/sim_card_faults.py`).

The simulator's card knobs (CHSd's host/sd_host.cpp, patch P3) set per run:

  1. The boot's warm-up (the first boot with a card's data reads the whole
     file once, STIRRING THE SWAMP): how long it takes with the board's card
     timing, and with every 1 MB of the card slow on its first read
     (CHSD_SIM_COLD_US: the ~1.05 s stall the board showed once after a
     write, if it came per region). Free runs; the game prints WARM ms=.
  2. tools/scripts/card_faults.txt with every 7th stream failing part way
     and 2 ms of jitter a command: every card user loses frames (Z's lost=)
     and play goes on (the script's Z and W would be refused on the card
     screen).
  3. A card pulled and put back (X0 X1, A: the title; this board has its
     data marked as read, so no warm-up) comes back cold: in play each 1 MB
     region's first read stalls. 1.0 s: frames are held (the logic waits),
     then play goes on; 2.0 s: past card::GIVE_UP lost frames, the card
     screen.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
ROOT = GAME.parent
sys.path.insert(0, str(ROOT / "chgame" / "tools"))
sys.path.insert(0, str(ROOT / "chgame" / "tools" / "chsim"))
import chsim  # noqa: E402
from chdrivelib import Driver, SimTransport, mask_of  # noqa: E402

CARD = ROOT / "out" / "card" / "CRITTERS.DAT"
BOARD = {"CHSD_SIM_CMD_US": "832", "CHSD_SIM_BLK_US": "222"}
KNOBS = ("CHSD_SIM_CMD_US", "CHSD_SIM_BLK_US", "CHSD_SIM_READ_US", "CHSD_SIM_JITTER_US", "CHSD_SIM_COLD_US",
         "CHSD_SIM_COLD_KB", "CHSD_SIM_FAIL_EVERY", "CHSD_SIM_INIT_US")
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok
    return ok


def env(**knobs):
    e = {k: v for k, v in os.environ.items() if k not in KNOBS}
    e["CHSD_CARD"] = str(CARD)
    e.update({k: str(v) for k, v in knobs.items()})
    return e


def warm_ms(exe, **knobs):
    """A free run of the boot: (WARM ms, lost) the game printed, or None. (A at frame 60: a block
    over 1 s late is too late for CHSd's read() at the boot, then INSERT CARD and A tries again.)"""
    r = subprocess.run([str(exe), "--frames", "400", "--input", "60:A,64:"], env=env(**knobs),
                       capture_output=True, text=True, encoding="latin-1")
    m = re.search(r"WARM ms=(\d+) lost=(\d+)", r.stdout)
    return (int(m.group(1)), int(m.group(2))) if m else None


class Run:
    """The simulator in lockstep with these knobs."""

    def __init__(self, exe, **knobs):
        old = dict(os.environ)
        os.environ.clear()
        os.environ.update(env(**knobs))
        try:
            self.t = SimTransport(exe)
        finally:
            os.environ.clear()
            os.environ.update(old)
        self.d = Driver(self.t, "ECRT")
        self.d.handshake()
        self.d.cmd("L1")

    def say(self, line):
        """A game command: (accepted, the lines it printed)."""
        self.t.send(line)
        out = []
        for _ in range(10000):
            r = self.t.readline()
            if r.startswith("OK ") and r[3:].strip().isdigit():
                self.t.send("N 5")              # a frame ack: still waiting
            elif r.startswith("OK"):
                return True, out
            elif r.startswith(("ERR", "HELD")):
                return False, out
            else:
                out.append(r)
        return False, out

    def lost(self):
        ok, out = self.say("Z")
        m = [re.search(r" lost=(\d+)", ln) for ln in out]
        m = [x for x in m if x]
        return int(m[0].group(1)) if ok and m else None

    def close(self):
        self.t.close()


def script_lines(path):
    for raw in Path(path).read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            yield line


def main():
    exe = chsim.build(GAME, out=GAME / "tools" / "chsim" / "build" / "card_faults_sim.exe")

    # 1. the warm-up
    blocks = CARD.stat().st_size // 512
    print(f"the warm-up: CRITTERS.DAT {blocks} blocks ({CARD.stat().st_size / 1e6:.1f} MB)")
    base = warm_ms(exe, **BOARD)
    if check(base is not None and base[1] == 0, f"warm-up, the board's card: {base} (ms, lost)"):
        mbs = blocks * 512 / 1e6 / (base[0] / 1000)
        print(f"     {mbs:.2f} MB/s: the whole {blocks * 512 / 1e6:.1f} MB once, {base[0] / 1000:.1f} s")
    for cold in (300000, 950000, 1050000):
        w = warm_ms(exe, CHSD_SIM_COLD_US=cold, **BOARD)
        regions = (blocks + 2047) // 2048
        check(w is not None, f"warm-up, each of the {regions} MB {cold // 1000} ms late on its first read: "
                             f"{w} (ms, lost streams)")

    # 2. faults all through play: card_faults.txt, every 7th stream failing
    r = Run(exe, CHSD_SIM_FAIL_EVERY=7, CHSD_SIM_JITTER_US=2000, **BOARD)
    lost, done = [], True
    for line in script_lines(GAME / "tools" / "scripts" / "card_faults.txt"):
        op, *args = line.split()
        if op == "wait":
            r.d.frames(int(args[0]))
        elif op == "step":
            for _ in range(int(args[0])):
                r.d.frames(1)
        elif op == "tap":
            r.d.buttons(mask_of(args[0]))
            r.d.frames(int(args[1]) if len(args) > 1 else 3)
            r.d.buttons(0)
            r.d.frames(1)
        elif op == "hold":
            r.d.buttons(mask_of(args[0]))
        elif op == "release":
            r.d.buttons(0)
        elif op == "say":
            if args == ["Z"]:
                n = r.lost()
                done &= n is not None
                lost.append(n)
            else:
                ok, _ = r.say(" ".join(args))
                done &= ok
    r.close()
    check(done and all(n is not None and n > 0 for n in lost),
          f"card_faults.txt, every 7th stream failing: played through, frames lost per part {lost}")

    # 3. a card put back cold: held frames, then on; or, too long, the card screen
    def walk_in(r):
        """Into play by the hut: from the card screen A until the title takes W (CHSd's read() gives
        up on a block over 1 s late, so a cold card's first try at the boot or after X1 says INSERT
        CARD: A again)."""
        for _ in range(5):
            ok, _ = r.say("W 150 600")
            if ok:
                return True
            r.d.buttons(mask_of("A"))
            r.d.frames(3)
            r.d.buttons(0)
            r.d.frames(10)
        return False

    for cold, survives in ((1000000, True), (2000000, False)):
        r = Run(exe, CHSD_SIM_COLD_US=cold, **BOARD)
        r.d.frames(30)
        ok_w = walk_in(r)                       # (and the warm-up before the title: every region warm)
        r.d.frames(10)
        r.say("X0")
        r.d.frames(10)                          # the card screen
        r.say("X1")                             # back in cold: no warm-up (its data is marked as read)
        ok_w &= walk_in(r)                      # by the hut: its world blocks cold (their first read since)
        r.say("Z0")
        for _ in range(150):
            r.d.frames(1)
        n = r.lost()
        r.close()
        if survives:
            check(ok_w and n is not None and n > 0, f"a card back in cold, {cold // 1000} ms a region: held "
                                                    f"{n} frames, played on")
        else:
            check(ok_w and n is None, f"a card back in cold, {cold // 1000} ms a region: past GIVE_UP, "
                                      f"the card screen (Z refused)")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
