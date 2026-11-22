/*
 * art.c - the 64-colour palette and the hand-made 16x16 block textures.
 *
 * The palette is 16 material hues of four steps.  A hue's four entries are
 * consecutive, so a shade step is (index & ~3) + step.  This is the whole
 * point of the table: the isometric renderer can darken a face by changing
 * the index, never by blending, so the pixels stay exact on an 8-bit Mac.
 *
 * Index 0 is the darkest ink (0x111111).  Putting the ink hue first makes an
 * untouched or mistakenly-empty texture byte read as a hole instead of a
 * bright colour; only cobblestone, gravel and bedrock use it, and only for
 * mortar and dark chips.
 *
 * Colour values in the comments are 0xRRGGBB.  The table stores the lossless
 * 0x0RGB nibble form described in art.h: a legal component is 0xNN, so one
 * nibble per gun identifies it exactly.
 */

#include "art.h"

#include <stddef.h>

/* ---------------------------------------------------------------- */
/* Palette: 16 hues x 4 steps, index = hue * 4 + step.               */
/* ---------------------------------------------------------------- */

static const unsigned short kPalette[kArtPaletteEntries] = {
    /* ink (darkest ink: index 0): 111111 222222 333333 444444 */
    0x111, 0x222, 0x333, 0x444,
    /* grass green:               225511 337722 449933 55AA44 */
    0x251, 0x372, 0x493, 0x5A4,
    /* leaf green:                113311 224422 336633 448844 */
    0x131, 0x242, 0x363, 0x484,
    /* dirt brown:                664422 775533 886644 997755 */
    0x642, 0x753, 0x864, 0x975,
    /* plank wood:                664411 886633 AA8844 CCAA66 */
    0x641, 0x863, 0xA84, 0xCA6,
    /* stone grey:                444444 555555 666666 888888 */
    0x444, 0x555, 0x666, 0x888,
    /* cool grey:                 555566 666677 777788 9999AA */
    0x556, 0x667, 0x778, 0x99A,
    /* sand tan:                  AA9977 BBAA88 CCBB99 DDCCAA */
    0xA97, 0xBA8, 0xCB9, 0xDCA,
    /* dark brown:                332211 443311 554422 665533 */
    0x321, 0x431, 0x542, 0x653,
    /* brick red:                 552211 773322 884433 AA5544 */
    0x521, 0x732, 0x843, 0xA54,
    /* pale blue:                 7799AA 88AABB 99BBDD AACCDD */
    0x79A, 0x8AB, 0x9BD, 0xACD,
    /* water blue:                223366 334488 4455AA 5566BB */
    0x236, 0x348, 0x45A, 0x56B,
    /* white / ice:               BBBBBB CCCCCC DDDDDD FFFFFF */
    0xBBB, 0xCCC, 0xDDD, 0xFFF,
    /* warm light:                665544 887766 AA9988 CCBBAA */
    0x654, 0x876, 0xA98, 0xCBA,
    /* cold dark:                 111122 222233 333344 444455 */
    0x112, 0x223, 0x334, 0x445,
    /* warm accent:               663311 884422 AA5533 CC6644 */
    0x631, 0x842, 0xA53, 0xC64,
};

/* ---------------------------------------------------------------- */
/* Textures: 256 bytes each, row-major, 16 pixels per row.           */
/* ---------------------------------------------------------------- */

/* The baker snaps a whole face to one step of each texel's hue (bake.c), so a
   change of step alone is invisible on a shaded face; a change of hue is what
   survives.  Textures that need a second tone therefore take it from a
   neighbouring material hue (the second green, the darker brown, the cool
   grey), while the four steps of each hue carry the finer tonal noise on the
   unlit path.  Shadow tones are the only other colours: the ink hue (index 0)
   in cobblestone, gravel and bedrock, and the cold dark hue of stone's
   one-pixel cracks. */

/* Grass top: yellow-green carrying the full four-step green ramp, with
   scattered clumps of the darker leaf green.  The second green is what keeps
   the clumps visible once a face is shaded to a single step; the ramp spread
   carries the finer noise on the unlit path.  Step 0 appears only as a few
   deep-shadow specks. */

