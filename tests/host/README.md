# Host tests

Native unit tests for the Toolbox-free game logic in `src/game/`.

They need nothing beyond a C compiler: no Retro68 toolchain, no Mac, no
emulator. CI (ubuntu) runs them before the cross-build, and they are the
only automated check of the world/iso maths.

## What they cover

`test_world.c`

- Out-of-range `World_Get` / `World_Set` return empty / `false`
  (`-1`, `48`, `z == 8`).
- `World_Reset` produces the 12x12 two-block grass plateau on
  `x,y in 18..29` and leaves everything else empty.
- `World_Height` counts occupied blocks from `z = 0` up to the first gap;
  `World_TopZ` reports the highest occupied `z` (or `-1`).
- `kWorldZ == 8` blocks fit in a column, the ninth is rejected.
- `World_Reset` clears edits.

`test_iso.c`

- The 2:1 lattice: `+x` is `(+16, +8)`, `+y` is `(-16, +8)`, `+z` is
  `(0, -8)`.
- `Iso_Unproject(Iso_Project(x, y, 0, cam))` round-trips for every tile
  `0..47 x 0..47` and cameras `(0,0)`, `(-160,-80)`, `(160,80)`.
- `Iso_Unproject(0, 0, ...)` yields tile `(0,0)`, never a negative one;
  points left/above the origin and tiles `x == 48` / `y == 48` are
  rejected.
- No `short` overflow at `x = y = 47, z = 7`.

## Running

```sh
sh tests/host/run.sh
```

Success prints one line per suite and exits 0:

```
test_world: ok
test_iso: ok
```

A failure prints the failing file, line and expression, and exits
non-zero. `CC` overrides the compiler:

```sh
CC=gcc sh tests/host/run.sh
```

The binaries are built with `-fsanitize=address,undefined` into a temp
directory that is removed on exit.

## Why the compat directory

`src/game/world.h` and `iso.h` include `<Types.h>` from the Mac
toolchain. `compat/Types.h` is a three-line shim (`Boolean`, `true`,
`false`) placed first on the include path so the same headers compile
natively. Nothing in `src/` needs to change.
