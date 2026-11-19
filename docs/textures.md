# Block textures: 16x16 pixel art on every face

Each block face is a hand-authored 16x16 texture, sheared onto the renderer's
quad and shaded by one palette ramp step per face direction. That is Minecraft's
picture exactly: flat faces, pixel art, one shade per axis.

## How it works

- `src/game/art.c` — a 64-entry palette of 16 material hues in four shade steps
  (`index = hue * 4 + step`) and sixteen textures: grass top and side, dirt,
  stone, cobblestone, sand, gravel, planks, log side and top, leaves, brick,
  snow, bedrock, water, glass. Shading is a ramp step, never a blend.
- `src/game/bake.c` — shears a texture onto the exact quads the renderer draws:
  a 32x16 diamond for the top face, 16x16 parallelograms for the two sides, each
  with a byte-per-pixel coverage mask. A side face is 16 pixels wide, not 32.
- `src/ui/sprite_sheet.c` — bakes all 14 kinds x 3 faces x 4 shade steps once at
  startup, then writes each face into the screen's pixels a row at a time.

`tests/host/test_art.c` and `tests/host/test_bake.c` prove the art is legal and
distinct and that the bake is a bijection onto the silhouette (256 inked pixels
for a top face, 128 for a side), deterministic and monotone under shading.

## The two facts that took the longest, and are worth keeping

**The screen is 32-bit direct colour, not 8-bit indexed.** Measured through the
harness log: `pixelSize 32`, `rowBytes 4096` for a 1024-wide screen. Every
earlier attempt assumed an indexed screen and wrote one byte per pixel — into the
blue channel of every fourth pixel. That is what "the sprite is a dark blob" was.
The writer emits `0x00RRGGBB` words now, with a 16-bit and an 8-bit branch for
other depths.

**A window port's global position is the negation of `portBits.bounds`.**
`portRect` starts at 0,0 and `LocalToGlobal` is a no-op for a window, so the
offset has to come from the rectangle the window was created at (or from
`-portBits.bounds.left/top`). Without it every sprite lands one window-origin to
the left.

Two more, for the record: `CopyBits`/`CopyMask`/`CopyDeepMask` from an offscreen
bitmap never resolved a colour table here (nothing, or white, every time), and an
indexed `GDevice` keeps a `Palette` of `ColorInfo` in `pmTable`, not a
`ColorTable`. Neither matters now that the pixels are written directly.

## Cost

Roughly 110,000 pixel writes for a full repaint (three faces per visible block,
256 pixels each). The scene repaints only on invalidation, not in the idle loop.

## Render cost, measured on the emulated guest

Ticks are 1/60 s, ten full repaints of the play area, measured through
`src/selftest.c` so the numbers come from the machine rather than from a guess:

| Version | Ticks per frame | Change |
|---|---|---|
| first textured build | 66.8 | — |
| walk columns, light only visible faces | 27.2 | 2.5x |
| hoist the depth branch, walk the row pointer | 25.3 | 2.6x total |

The split at that point: 25.3 ticks of drawing against 4.8 ticks of traversal
and lighting, so 83% of a frame is the pixel copy. Getting further means writing
fewer pixels -- a screen-space cache of the last frame, or 32-bit preconverted
sprite bands (which cost 344 KB of static data) -- not more micro-optimisation of
the loop that is already one load, one table lookup and one store per pixel.
