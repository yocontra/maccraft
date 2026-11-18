# Emulator tests (T2)

These tests boot a real Mac OS 9 system under SheepShaver, launch the built
MacCraft application from `System Folder/Startup Items`, drive it with synthetic
mouse and keyboard events, and assert on the pixels and on OCR text. Nothing
here runs in CI: GitHub's Linux runners cannot boot a Mac, and neither the ROM
nor the boot volume can be committed. T2 runs locally on this Mac.

The same harness shape lives in `macintosh-ai/tests/emulator/`; keep the driver
commands, exit codes and VM cloning behaviour identical in both repos.

## Requirements

| Piece | What is used here |
|---|---|
| Emulator | SheepShaver inside `/Applications/Mac OS 9.app` |
| Emulator binary | `/Applications/Mac OS 9.app/Contents/Resources/Files/SheepShaver.app/Contents/MacOS/SheepShaver` |
| ROM | `~/Library/Application Support/MacOS9/MacOS.sheepvm/Mac OS ROM` (PowerPC, 1.9 MB) |
| Boot volume | `/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle` (3.8 GB on disk, 4.2 GB volume) |
| Screen | `screen win/1024/768`, `ramsize 1073741824` |
| Networking | `ether slirp` (not used by these scenarios) |
| OCR | `/opt/homebrew/bin/tesseract` (5.5.4 verified) |
| Python | 3 with `pyobjc-framework-Quartz` and `Pillow` |

The boot volume is **not** where the plan text implied: the prefs file at
`~/Library/Application Support/MacOS9/MacOS.sheepvm/prefs` has
`disk /Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle`,
and the `Application Support` bundle contains the ROM and prefs but no
sparsebundle. `vm/make_vm.sh` therefore resolves the disk by reading that
`disk` line first, then falling back to the two known paths. `MAC_ROM` and
`MAC_BOOT_DISK` override both.

## One-time setup

1. `brew install tesseract`
2. Install the Python dependencies into `tests/emulator/.venv`, which `run.sh`
   picks up automatically (before `/opt/homebrew/bin/python3`):

   ```sh
   /opt/homebrew/bin/python3 -m venv tests/emulator/.venv
   tests/emulator/.venv/bin/pip install -r tests/emulator/requirements.txt
   ```

   `requirements.txt` selects `pyobjc-framework-Quartz==10.3.1` on Python
   < 3.14 and `>=13.0` on 3.14+ (10.3.1 has no wheel for the default
   `/opt/homebrew/bin/python3`, currently 3.15). `EMU_PYTHON=<python>` still
   overrides the interpreter.
3. Grant **Accessibility** and **Screen Recording** to the terminal or agent
   host that runs the harness (System Settings > Privacy & Security). Without
   them `CGEventPost` silently does nothing and `CGWindowListCreateImage`
   returns nil/blank; `run.sh` exits 2 rather than reporting a false failure.

## Build first

`run.sh` never builds anything. Produce the application first:

```sh
make build APP_NAME=MacCraft        # inside the Retro68 image
```

`vm/make_vm.sh` installs `dist/MacCraft.APPL` plus its AppleDouble
`dist/%MacCraft.ad` (falling back to `dist/App.APPL`, then a single
`dist/*.APPL`/`dist/%*.ad` pair). `EMU_APP=<path>` overrides the `.APPL`; the
companion `%<name>.ad` is derived from its basename. The build must copy both,
which `make build` does.

## Running

```sh
tests/emulator/run.sh 01-launch --record   # first run only: write baselines
tests/emulator/run.sh 01-launch
tests/emulator/run.sh 02-place-block
tests/emulator/run.sh 03-pan
```

Exit codes:

* **0** — pass
* **1** — assertion failed; the observed diff fraction and OCR text are printed
* **2** — environment missing (ROM, boot volume, built app, tesseract,
  pyobjc/Pillow, TCC permission, emulator window never appeared)

`--record` writes `baselines/launch.png` / `baselines/game.png` instead of
comparing. `--keep` leaves `tests/emulator/.work/Test.sheepvm` and the cloned
volume behind for inspection; the emulator is still stopped.

## What each scenario proves

### `01-launch.sh`

Boots the clone, waits for the application's splash screen (OCR finds
`Start`), compares the whole guest frame to `baselines/launch.png` (≤ 2 %
pixels differ), clicks the Start Game button, waits for the splash text to
disappear and the new window to become stable, then compares the game frame to
`baselines/game.png` (≤ 2 %). Proves: the app launches at boot, creates its
splash window and its game window, draws the block selector and the initial
48×48×8 world, and responds to a real click on a Toolbox control.

### `02-place-block.sh`

From the game window: captures the screen, clicks the centre tile
(`points.tile`, the top of the world tile 24,24), asserts ≥ 5 % of the `tile`
region changed and ≤ 1 % of `outside-tile` changed, then shift-clicks the same
point and asserts the `tile` region is back within 2 % of the pre-click frame.
Proves: iso unprojection maps a screen click to the plateau tile, placement
adds a block in that column and invalidates only that column, and
shift-clicking removes the same block. The unchanged `outside-tile` region is
the dirty-rectangle check.

### `03-pan.sh`

Captures the display, sends `right` five times, waits for the repaint to
settle, and asserts ≥ 20 % of the `display` region differs. Proves: arrow
keys reach the game window and the camera pan redraws the world.

