# Block textures: the pipeline is built, the blit is not solved

The renderer draws blocks as *flat shaded polygons* today. Minecraft fidelity
wants a 16x16 pixel texture on each face, and everything for that exists in this
tree except one thing: getting the pixels onto the screen.

## What is built and tested

- `src/game/art.c` — a 64-entry palette of 16 material hues in four shade steps
  (`index = hue * 4 + step`), plus sixteen hand-authored 16x16 textures:
  grass top and side, dirt, stone, cobblestone, sand, gravel, planks, log side
  and top, leaves, brick, snow, bedrock, water, glass. Face shading is a ramp
  step, never a blend, so a face stays pixel art.
- `src/game/bake.c` — shears a texture onto the exact quads the renderer draws:
  a 32x16 diamond for the top face, and 16x16 parallelograms for the two sides,
  each with a byte-per-pixel coverage mask. A side face is 16 pixels wide, not
  32: that is the geometry `DrawBlock` actually paints.
- `src/game/bake_quickdraw.c` — a `CopyDeepMask` blitter with a cached offscreen
  buffer and the palette pinned into it.

`tests/host/test_art.c` and `tests/host/test_bake.c` prove the art is legal and
distinct and that the bake is a bijection onto the silhouette (256 inked pixels
for a top face, 128 for a side), deterministic and monotone under shading.

## Update: the direct-write route works, placement does not yet

A later attempt took a different road and got the pixels on screen: lock the
device PixMap and write the band bytes straight into it. Sprites appeared with
their real texture, which proves the bake, the shading steps and the row copy are
all correct. Two things were still wrong when it was set aside:

- the sheet was written at local coordinates, and a window's `portRect` starts at
  0,0 so `LocalToGlobal` is a no-op for it; with the window's origin passed in
  explicitly the sprites moved but still landed outside the play area, and
  disabling the hand clipping did not change where they appeared;
- the frame it produced also showed the play area sky short, which points at the
  same geometry, not at the blit.

That prototype was removed rather than shipped, because v1.0.5's polygon
renderer draws the world correctly and the direct-write version did not. The next
person should start by logging `sx, sy` and `sDisplayRect` for the first ten
blocks in the harness log (`src/selftest.c` already writes one) and comparing
them with what the polygon path draws -- the pixel writing itself is solved.

The device palette is also readable now: `pmTable` on an indexed GDevice holds a
`Palette` (a dense list of `ColorInfo` with `ciRGB`), not a `ColorTable`. Reading
it through that structure gives a usable nearest-colour map; reading it as a
`ColorTable` is what made every sprite white earlier.

## What blocks it (as of v1.0.5)

`CopyBits`, `CopyMask` and `CopyDeepMask` from an offscreen source into this
window do not transfer colour on the emulated Mac OS 9 guest. Measured, not
guessed:

| Attempt | Result |
|---|---|
| `CopyMask(sheet, mask, portBits)` | Mac OS "error of type 10" — `portBits` is a dummy for a colour port |
| `CopyDeepMask(sheet, mask, *portPixMap)` | runs, draws nothing |
| `CopyBits(sheet, *portPixMap)` | runs, draws nothing |
| screen-bitmap destination, no mask | draws the correct silhouette, in white |
| `CopyDeepMask`, screen destination, `GetCTable(8)` mapping | still white |
| sheet carrying its own 64-entry `CTabHandle` | still white |

Two facts fell out of that table and are worth keeping:

1. A self-copy of the screen's own bits *does* land, and the sprite silhouette
   appears when the destination is the screen bitmap — so a blit is possible;
   what fails is anything that has to resolve the source against a colour table.
2. The guest's 256-entry palette is not the standard Mac cube. Measured from a
   screenshot of the selector strip, only a handful of distinct colours come
   back: grass (0x669933) -> (0,128,27), sand -> white, stone -> white,
   dirt/cobble/bedrock -> black, water -> (21,0,209). Installing our own palette
   with `SetPalette` made no difference on this guest. On a real Mac with the
   standard palette the mapping would be far kinder, but nothing here can be
   verified against that.

So the open question is narrow: **how to get an indexed bitmap onto a CGrafPort
on this QuickDraw, with its mask.** The next things to try, in order:

1. `CopyBits` with the destination being `(*port)->portPixMap` while that port is
   also the current port *and* the sheet's `pmTable` is the port's own table
   handle (not a fresh `CTable`).
2. `CopyBits` of the sprite into a 1-bit offscreen world with the mask applied,
   then `CopyMask` that world with `srcOr` — the classic two-pass stencil.
3. Draw the face with `CopyBits` per horizontal run, clipped by the quad's
   scanline extents (no mask at all): ~16 rectangular copies per face.

Until one of those works, the polygon renderer stays: it is what produces the
screenshot in the README, and `tests/host/run.sh` keeps the texture pipeline
honest so it can be switched on the day the blit lands.