static const unsigned char kTexGrassTop[kArtTextureBytes] = {
     6,  7,  6,  5,  6,  6,  5,  6,  5,  6,  5,  7,  6,  6,  7,  6,
     6,  6,  6,  6,  6,  5,  6,  6,  7,  7,  6,  6,  7,  6,  6,  6,
     6,  5,  5,  7,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  7,  6,  6,  7,  6,  5,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  5,  6,  6,  6,  6,  6,  6,  6,  5,  6,
     7,  7,  5,  6,  7,  6,  6,  6,  5,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  7,  7,  6,  6,  6,  6,  6,  5,  6,  6,  6,  6,  6,
     6,  6,  6,  5,  6,  6,  6,  6,  6,  6,  7,  6,  5,  5,  7,  7,
     6,  5,  5,  7,  7,  6,  5,  6,  6,  7,  6,  6,  6,  7,  6,  6,
     6,  6,  5,  7,  6,  6,  6,  7,  5,  6,  6,  7,  6,  5,  6,  6,
     6,  6,  6,  6,  7,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     7,  7,  6,  6,  6,  6,  6,  6,  5,  6,  6,  6,  6,  6,  7,  6,
     6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  6,  6,  7,  6,  6,  6,
     6,  6,  7,  5,  6,  6,  6,  6,  6,  6,  6,  7,  6,  6,  6,  6,
     6,  6,  7,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  5,  6,  7,
};

/* Grass side: a thin lip of green on one block of dirt.
 *
 * The lip used to be five rows of sixteen -- nearly a third of the face -- and
 * with the top diamond above it a grass block's green read as half a block, so
 * every one-block terrace looked like a dirt block with a half block of grass
 * balanced on it. Two rows, with a third row of ragged teeth dipping into the
 * soil, is a lip: the face is one block, dirt, with grass on its top edge. */

static const unsigned char kTexGrassSide[kArtTextureBytes] = {
     6,  6,  6,  6,  6,  6,  7,  6,  6,  6,  5,  6,  5,  6,  6,  5,
     6,  6,  6,  6,  7,  5,  6,  5,  7,  7,  5,  6,  6,  6,  6,  6,
    13, 13, 14, 14,  6,  6, 13, 13, 14,  6, 13, 13,  7, 13,  6, 14,
    14, 13, 14, 12, 13, 14, 13, 13, 14, 13, 14, 13, 14, 13, 14, 13,
    13, 13, 13, 13, 14, 13, 13, 13, 14, 13, 14, 13, 13, 13, 13, 12,
    12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 13, 13, 13, 14,
    13, 13, 14, 13, 13, 13, 13, 14, 12, 13, 13, 13, 13, 13, 13, 13,
    14, 13, 12, 13, 13, 13, 13, 13, 13, 14, 12, 13, 13, 12, 14, 13,
    13, 14, 13, 13, 13, 13, 13, 13, 14, 12, 12, 13, 13, 13, 12, 13,
    13, 13, 13, 13, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 12, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 12, 13, 12, 13, 13, 13, 13, 13, 13, 13, 13, 12, 13, 13,
    14, 14, 13, 13, 13, 13, 13, 13, 14, 13, 13, 13, 13, 13, 13, 13,
    14, 13, 13, 13, 13, 13, 13, 14, 13, 13, 14, 13, 13, 13, 13, 13,
    13, 13, 12, 12, 13, 12, 13, 13, 13, 13, 12, 14, 13, 12, 13, 13,
    14, 12, 13, 13, 13, 13, 13, 13, 13, 14, 13, 14, 12, 13, 13, 14,
};

/* Dirt: coarse brown grit across the dirt ramp with clumps of dark-brown
   pebbles.  It is deliberately darker than sand and lighter than bedrock, so
   the three terrains separate at a glance. */

/* Dirt used to use all four steps of its own hue inside a single block, so a
 * shaded face looked exactly like an unshaded one: the texture's own spread was
 * as wide as the shading step. The body is one step now, with sparse accents
 * and dark grit, and the face shade is what changes the tone. */
static const unsigned char kTexDirt[kArtTextureBytes] = {
    14, 14, 14, 13, 14, 14, 14, 15, 15, 14, 32, 14, 13, 13, 14, 14,
    14, 14, 14, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 13,
    14, 14, 14, 14, 14, 15, 15, 14, 14, 15, 15, 13, 13, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 32, 15, 14, 14, 14, 32, 14, 14, 14, 14,
    15, 14, 13, 14, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    15, 13, 14, 15, 14, 14, 32, 15, 14, 15, 14, 14, 32, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 32, 14, 14, 14, 14, 14, 14,
    15, 14, 14, 14, 14, 32, 14, 13, 14, 14, 32, 14, 14, 14, 14, 13,
    14, 14, 15, 14, 32, 14, 15, 14, 14, 13, 14, 14, 14, 14, 14, 14,
    14, 14, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 32, 14, 13, 32, 14, 14, 14, 15, 14, 15, 14, 14,
    14, 32, 14, 32, 14, 14, 13, 14, 14, 14, 15, 14, 13, 14, 14, 14,
    14, 13, 14, 14, 15, 14, 14, 15, 13, 14, 15, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 15, 13, 14, 32, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 15, 14, 14, 14, 13, 14, 14, 14, 14, 32,
    14, 13, 14, 14, 14, 14, 14, 32, 14, 14, 14, 15, 14, 14, 14, 14,
};

