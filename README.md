# MacCraft

An isometric block-building game for classic 68k Macintoshes.

MacCraft is a small Minecraft-inspired sandbox: a fixed 48 × 48 × 8 world of
dirt, stone and grass drawn as 32 × 16 isometric tiles, with place and remove
actions, camera panning and a three-swatch block selector. It is written in C
against the classic Macintosh Toolbox and compiled with
[Retro68](https://github.com/autc04/Retro68). There is no 3D hardware and no
68020-only code: everything is integer 2D QuickDraw, so it runs on a Mac Plus
or SE as well as a Quadra.

<img src="docs/img/game.png" alt="MacCraft: a textured green block plateau on a blue sky, with the block selector along the bottom" width="100%">

## Controls

| Action | Input |
| --- | --- |
| Place block | Click an empty column, or the top face of a column |
| Remove block | Shift-click the top block of a column |
| Pan camera | Arrow keys or W/A/S/D |
| Select block | Click a block swatch in the window (dirt, stone, grass) |
| New game | Cmd-N |
| Quit | Cmd-Q |

## Requirements

- System 7 or later on a 68000 (or better) Macintosh
- 768 KB of application memory; a 1 MB partition is comfortable
- No network, no extensions, no System 7.5 features

## Current state

The world is a hand-authored island — coast, sand shelf, lagoon, plateau, stone
ridge with snow, a plank hut and trees — rendered as 32x16 isometric tiles with
one shade per face (top 1.0, left 0.8, right 0.6, dimmed by neighbouring blocks),
face culling, a Minecraft-style cloud layer, and a selection wireframe on the
block under the middle of the view.

Block textures are the next step and are already built: `src/game/art.c` holds a
64-entry shade-ramp palette and sixteen hand-authored 16x16 textures, and
`src/game/bake.c` shears them onto the renderer's exact quads. Getting an indexed
bitmap onto this QuickDraw port is unsolved; `docs/textures.md` records what was
tried, what each attempt proved, and the next three things to try.

## Building

The build runs inside the upstream Retro68 Docker image, so nothing needs to be
installed on the host beyond Docker:

```bash
docker run --rm -v "$PWD:/root/work" -w /root/work \
  -e APP_VERSION_STR="$(git describe --tags --always --dirty)" \
  ghcr.io/autc04/retro68 bash -lc 'make build APP_NAME=MacCraft'
```

The application is built against Apple's Universal Interfaces (required —
Retro68's bundled Multiversal set has no `Controls.h`, `Lists.h` or `Scrap.h`),
which the Makefile stages from `IAL_DIR`; the stock Retro68 image ships none, so
`IAL_DIR` must point at a copy. Locally, mount one into the container:

```bash
docker run --rm -v "$PWD:/root/work" \
  -v "$HOME/Documents/SendToMacOS9/MPW/MPW-GM+PR+Final Updates/Interfaces&Libraries:/ial:ro" \
  -w /root/work \
  -e APP_VERSION_STR="$(git describe --tags --always --dirty)" \
  ghcr.io/autc04/retro68 bash -lc 'make build APP_NAME=MacCraft IAL_DIR=/ial'
```

CI fetches the same interfaces from a pinned mirror of Apple's set into `ial/`
(gitignored). Output lands in `dist/`:

- `MacCraft.dsk` — bootable/disk image containing the application
- `MacCraft.bin` — raw application binary
- `MacCraft.APPL` — the application with its resource fork
- `MacCraft.ad` / `%MacCraft.ad` — the empty data fork and the AppleDouble file
  that carries the resource fork (what the `.sit` step uses)

With a local Retro68 build tree and a local interfaces copy, both can be
pointed at the host (defaults: `/Retro68-build` and the MPW path above):

```bash
make build APP_NAME=MacCraft RETRO68_PATH="$HOME/Projects/Retro68/build"
```

Other targets: `make run` (launch in Mini vMac through `LaunchAPPL -e minivmac`,
requires a configured `LaunchAPPL.cfg`), `make debug-build`, `make clean`,
`make format`, `make lint`.

### Tests

```bash
bash tests/host/run.sh
```

runs the Toolbox-free unit tests (world bounds, isometric round-trip) on the
host with AddressSanitizer and UBSan. The emulator scenarios in
`tests/emulator/` drive a real SheepShaver guest and are run locally on macOS;
see `tests/emulator/README.md`.

## Download

Tagged builds are published on the
[Releases](https://github.com/yocontra/maccraft/releases) page. Each release
carries the `.dsk`, the `.bin` and a `.sit` StuffIt archive with the resource
fork intact — on a real Mac, download the `.sit` and unstuff it, then copy the
application to your boot disk. The `.dsk` is convenient for emulators
(Mini vMac, Basilisk II, SheepShaver).

## License

MIT — see [LICENSE](LICENSE).
