# tools/: the shared tools

Everything in this folder serves every game (the CHGame library's examples,
`platform/board/arduino/CHGame/libraries/CHGame/examples/Games/`), every app
and the libraries. The one entry point is `chgame`:

```bash
pip install -e .[sim]        # once, in the repository root: Pillow, pyserial, zig; the `chgame` command
cd platform/board/arduino/CHGame/libraries/CHGame/examples/Games/CHFour
chgame build                 # release build + size
chgame check                 # everything checkable without a board
chgame run tools/scripts/gameplay.txt out/gameplay
chgame --sketch CHChess gif  # any sketch by name, from anywhere
```

`python tools/chgame.py` is the same thing uninstalled. From inside a game's
folder (or any folder below it) the sketch is found by itself; from anywhere
else `--sketch` takes a game's, app's or CHGfx example's name, or a folder.
`chgame --help` lists the commands; the table in [CLAUDE.md](../CLAUDE.md)
says which to use when.

The shared tools know nothing about any one game. What a game has to say
about itself is in its `tools/game.py` (below), its `tools/chsim/chdrive.py`
(its own script commands) and its `tools/scripts/*.txt`.

## What is here

| Tool | What it does |
|---|---|
| `chgame.py` | The entry point. Finds the sketch, puts `tools/` and `tools/chsim/` on the path, and runs one of the modules below in-process: `build`, `upload`, `sim`, `run`, `shot`, `check`, `test`, `redraw`, `gif`, `size`, `audio`, `uploader`, `pack`, `card`. Exit codes: 0 done, 1 the thing failed, 2 usage or configuration. |
| `paths.py` | Where things are: the repository root, the libraries, the games and apps, `games()`, `sketch(name_or_dir)`, `here()` (the sketch the current folder is in). Puts the Python uploader (`platform/bootloader/host/py`) on the path when it is not installed. |
| `device.py` | Builds, uploads and drives any sketch on the board through arduino-cli, against the libraries in `platform/board/arduino/CHGame/libraries/`: `build(sketch, debug)`, `upload()` (the Python uploader; `--arduino` for arduino-cli and the Go tool), `run()` (debug build, upload, drive a script), `shot()`. A debug build adds `-DCHGAME_DEBUG=1`. |
| `check.py` | `chgame check`: the game's pre-steps, host tests, every script twice (same frames both times, no `BUG:`), the redraw check, the simulator tests, the device build and its size, then `ALL GOOD` or the sections that failed. `--quick`, `--no-device`, `--compare A B` (two output folders, frame by frame). Reads `tools/game.py`. |
| `hosttests.py` | `chgame test`: compiles and runs the game's host unit tests (`tools/tests/*.cpp`, one executable per entry of `TESTS` in `tools/game.py`) with one compiler line for all 20 (`-std=gnu++17 -fsanitize=undefined ...`), compares a test's output with a Python reference model where a game has one, and runs its extra test scripts. |
| `gamecfg.py` | Loads a game's `tools/game.py` and applies the defaults. Its docstring is the schema (summarised below). |
| `chsim/chsim.py` | The one simulator. `build` compiles a sketch for the PC: its `.ino` + `src/`, CHGfx's portable code, the CHGame library when the sketch includes it, CHSd's FAT reader and pretend card when it includes that, and the host shims. `run` is the free run: no driver, buttons from `--input`, the panel's frames to PNGs or a GIF (`chgame sim --free`). `test` builds and runs CHGfx's own tests (`chgame --sketch CHGfx test`). `$CHSIM_CXX`, `$CHSIM_FLAGS`, `$CHSIM_WRAP` (valgrind: see its docstring), `$CHSD_CARD`. |
| `chsim/host/` | The shims. `main.cpp`: virtual time, the lockstep protocol on stdin/stdout, the free mode, a deterministic seed. `chgfx_host.cpp`: stands in for `CHGfx.cpp` with the panel model (the measured wire rate scaled by the SPI divider, 45 us of setup, rows landing on a simulated panel as they convert, per-column tearing detection, the board's palette); a draw into rows still being sent is a `BUG:` line and exit code 3. `Arduino.h`, `sim.h`: the Arduino API for the PC and the shims' hooks. |
| `chsim/chdrivelib.py`, `chsim/chdrive.py` | The script driver: runs a script against the simulator or the board over the library's debug protocol (`wait tap hold release snap gif rec step say free freegif perf prof cal`; the header of `chdrivelib.py`). `chdrive.py` drives any sketch; a game's own `tools/chsim/chdrive.py` subclasses `Driver` for its commands and exports `DRIVER` and `IDENT`, which the shared tools load by path. |
| `chsim/diffdrive.py` | `chgame redraw`: builds the game twice, as it is and with `CHSIM_FORCE_FULL` (every frame redrawn in full), runs a script in both in lockstep and reports any pixel the incremental redraw got wrong. Five games have `tools/scripts/diff/*.txt` for it. |
| `chsim/simsave.py` | `Session`: a scripted save test in the simulator (say, tap, power-cycle, check), the body of the three `tools/tests/sim_save.py`. |
| `chsim/fbimage.py`, `chsim/gifsheet.py` | A framebuffer dump (8 KB of 4 bpp + 32 B palette) to a PNG, a contact sheet or a full-colour GIF; a GIF's frames tiled into one image to review it. |
| `readme_gif.py` | `chgame gif`: runs the game's `tools/scripts/gameplay.txt`, joins the clips it records into `docs/gameplay.gif` and refuses anything over 1 MB. `--check` checks every game's. The README format is in `docs/game-readme.md`. |
| `check_size.py` | `chgame size`: flash and RAM from the linker map, against the 50,944 B / 18,416 B limits, with the room left for the save pages; `--top N`, `--symbols`. |
| `audio/preview.py` + `audio/host/` | `chgame audio`: the game's sound effects and songs as WAV, rendered by the real engine through a model of the piezo timer, with a hash per sound. |
| `artlib.py`, `art/common/` | The art several games share, kept once: `dealer.png`, `hand.png`, `faces.png`, the card ranks, pips and suits, the chips, the slab-serif `font.txt`, the win and broke lettering, `token_banana.png`. A game's `tools/assets.py` asks `artlib.art(tools_dir, name)`, which takes the game's own `tools/art/` file first and the common one otherwise. |
| `pixkit.py` | A Python rendering of the library's drawing primitives, palette and sprites (checked against `chgame/Draw.cpp`), for mock-ups of screens before they are coded: CHRoulette's and CHWordWheel's `mockup.py` and previews. |
| `music/composer.py` | The Playtune composer: note names and voices to the bytes the library's `audio::music` plays, and the writer of a game's `src/audio/Music.cpp`. The three `make_music.py` (CHBlackjack, CHRoulette, CHWordWheel) keep only their songs. |
| `fonts/serif.py`, `fonts/font_preview.py` | The anti-aliased serif rasteriser behind CHCrossword's and CHWords' `tilefont.py` and CHDominoes' `aafont.py`, and the display-font preview sheet behind CHBackgammon's and CHFour's `font_preview.py`. |
| `serialcap.py` | Finds the board's USB serial port (VID:PID 16C0:27DD), opens it with DTR set and prints what it says. `find_port()` is used by `device.py` and the driver. |
| `chgpack.py` | `chgame pack`: game packages for the SD menu (`docs/chg-format.md`): `pack`, `verify` (exactly as the bootloader does), `info` (a card, folder or image). |
| `sdcard/mkcard.py` | `chgame card`: builds every game in `sdcard/games.json` (and CHSDtoUSB), packs them and lays out a whole card in `out/sdcard/`; `--image` also writes a FAT32 image. |
| `libzip.py` | Packs one bundled library as `out/<Name>-<version>.zip` for the IDE's *Add .ZIP Library*. |
| `release/` | Cutting a board package release, in Python: `build_uploader.py` (the Go `chgame-upload` for five hosts), `make_tool_archives.py`, `make_package.py` (the platform archive from `git ls-files` and the Boards Manager index), `release.py` (checks, builds, packages, runs the new-user test and publishes with `gh`; `--dry-run`), `stage.py` (the same as a local `-local` pre-release, tested), `serve.py` (serves a built release to the Arduino IDE on localhost), `acceptance.py` (the new-user test: a fresh arduino-cli installs the package, every example compiles from it, the SD card zip is packed from those builds). The steps are in `platform/board/docs/building.md` and `trying-a-release.md`. |

`pyproject.toml` in the repository root installs all of it as the
`chgame-tools` package (`pip install -e .`; extras `sim` for zig, `words`
for CHWords' dictionary builder). `tools/requirements.txt` is the same
dependency list for a plain `pip install -r`.

## What a game has of its own

| In the game | What it is |
|---|---|
| `tools/game.py` | What the shared `chgame test`, `check` and `redraw` need to know (the schema below). Absent means every default. |
| `tools/chsim/chdrive.py` | The shared driver plus the game's own script commands (CHChess `goto`/`waitturn`/`board`, CHCrossword `type`/`solve`/`--card`, CHMahjong `solve`/`takehint` ...) and its handshake `IDENT`. |
| `tools/scripts/*.txt` | Driver scripts: the README's GIF (`gameplay`), smoke tests, perf runs, device-only runs (`device_*`), the redraw scripts (`diff/`). |
| `tools/assets.py` + `tools/art/` | The art pipeline: `tools/art/*` and `tools/art/common/*` to `src/assets/Assets.{h,cpp}`, with previews in `build/assets`. |
| `tools/tests/*.cpp` | The host unit tests; `sim_save.py` (CHBingo, CHCraps, CHYacht), `ref_bingo.py`, `ref_roulette.py` and `run_ball_tests.py` (reference models), `cwtests.py` (CHCrossword's card tests). |
| `tools/chsim/host/` | (optional) the game's own simulator shims; a `.cpp` there with a shared shim's name replaces it. |

### `tools/game.py`

A plain Python module of UPPERCASE constants and, where a game needs code,
hook functions; an unknown name is an error, so a typo cannot turn a check
off. The full schema with the defaults is the docstring of `gamecfg.py`.

| Name | Default | What it says |
|---|---|---|
| `TESTS` | `{}` | name → spec: `sources` (game-relative; globs; `lib/` the CHGame library's src, `chsd/` CHSd's; a trailing `?` for a source that may be missing), `opt`, `defines`, `includes`, `cwd`, `args`, `optional`, `run`, `reference` (a Python model to compare the output with), `extra_flags`; or a callable returning the dict |
| `QUICK_ARGS` | `["--quick"]` | what `chgame check --quick` passes to the tests |
| `EXTRA_TEST_SCRIPTS` | `[]` | Python scripts run after the executables |
| `PRE_STEPS` | `[]` | `(title, argv)` scripts run before everything (a puzzle pack, the SD bank) |
| `SKIP_SCRIPTS` | `("device_",)` | script stems not run in the simulator |
| `ONCE_SCRIPTS` | `set()` | stems run once instead of twice (scripts using `free`/`freegif` are, always) |
| `ECHO` | `()` | output-line prefixes echoed under a script, besides `perf` and `calibration` |
| `CARD` | `None` | which scripts get an SD card (`$CHSD_CARD`), which file, and how to build it |
| `REDRAW` | `tools/scripts/diff/*.txt`, ticks `(1, 3)`, `CHSIM_FORCE_FULL` | the redraw check |
| `SIM_TESTS` | `[]` | Python scripts run against the simulator (`sim_save.py`) |
| `BUILD_REQUIRE` | `[]` | substrings the device build must print (`save pages free: 2`) |
| `before_tests`, `after_tests`, `before_check`, `after_host_tests` | – | hooks |

### Game-specific tools

| Game | Tools |
|---|---|
| CHBackgammon | `train/` (self-play trainer for the CPU's network, the race table fit, the match equity table), `lookdev.py`, `font_preview.py` |
| CHBingo | `chsim/autoplay.py` (records a won round as a GIF), `tests/ref_bingo.py` (independent model of the set-up) |
| CHBlackjack | `make_music.py`, **`probes/FlashProbe/`** (a hardware probe sketch that proves a sketch can erase and write a flash page that survives re-upload; the save-page mechanism of every game rests on it) |
| CHBoardwalk | `sheet.py` (sprites ↔ one editable PNG sheet), `lookdev.py` |
| CHCheckers | `sheet.py`, `tests/demo_line.cpp` (finds the title screen's demo game) |
| CHChess | `book.py` (trims the opening book), `pieces.py` (renders the iso pieces), `sheet.py` |
| CHCrossword | `puzzles/` (pack format, builder, grid filler, `.puz` import, card images), `tilefont.py`, `tests/cwtests.py` |
| CHDominoes | `aafont.py`, `tilemock.py` |
| CHFour | `font_preview.py` |
| CHMahjong | `faces.py` (tile faces), `bird.py`, `layouts.py` + `layouts/*.txt` (layout maps → `Layouts.cpp`), `sheet.py` |
| CHRoulette | `wheel.py` (wheel angle maps), `spin_preview.py`, `logo_preview.py`, `mockup.py`, `make_music.py`, `tests/run_ball_tests.py`, `tests/ref_roulette.py` |
| CHSlots | `strips.py` (reel strips → `Strips.h`) |
| CHSnakes | `turns.py` (CPU turn table), `sheet.py`, `lookdev.py` |
| CHSolitaire, CHTicTacToe | `make_logo.py`; CHTicTacToe also `pieces.py` (ray-marched iso pieces) |
| CHWords | `dict/` (word lists → flash dictionary and `sdcard/WORDS.DIC`), `tilefont.py` |
| CHWordWheel | `phrases/build_bank.py` (flash bank + `sdcard/PHRASES.BNK`), `make_music.py`, `mockup.py` |

### Elsewhere in the repository

| Tool | What it does |
|---|---|
| `platform/bootloader/host/py/` | The Python uploader, `chgame-upload` (`chgame uploader ...` or `python -m chgame_upload`): probe, info, flash, selfupdate, burn. The Go tool beside it in `host/go/` is the one the board package ships; both are held to the same protocol vectors (`platform/bootloader/test/protocol/`). |
| `platform/bootloader/test/native/run_tests.py` | The bootloader's PC test suite. |
| `platform/board/arduino/CHGame/libraries/CHGfx/extras/tests/` | CHGfx's own tests, run on the simulator above (`chgame --sketch CHGfx test`); `extras/fontconvert.py`, `sprite4.py` are its converters. |
| `platform/board/arduino/CHGame/libraries/CHSd/tools/fatimg.py`, `tests/run_tests.py` | FAT16/FAT32 card images, and CHSd's host tests on them. |
| `platform/board/arduino/CHGame/libraries/CHGame/examples/Apps/CHSDtoUSB/tools/chsd_test.py`, `scsi.py` | Hardware test suite for the SD-to-USB sketch (Windows, SCSI pass-through). `find_drive()` locates the board's drive. |

## Candidates to share later

- **The `sheet.py` export/import pattern** (CHBoardwalk, CHCheckers,
  CHChess, CHMahjong, CHSnakes): the same idea five times, each with its own
  sprite list.