/* Stone: cool mid grey built from the stone and cool-grey ramps, with short
   one-pixel cold-dark cracks.  The cracks need the cold dark hue because the
   stone ramp's own dark steps would be snapped back to the face's step when
   the face is baked.  There are no 2x2 cells on purpose: at 1x a 2px cell
   reads as tile, not as rock. */

static const unsigned char kTexStone[kArtTextureBytes] = {
    22, 22, 22, 21, 22, 23, 21, 22, 21, 22, 23, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 21, 22, 23, 21, 22, 21, 22, 21, 22, 25, 23, 22,
    25, 59, 58, 59, 58, 59, 59, 22, 22, 23, 23, 25, 22, 21, 22, 22,
    22, 22, 22, 22, 22, 21, 23, 21, 21, 22, 22, 22, 22, 26, 21, 22,
    21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 21, 59, 22, 26, 21,
    22, 22, 23, 22, 22, 26, 22, 22, 23, 21, 22, 22, 59, 59, 23, 22,
    25, 22, 23, 22, 22, 22, 21, 22, 22, 22, 22, 22, 22, 22, 58, 22,
    22, 21, 23, 22, 22, 22, 22, 22, 21, 21, 21, 21, 22, 23, 22, 59,
    25, 22, 23, 23, 22, 21, 22, 22, 22, 21, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 23, 58, 22, 21, 22, 22, 22, 22, 22, 21,
    23, 21, 22, 25, 23, 21, 26, 23, 58, 22, 22, 26, 21, 22, 21, 22,
    23, 22, 21, 23, 21, 21, 26, 22, 21, 59, 21, 21, 22, 21, 22, 22,
    22, 21, 22, 22, 22, 22, 22, 22, 26, 59, 22, 22, 23, 22, 21, 21,
    22, 22, 22, 22, 23, 23, 22, 22, 22, 59, 22, 21, 22, 25, 21, 22,
    22, 23, 23, 22, 21, 22, 26, 25, 25, 22, 58, 59, 22, 22, 22, 22,
    22, 23, 22, 22, 22, 22, 22, 26, 21, 22, 23, 22, 23, 21, 25, 22,
};

/* Cobblestone: five stones of different sizes in a pinwheel, separated by
   one-pixel ink mortar.  Each stone is lit along its upper and left edge, so
   the ink below and right reads as shadow and the cell as a rounded pebble
   rather than a flat plate. */