The 20 % figure is the plan's. Each pan is `kGameCameraPanX` = 16 px, so five
pans move the world 80 px; if the drawn plateau is too sparse for that to
touch a fifth of the display, this scenario fails and prints the measured
fraction — that number is the evidence, not a reason to re-run.

## How the VM is built

`vm/make_vm.sh`:

1. Reads the ROM and boot-volume paths (above) and refuses anything that is
   not a directory-based image of at least 100 MB.
2. `cp -cR` clones the sparsebundle to `tests/emulator/.work/MacOS9Test.sparsebundle`.
   On APFS this uses `clonefile(2)`: instantaneous and space-free. The user's
   volume is only read.
3. Attaches the clone at `.work/mnt` with
   `diskutil image attach --mountOptions nobrowse --mountPoint` (falling back
   to `hdiutil attach -noverify`), verifies `System Folder` exists, removes any
   old `Startup Items/MacCraft`, and installs the app.

   Retro68 writes `dist/MacCraft.APPL` as a **bare data fork** and the resource
   fork + Finder info as an AppleDouble in `dist/%MacCraft.ad`. A Mac OS 9
   volume needs both, so `make_vm.sh` parses the AppleDouble (magic
   `0x00051607`, entry 2 = resource fork, entry 9 = Finder info), writes the
   data fork to a staging file, sets `com.apple.ResourceFork` /
   `com.apple.FinderInfo` xattrs on it, then `ditto`s the staged file into
   `Startup Items/MacCraft`; if `ditto` drops the xattrs it writes them
   directly onto the HFS file. It then asserts `GetFileInfo -t` is `APPL` and
   `-c` is `MCft`. `ditto`ing the `.APPL` alone would install a zero-byte file
   with no resource fork, which the guest cannot launch.
4. Detaches the clone (also on exit/signal).
5. Creates `.work/Test.sheepvm/` with the rendered `prefs`, a symlink to
   `Mac OS ROM` and a symlink to the clone.
6. Creates `.work/share/` as the only `extfs` export. The harness never
   exports `~/Documents` or any other user directory.

`driver.py` launches the emulator with the `.sheepvm` bundle path (the macOS
convention used by the `Mac OS 9.app` droplet) and, if no window appears,
retries with `--config .work/Test.sheepvm/prefs`. The winning launch form
(`bundle` or `config`) is printed and written to `.work/launch-form`. On exit
the emulator process group is terminated, any leftover mount is detached, and
(unless `--keep`) the clone and `.sheepvm` are deleted.

Everything the harness writes lives under `tests/emulator/.work/`, inside the
clone, or — with `--record` only — in `tests/emulator/baselines/`.

## Coordinates

`scenarios/coords.json` holds named regions (`full`, `display`, `tile`,
`outside-tile`, `start-button`) and points (`start`, `tile`) in **guest-screen**
pixels (1024×768). The header comment in that file derives every number from
the runtime layout in `src/ui/splash_window.c` and `src/ui/game_window.c`: both
windows are sized from `qd.screenBits`, so with a 1024×768 guest they land at
fixed positions. `driver.py` translates guest coordinates through the host
window's position and title bar on every action, so moving or scaling the
SheepShaver window on the host does not change the scenarios.

Scenarios avoid the emulated mouse cursor contaminating diffs: before each
capture they run `emu move 5 5`, parking it in the menu bar outside every
asserted region.

## Driver commands

Scenarios are POSIX `sh` and get one function, `emu`, from `lib.sh`. Commands:

```
emu wait_ocr <region> <pattern> <timeout>    poll OCR until the pattern appears
emu wait_no_ocr <region> <pattern> <timeout> poll OCR until the pattern is gone
emu ocr <region> [pattern]                   print OCR text (optionally assert it)
emu move <region|point|X Y>                  move the guest cursor, no click
emu click <region|point|X Y> [--shift]       click (optionally shift-click)
emu dblclick <region|point|X Y>              double-click
emu key <combo>                              e.g. cmd+comma is "cmd+,", also "right", "return"
emu type <text>                              type printable ASCII on the US layout
emu shot <name>                              save .work/shots/<name>.png
emu changed <region> <baseline> <min>        assert >= min of the region changed
emu stable <region> <baseline> <max>         assert <= max of the region changed
emu wait_stable <region> <timeout> [max]     wait until frame-to-frame change <= max
emu sleep <seconds>
emu window                                   print the host window geometry
```

A diff fraction counts pixels whose 8-bit grayscale differs by more than 16,
so antialiasing and the dither phase do not count as change. Baselines are
full 1024×768 guest frames; a region is cropped from both before comparing.
Regions outside the guest screen are rejected rather than silently compared.

## Limitations

* SheepShaver only. `--driver basilisk` exits 2 with an explanation because
  there is no 68k ROM on this machine (`MAC_ROM`/`MAC_BOOT_DISK` could supply
  one); Basilisk is not on the acceptance path.
* The guest must boot to the Finder without a login dialog or the app will
  never appear; if `wait_ocr ... "Start"` times out, look at
  `.work/sheepshaver.log` and `--keep` the VM.
* `pyobjc-framework-Quartz==10.3.1` needs Python ≤ 3.13; on 3.14+ the
  requirements file selects pyobjc 13. See setup above.
* OCR is a sanity check on bitmap fonts, not a precise reading. Scenarios only
  look for short words (`Start`).
* The clone is a snapshot of the guest's disk. If a previous run damaged the
  VM, delete `tests/emulator/.work/` and run again; the user's volume is never
  modified.
