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

## Interaction and sound

- The tile under the mouse is picked on the surface you can see, not by
  unprojecting the ground plane: `game/pick.c` searches the columns within
  `kWorldZ` tiles of the ground candidate and hit-tests each one's top-face
  diamond, so clicking a hill acts on the hill. `test_pick` proves it against a
  flat area, a three-level stack and a tall column behind a short one.
- The selection box, the status strip and the click all read that same pick, so
  what is highlighted is what a click acts on. Moving between cells invalidates
  only the two selection-box rectangles.
- The HUD (crosshair, held block) is drawn through the sprite pixel path. Every
  QuickDraw route for it failed on this port: `PaintRect` draws nothing inside
  the play area, and `FillRect` is at the mercy of the update clip while the
  world's pixels are written directly. `Sprite_FillRect` is the one primitive
  that always lands.
- Sound: `sound/sfx.c` builds `'snd '` command lists in memory and plays them
  through a `SndNewChannel` square-wave synth -- click on a swatch, place and
  break on the world. The harness proves playback was accepted
  (`sfx_lasterr=0`), which required one fix in the harness itself: the generated
  prefs set `nosound true`, so the guest had no audio hardware and the Sound
  Manager answered everything with `noHardwareErr` (-200).

## Sky and clouds are painted, not filled

In the play area QuickDraw fills did not land reliably on this port, so the sky
and its clouds are drawn through the pixel path as well: one flat `0x78A7FF`
fill (Minecraft's sky), then the cloud slabs from `sky.c`'s layout, white with a
`0xC8C8D8` underside one pixel high. `Sky_CloudRectCount`, `Sky_CloudRect` and
`Sky_CloudPeriod` are exported from `sky.h` so the colour renderer and the
one-bit `Sky_Draw` dither path place clouds in the same spots. A 1-bit screen
still uses `Sky_Draw` for the whole backdrop, because dithering is the better
picture there.

## Contact shading

A side face is no longer one flat shade: the bake darkens it in three steps from
top to bottom (`RowShade`), which reads as Minecraft's smooth lighting where a
face meets the ground. Side faces stop looking like they float.

## Rendering cost, second pass

Two things dominated the "it rescans the map all the time" report, and neither was
the rasteriser:

- The idle loop marked the whole display dirty on **every physics tick** because
  it repainted whenever `onGround` was false. A player spawned inside geometry
  never landed, so the display was repainted sixty times a second. It is now
  invalidated only when the camera moved, or when the player crossed into another
  pixel.
- The held block's swing marked the whole display dirty for its eight ticks; it
  marks the block's own corner now.

A camera pan no longer recomposes the scene at all: `Sprite_ScrollRect` shifts the
frame that is already on the screen and the renderer redraws only the strip that
the shift exposes. That is the difference between a full 25-tick pass and a couple
of memmove rows per tick while walking.

The spawn point was also wrong in a way that hid itself: the island's centre is the
hut, and the hut's roof is the top block the world can hold (z = 7), so spawning at
`TopZ + 1` put the player at z = 8, outside the world, falling for ever. The spawn
now walks out from the centre to the first column with ground and no structure.