static const unsigned char kTexCobblestone[kArtTextureBytes] = {
     0, 26, 26, 26, 26, 27, 26,  0, 26, 26, 27, 26, 26, 26, 26,  0,
    26, 22, 22, 22, 22, 21, 22,  0, 26, 22, 22, 22, 21, 22, 22,  0,
    26, 22, 22, 21, 22, 22, 22,  0, 27, 22, 21, 22, 22, 22, 22,  0,
    26, 21, 22, 22, 22, 22, 22,  0, 26, 22, 22, 22, 22, 22, 22,  0,
    26, 22, 22, 22, 22, 22, 21,  0, 26, 22, 22, 22, 22, 21,  0,  0,
    27, 22, 22, 22, 21, 22, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    26, 22, 21, 22, 22, 22,  0,  0, 26, 27, 26, 26, 26, 26, 27,  0,
     0,  0,  0,  0,  0,  0,  0,  0, 27, 22, 22, 22, 22, 22, 21,  0,
    26, 26, 27, 26, 26, 26, 26,  0, 26, 22, 22, 22, 21, 22, 22,  0,
    26, 22, 22, 21, 22, 22, 22,  0, 26, 22, 21, 22, 22, 22, 22,  0,
    27, 21, 22, 22, 22, 22, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    26, 22, 22, 22, 22, 22, 21,  0, 26, 27, 26, 26, 26, 26, 27,  0,
    26, 22, 22, 22, 21, 22, 22,  0, 27, 22, 22, 21, 22, 22, 22,  0,
    26, 22, 21, 22, 22, 22, 22,  0, 26, 21, 22, 22, 22, 22, 22,  0,
     0, 27, 22, 22, 22, 22,  0,  0, 26, 22, 22, 22, 22, 22, 21,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* Sand: pale tan with fine tonal noise and sparse dark-brown grains.  The
   grains are the only second hue, so they stay visible once a face is baked;
   the four sand steps keep a dune from looking like a flat fill on the unlit
   path. */

static const unsigned char kTexSand[kArtTextureBytes] = {
    29, 30, 29, 31, 30, 29, 29, 29, 28, 31, 28, 32, 29, 30, 28, 31,
    29, 29, 31, 28, 31, 29, 30, 30, 30, 29, 31, 29, 30, 29, 29, 30,
    31, 29, 31, 29, 31, 29, 29, 29, 31, 30, 29, 30, 28, 28, 28, 28,
    30, 29, 28, 29, 28, 29, 28, 30, 29, 29, 30, 30, 29, 30, 28, 29,
    28, 30, 31, 31, 28, 29, 28, 29, 28, 30, 30, 31, 29, 30, 31, 28,
    30, 31, 28, 30, 31, 29, 31, 29, 29, 31, 29, 28, 32, 31, 30, 30,
    29, 28, 32, 29, 29, 29, 30, 29, 28, 30, 30, 29, 28, 29, 30, 32,
    30, 29, 29, 29, 29, 31, 28, 30, 29, 29, 29, 31, 30, 28, 28, 29,
    30, 30, 29, 29, 30, 29, 29, 28, 31, 31, 30, 29, 29, 29, 31, 31,
    30, 28, 28, 30, 31, 30, 28, 29, 28, 29, 29, 33, 31, 30, 28, 28,
    31, 30, 29, 28, 30, 30, 29, 30, 29, 30, 31, 30, 29, 28, 28, 31,
    31, 28, 31, 30, 28, 29, 30, 29, 30, 28, 30, 29, 30, 33, 29, 30,
    29, 29, 29, 28, 31, 31, 31, 29, 29, 29, 29, 29, 30, 30, 29, 30,
    29, 29, 29, 29, 29, 31, 28, 29, 29, 30, 31, 31, 28, 30, 30, 29,
    31, 30, 31, 29, 29, 29, 30, 31, 29, 30, 31, 29, 28, 30, 32, 30,
    30, 28, 29, 30, 29, 29, 29, 33, 28, 29, 29, 30, 29, 28, 28, 29,
};

/* Gravel: dense small pebbles in grey and brown with dark gaps.  It is
   higher contrast and more varied than stone, so a gravel patch never
   merges into the stone around it. */

static const unsigned char kTexGravel[kArtTextureBytes] = {
    20,  0, 22, 20, 21, 20,  0, 23, 22, 20,  0, 24, 21,  0, 14, 12,
     0,  0,  1,  0, 22,  1,  0, 23, 20, 20,  0,  0,  0,  0,  0,  0,
    53, 53,  0,  1,  0,  1,  1,  0,  1,  0,  0, 24, 24, 24,  0, 21,
    52, 13,  0, 22, 22,  0, 13, 13, 13,  1, 24, 23, 23, 21,  1, 21,
    13, 13,  0, 22, 20,  0, 13, 12, 12,  1, 24, 21, 21,  0, 21, 20,
     0,  1,  0, 22, 20,  1, 13, 12, 12,  0,  0,  0,  1,  0,  0,  1,
    23,  0,  1,  1,  0,  0,  0,  0,  1,  0, 23,  1, 23,  1, 14, 14,
    22, 23,  0, 54, 54,  1, 22, 22, 22,  1, 23, 23, 21,  1, 14, 13,
    21,  1, 54, 53, 52,  0, 22, 20,  0,  0,  1, 23, 21,  1, 14, 12,
     0,  0, 54, 52, 52,  0,  0,  0,  0,  0,  1,  1,  0,  0,  1,  1,
    21,  1,  0,  1,  1,  1, 22, 22, 22,  0, 23, 23,  0,  1, 22, 22,
    20,  0, 24, 24, 24,  0,  1, 22, 20,  1, 23, 22, 23,  0, 22, 21,
    20,  0, 24, 23, 23, 24,  0, 22, 20,  0, 23, 22, 21,  1, 22, 21,
     0,  1, 24, 23, 23, 21,  1, 22, 20,  1, 23, 22, 21,  0, 22, 21,
    21,  0, 24, 24, 23, 21,  0, 22, 20,  1,  0, 23, 21,  0, 22, 21,
     0, 24, 21, 23, 23, 21,  1, 22, 21, 22,  0, 23, 21,  0, 22, 21
};

/* Planks: four three-row boards separated by one-pixel dark seams.  The end
   joints are staggered board to board, the grain runs in short horizontal
   dashes, and one knot sits on the third board.  Seams and knot use the dark
   brown hue, which survives baking; the plank ramp carries the grain. */

static const unsigned char kTexPlanks[kArtTextureBytes] = {
    18, 18, 18, 18, 18, 19, 19, 18, 18, 18, 18, 33, 18, 18, 18, 18,
    18, 18, 15, 15, 15, 18, 18, 18, 18, 19, 18, 33, 18, 18, 19, 18,
    17, 17, 17, 18, 18, 18, 18, 15, 15, 15, 18, 33, 17, 17, 17, 17,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
    18, 19, 18, 18, 33, 18, 18, 18, 18, 18, 18, 18, 18, 19, 18, 18,
    18, 18, 18, 18, 33, 18, 15, 15, 15, 18, 18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 33, 17, 17, 18, 18, 18, 17, 17, 17, 17, 17, 17,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
    18, 18, 18, 33, 33, 18, 18, 18, 18, 19, 18, 18, 18, 33, 18, 18,
    18, 18, 18, 33, 33, 16, 18, 18, 18, 18, 15, 15, 15, 33, 18, 18,
    17, 17, 17, 33, 18, 18, 18, 18, 18, 18, 18, 18, 33, 17, 17, 17,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
    18, 18, 18, 18, 18, 19, 19, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 15, 15, 15, 18, 18, 18, 18, 19, 18, 18, 18, 18, 19, 18,
    17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 17, 17, 17, 17, 17,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
};

/* Log side: vertical bark ridges one or two columns wide, broken by short
   horizontal notches and a knot.  Three browns alternate so the trunk reads
   as round without using a gradient. */

static const unsigned char kTexLogSide[kArtTextureBytes] = {
    34, 35, 33, 34, 32, 34, 35, 35, 35, 34, 32, 33, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 33, 34, 32, 34, 35, 33, 35, 33,
    34, 35, 33, 34, 35, 34, 35, 35, 33, 32, 32, 34, 35, 33, 35, 34,
    34, 35, 33, 34, 33, 34, 35, 35, 33, 34, 32, 34, 35, 33, 35, 34,
    34, 32, 32, 32, 35, 34, 35, 35, 33, 35, 32, 33, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 32, 32, 32, 34, 35, 33, 35, 34,
    34, 35, 35, 34, 32, 34, 35, 35, 32, 32, 35, 34, 35, 33, 35, 33,
    34, 33, 33, 34, 32, 34, 35, 35, 35, 34, 32, 34, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 33, 35, 33, 34, 32, 34, 32, 32, 32, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 33, 34, 32, 34, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 33, 34, 33, 34, 35, 33, 35, 34,
    34, 35, 33, 32, 32, 35, 35, 33, 33, 34, 32, 34, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 33, 34, 33, 34, 35, 33, 35, 33,
    34, 35, 33, 34, 33, 34, 35, 35, 33, 34, 33, 34, 35, 33, 35, 34,
    34, 33, 33, 35, 32, 34, 32, 32, 32, 34, 32, 33, 35, 33, 35, 34,
    34, 35, 33, 34, 32, 34, 35, 35, 33, 34, 33, 34, 35, 33, 35, 34
};

/* Log top: concentric rings around a dark heart, with deterministic
   per-pixel jitter and one radial crack.  The jitter is what stops the rings
   looking like a printed target. */

static const unsigned char kTexLogTop[kArtTextureBytes] = {
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 33, 35, 35, 35, 35, 35, 35, 33, 33, 32, 32, 32,
    32, 32, 32, 35, 35, 35, 34, 34, 34, 34, 35, 35, 33, 33, 32, 32,
    32, 32, 33, 35, 34, 34, 34, 35, 35, 35, 34, 35, 35, 33, 32, 32,
    32, 33, 33, 34, 34, 35, 35, 35, 34, 35, 35, 34, 35, 35, 33, 32,
    32, 33, 35, 34, 35, 35, 34, 34, 34, 34, 35, 35, 34, 35, 33, 32,
    32, 33, 35, 34, 35, 34, 34, 32, 32, 34, 34, 35, 34, 35, 33, 32,
    32, 33, 35, 34, 35, 35, 32, 32, 32, 34, 35, 35, 34, 35, 33, 32,
    32, 33, 35, 34, 35, 35, 34, 32, 34, 32, 35, 34, 34, 35, 33, 32,
    32, 33, 33, 34, 34, 35, 35, 35, 34, 35, 32, 34, 35, 35, 33, 32,
    32, 32, 33, 35, 34, 34, 35, 35, 35, 35, 34, 32, 35, 33, 32, 32,
    32, 32, 33, 33, 35, 34, 34, 34, 34, 34, 34, 35, 32, 32, 32, 32,
    32, 32, 32, 33, 33, 33, 35, 35, 35, 35, 33, 33, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

/* Leaves: deep green with clustered holes in the darkest green (index 8),
   because the texture format has no alpha.  A fifth of the pixels are holes
   and a sixth are grass-green sunlit tips, so a canopy reads as dappled
   foliage rather than a slab; the tips are a second hue because a step
   change alone would flatten when the face is baked. */

static const unsigned char kTexLeaves[kArtTextureBytes] = {
    10, 10, 10,  9,  5,  9,  9, 10, 10, 10,  9, 10,  5,  8, 10, 10,
    10,  9,  8,  5, 10, 10, 10, 11, 10, 10, 10, 11,  7, 11,  9,  8,
     9,  7,  5, 11,  9, 10,  9,  8,  5, 10,  8, 10, 10, 11,  6,  8,
    11,  6,  6, 10,  5,  5,  8,  8, 10, 11, 10,  6, 10, 10,  8,  7,
     9, 10,  9, 11, 11,  6, 10,  8,  8,  6, 10, 10, 10,  9,  7,  8,
    10, 11,  6,  5,  9, 10, 10, 10,  6, 10,  8,  9,  9,  8,  8, 11,
     9, 10, 10,  9,  9,  8,  9,  8,  8, 10,  9, 11, 11,  8, 11, 10,
    10,  9,  7,  6,  9, 10, 10,  8,  8,  9,  9, 11, 10, 10, 10,  9,
    10, 11,  9,  7,  8, 10,  6, 10,  9,  6,  7, 10, 10, 10,  9,  8,
     8,  8,  5,  9, 11,  9,  5,  6, 11,  7, 11,  8,  8,  9,  9,  8,
     6,  9, 10,  8,  8,  8, 10, 10, 11, 11,  8, 10,  6,  9, 10, 10,
     6, 10, 11, 10, 10,  8,  8,  8, 10, 10, 10,  8,  8, 10,  9, 11,
     8,  9,  9,  8, 11,  9, 11, 10,  9, 11, 10,  8, 10, 11,  8, 11,
     9, 10,  6, 10, 10, 10, 10,  9,  9,  9, 11,  6, 11, 10,  8, 11,
     9,  9,  8,  7,  9, 10, 11,  9, 10, 10,  8, 10, 10, 10, 11, 10,
     8, 10,  8, 10, 10,  6, 10,  6,  6, 11,  8, 10, 10,  9,  9,  8,
};

/* Brick: running bond.  Three-pixel brick courses, one-pixel light mortar,
   and vertical joints offset on alternate courses.  Each brick has a light
   top row and a dark bottom row, which is the whole trick. */

static const unsigned char kTexBrick[kArtTextureBytes] = {
    39, 39, 39, 39, 39, 39, 39, 27, 39, 39, 39, 39, 39, 39, 39, 39,
    39, 38, 38, 38, 38, 38, 38, 27, 38, 38, 38, 38, 38, 38, 39, 38,
    37, 37, 37, 37, 37, 37, 37, 27, 37, 37, 37, 37, 37, 37, 37, 37,
    26, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 26, 27, 27, 27,
    39, 39, 39, 27, 39, 39, 39, 39, 39, 39, 39, 27, 39, 39, 39, 39,
    38, 38, 38, 27, 39, 38, 38, 38, 38, 38, 38, 27, 38, 38, 38, 38,
    37, 37, 37, 27, 37, 37, 37, 37, 37, 37, 37, 27, 37, 37, 37, 37,
    27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27,
    39, 39, 39, 39, 39, 39, 39, 27, 39, 39, 39, 39, 39, 39, 39, 39,
    38, 39, 38, 38, 38, 38, 38, 27, 39, 38, 38, 38, 38, 38, 38, 39,
    37, 37, 37, 37, 37, 37, 37, 27, 37, 37, 37, 37, 37, 37, 37, 37,
    27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27,
    39, 39, 39, 27, 39, 39, 39, 39, 39, 39, 39, 27, 39, 39, 39, 39,
    38, 38, 38, 27, 38, 39, 38, 38, 38, 38, 38, 27, 39, 38, 38, 38,
    37, 37, 37, 27, 37, 37, 37, 37, 37, 37, 37, 27, 37, 37, 37, 37,
    27, 27, 27, 26, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 26
};

/* Snow: near white with a handful of grey and pale-blue flecks.  The blue
   is barely present so snow stays white but does not look like blank paper. */

static const unsigned char kTexSnow[kArtTextureBytes] = {
    51, 51, 51, 50, 51, 51, 51, 51, 51, 51, 51, 51, 50, 51, 51, 51,
    51, 51, 51, 51, 51, 51, 51, 50, 50, 51, 51, 51, 51, 51, 51, 50,
    51, 43, 51, 51, 51, 51, 51, 50, 50, 51, 51, 51, 51, 51, 51, 51,
    51, 51, 51, 51, 51, 51, 51, 50, 51, 51, 51, 51, 51, 50, 50, 51,
    51, 51, 51, 50, 51, 51, 51, 51, 50, 49, 51, 51, 51, 51, 51, 51,
    49, 51, 51, 51, 51, 50, 51, 50, 51, 50, 51, 51, 51, 51, 51, 51,
    51, 51, 51, 51, 51, 51, 51, 51, 51, 43, 51, 51, 51, 51, 49, 50,
    51, 51, 51, 50, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 50,
    50, 51, 51, 50, 51, 51, 50, 51, 51, 51, 51, 51, 51, 51, 51, 51,
    51, 51, 51, 51, 50, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51,
    51, 50, 51, 50, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51,
    51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 50, 51, 51, 51, 51,
    51, 50, 43, 51, 51, 51, 51, 51, 51, 51, 50, 51, 51, 51, 50, 51,
    50, 51, 50, 51, 50, 49, 50, 51, 50, 50, 49, 51, 50, 51, 51, 51,
    50, 51, 51, 50, 51, 51, 51, 51, 51, 51, 50, 51, 51, 51, 51, 51,
    51, 51, 51, 50, 51, 51, 50, 43, 51, 51, 51, 51, 51, 51, 51, 50
};

/* Bedrock: near-black with irregular chunks of dark and mid grey.  The
   chunk edges are jagged; a smooth pattern here would read as stone. */

static const unsigned char kTexBedrock[kArtTextureBytes] = {
     2,  1,  1,  2,  1,  2,  1,  1,  2, 20, 20,  2,  2,  2,  0,  0,
     2,  1,  2,  1,  1,  1,  2,  2,  2, 20, 20,  1,  1,  1,  0,  0,
     2,  2,  2,  2,  2,  1,  1,  1,  1, 20, 20,  1,  2,  2,  0,  0,
     1,  2,  2,  0,  0,  0,  2,  1,  1,  0,  0,  0,  1,  1,  1,  2,
     2, 20, 20, 20, 20,  0,  1,  1, 21, 21, 56,  1,  0,  0,  0,  2,
     2, 20,  2,  2,  2,  0,  1, 56, 21, 21, 20, 20, 20,  0,  0,  2,
     2,  1,  2,  2,  2,  1,  0, 56, 21, 21, 20, 20, 20, 56,  0, 21,
     2,  1,  2,  2,  2,  1,  2,  2, 56, 56,  1,  1, 56, 56,  0,  1,
     1,  2,  1,  2,  2,  1,  2,  2,  1,  1,  2,  2, 56, 56, 20, 20,
     2,  2,  1,  1,  2,  2,  1,  0,  0,  2,  1,  2, 56, 56, 20, 20,
     1,  2,  1,  2,  2,  2,  1,  0,  0,  2,  1,  1,  1,  1, 20, 20,
     1,  2, 21, 21, 21, 21,  1,  0,  0,  2,  2,  1,  2,  1,  1,  2,
     2,  2,  2,  2,  1,  1,  1,  2,  1,  1,  1,  2,  1,  1,  1,  1,
     2,  1,  2,  2,  1,  2,  2,  1,  1,  1,  1,  1,  2,  2,  1,  2,
     1,  1,  2,  1,  2,  1,  1,  1,  2,  2,  1,  1,  2,  1,  1,  1,
     2,  2,  2,  1,  1,  2,  2,  1,  1,  2,  1,  2,  2,  1,  2,  2
};

/* Water: mid blue with three broken horizontal glints in the pale-blue hue,
   and dark troughs beneath them.  The glints are a second hue because a step
   change alone would flatten when the face is baked. */

static const unsigned char kTexWater[kArtTextureBytes] = {
    46, 46, 46, 45, 44, 46, 47, 46, 45, 46, 46, 46, 46, 46, 47, 46,
    46, 46, 46, 45, 46, 47, 45, 45, 46, 44, 46, 46, 47, 45, 47, 44,
    47, 42, 45, 42, 42, 43, 41, 42, 42, 42, 42, 41, 42, 43, 46, 45,
    45, 45, 46, 46, 41, 42, 42, 41, 42, 43, 42, 42, 46, 45, 45, 46,
    46, 47, 46, 46, 46, 47, 46, 46, 47, 47, 47, 46, 47, 46, 47, 46,
    44, 44, 44, 44, 45, 47, 45, 44, 44, 46, 44, 44, 46, 47, 44, 44,
    45, 46, 46, 45, 46, 46, 47, 45, 45, 46, 45, 45, 47, 46, 47, 44,
    46, 46, 46, 45, 47, 46, 45, 47, 47, 46, 46, 45, 47, 47, 45, 46,
    41, 42, 46, 41, 46, 42, 42, 41, 41, 41, 42, 47, 41, 46, 43, 46,
    46, 47, 42, 42, 41, 42, 43, 42, 42, 42, 42, 41, 42, 47, 46, 45,
    46, 46, 45, 46, 47, 47, 47, 46, 46, 47, 46, 45, 46, 47, 47, 46,
    45, 46, 45, 44, 45, 44, 46, 46, 44, 45, 46, 44, 44, 45, 45, 45,
    44, 45, 46, 46, 46, 46, 46, 46, 47, 46, 47, 46, 45, 46, 46, 46,
    46, 42, 42, 41, 43, 42, 42, 43, 41, 42, 41, 43, 43, 44, 46, 46,
    47, 44, 45, 44, 46, 41, 41, 42, 42, 46, 42, 42, 46, 44, 45, 44,
    46, 46, 46, 46, 46, 44, 45, 46, 46, 47, 47, 46, 46, 46, 45, 45,
};

/* Glass: pale cyan frame, near-white pane, one diagonal highlight.  With
   no alpha the pane is a light fill rather than a transparent hole, so the
   frame carries the read. */

static const unsigned char kTexGlass[kArtTextureBytes] = {
    43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
    43, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 51, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 50, 50, 51, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 50, 51, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 51, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 51, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 51, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 51, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 51, 50, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 51, 50, 50, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 51, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 42, 43,
    43, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 43,
    43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43
};

/* ---------------------------------------------------------------- */
/* Accessors                                                         */
/* ---------------------------------------------------------------- */

unsigned short Art_RGB(int index)
{
    if (index < 0) {
        index = 0;
    } else if (index >= kArtPaletteEntries) {
        index = kArtPaletteEntries - 1;
    }
    return kPalette[index];
}

void Art_RGBColor(int index, unsigned short *red, unsigned short *green,
                  unsigned short *blue)
{
    unsigned short packed = Art_RGB(index);

    /* A nibble n is the 8-bit component 0x11 * n; 0x1111 repeats that byte
       in both halves, which is the form a QuickDraw RGBColor wants. */
    if (red != NULL) {
        *red = (unsigned short)(((packed >> 8) & 0x0F) * 0x1111);
    }
    if (green != NULL) {
        *green = (unsigned short)(((packed >> 4) & 0x0F) * 0x1111);
    }
    if (blue != NULL) {
        *blue = (unsigned short)((packed & 0x0F) * 0x1111);
    }
}

unsigned char Art_ShadeIndex(unsigned char index, short factor)
{
    unsigned char base;
    unsigned char step;

    if (index >= kArtPaletteEntries) {
        index = kArtPaletteEntries - 1;
    }
    base = (unsigned char)(index & 0xFCu);

    /* Four bands chosen so the renderer's face factors land on the intended
       steps: <=100 is the darkest step, 154 the left-face step, 205 the
       right-face step, 256 full sun.  Descending tests keep the result
       monotonic in factor. */
    if (factor >= 230) {
        step = 3;
    } else if (factor >= 185) {
        step = 2;
    } else if (factor >= 110) {
        step = 1;
    } else {
        step = 0;
    }

    return (unsigned char)(base | step);
}

const unsigned char *Art_Texture(BlockID id, short face)
{
    /* Numeric ids are the contract in world.h/blocks.h; they are kept
       literal here so this module does not depend on the look system being
       compiled in. */
    switch (id) {
    case 1: /* grass */
        return (face == 0) ? kTexGrassTop : kTexGrassSide;
    case 2: /* dirt */
        return kTexDirt;
    case 3: /* stone */
        return kTexStone;
    case 4: /* cobblestone */
        return kTexCobblestone;
    case 5: /* sand */
        return kTexSand;
    case 6: /* gravel */
        return kTexGravel;
    case 7: /* planks */
        return kTexPlanks;
    case 8: /* log */
        return (face == 0) ? kTexLogTop : kTexLogSide;
    case 9: /* leaves */
        return kTexLeaves;
    case 10: /* brick */
        return kTexBrick;
    case 11: /* snow */
        return kTexSnow;
    case 12: /* bedrock */
        return kTexBedrock;
    case 13: /* water */
        return kTexWater;
    case 14: /* glass */
        return kTexGlass;
    case 0: /* empty */
    default:
        /* Unknown ids draw as stone so a corrupt world cell is still solid. */
        return kTexStone;
    }
}
