/*
 * blocks.c - the look of every block kind: base colours plus grain stencils.
 *
 * All data is static and const.  The accessors are pure lookups, so the
 * renderer can call them in any order during a paint pass.
 *
 * A look carries one hue, repeated for all three faces.  The light model
 * (game/light) is the only place that darkens a side; pre-darkening here as
 * well would multiply the two factors and turn stone and bedrock black.
 *
 * Colours sit exactly on the display's six-level cube (0x00, 0x33, 0x66,
 * 0x99, 0xCC, 0xFF per gun).  Values between levels would be quantised down a
 * step at draw time, so every kind names the cube levels it uses in its own
 * comment; dark kinds stay at 0x33 rather than 0x00 so their grain survives.
 *
 * Patterns are 8 bytes, MSB = leftmost pixel, a set bit is ink.  Densities are
 * quoted per pattern so the grain stays a texture rather than a second colour.
 */

#include "blocks.h"

/* Brightness steps for Block_Tone, in percent: two above and two below the
   base, small enough to stay the same material but large enough to separate
   neighbouring tops on an 8-bit-per-channel display. */
#define kToneSteps 4

static const unsigned short kToneScale[kToneSteps] = { 106, 102, 98, 94 };

/* One hue for all three faces, as the three BlockColor entries of a BlockLook
   (so it must sit inside the look's own braces).  Shading belongs to the light
   model, so the palette must not pre-darken a side. */
#define BLOCK_HUE(r, g, b) { r, g, b }, { r, g, b }, { r, g, b }

/* ---------------------------------------------------------------- */
/* Grass: saturated mid green with a dry-grass speckle.              */
/* ---------------------------------------------------------------- */

static const BlockLook kGrassLook = {
    /* Cube R 0x66 G 0x99 B 0x33: saturated mid green. */
    BLOCK_HUE(0x6666, 0x9999, 0x3333),
    /* Dry tufts, ~6%: broken vertical strokes, not a uniform dither. */
    { 0x10, 0x10, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00 },
    /* Sparse vertical strands, ~9%. */
    { 0x20, 0x20, 0x20, 0x20, 0x00, 0x04, 0x04, 0x04 }
};

/* ---------------------------------------------------------------- */
/* Dirt: mid brown with dark grit.                                   */
/* ---------------------------------------------------------------- */

static const BlockLook kDirtLook = {
    /* Cube R 0x66 G 0x33 B 0x00: mid brown, darker than log. */
    BLOCK_HUE(0x6666, 0x3333, 0x0000),
    /* Isolated grit, ~6%. */
    { 0x01, 0x00, 0x20, 0x00, 0x00, 0x84, 0x00, 0x10 },
    /* Grit in short vertical runs, ~12%. */
    { 0x24, 0x00, 0x24, 0x00, 0x24, 0x00, 0x24, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Stone: cool grey with fine noise.                                 */
/* ---------------------------------------------------------------- */

static const BlockLook kStoneLook = {
    /* Cube 0x99 grey: the reference stone tone. */
    BLOCK_HUE(0x9999, 0x9999, 0x9999),
    /* Fine, irregular flecks, ~8%. */
    { 0x02, 0x00, 0x40, 0x08, 0x00, 0x21, 0x00, 0x04 },
    /* The same noise shifted off the top's phase, ~8%. */
    { 0x10, 0x00, 0x02, 0x00, 0x40, 0x08, 0x00, 0x01 }
};

/* ---------------------------------------------------------------- */
/* Cobblestone: chunky mortar grid.                                  */
/* ---------------------------------------------------------------- */

static const BlockLook kCobbleLook = {
    /* Cube 0x66 grey: one step below stone so the mortar grid reads. */
    BLOCK_HUE(0x6666, 0x6666, 0x6666),
    /* Two full mortar courses plus short cross joints, ~34%. */
    { 0x08, 0x08, 0x08, 0xFF, 0x08, 0x08, 0x08, 0xFF },
    /* Horizontal courses only, ~25%. */
    { 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Sand: pale yellow with sparse dark flecks.                        */
/* ---------------------------------------------------------------- */

static const BlockLook kSandLook = {
    /* Cube R 0xCC G 0xCC B 0x99: pale yellow. */
    BLOCK_HUE(0xCCCC, 0xCCCC, 0x9999),
    /* Very sparse flecks, ~6%. */
    { 0x04, 0x00, 0x20, 0x00, 0x80, 0x00, 0x08, 0x00 },
    /* Flecks drifting along the strata, ~5%. */
    { 0x00, 0x20, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40 }
};

/* ---------------------------------------------------------------- */
/* Gravel: dense, random-ish grain.                                  */
/* ---------------------------------------------------------------- */

static const BlockLook kGravelLook = {
    /* Cube R 0x99 G 0x99 B 0x66: warm grey, distinct from stone and sand. */
    BLOCK_HUE(0x9999, 0x9999, 0x6666),
    /* Evenly scattered pebbles, ~37%. */
    { 0x52, 0xA4, 0x49, 0x92, 0x25, 0x49, 0xA4, 0x92 },
    /* A different scatter so the side does not repeat the top, ~42%. */
    { 0x96, 0x49, 0xB2, 0x24, 0x69, 0x92, 0x4D, 0x26 }
};

/* ---------------------------------------------------------------- */
/* Planks: horizontal brown lines.                                   */
/* ---------------------------------------------------------------- */

static const BlockLook kPlanksLook = {
    /* Cube R 0xCC G 0x99 B 0x66: warm wood, brighter than log. */
    BLOCK_HUE(0xCCCC, 0x9999, 0x6666),
    /* Two board seams, 25%. */
    { 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF },
    /* Seams offset by a row so the side grain does not line up with the top. */
    { 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Log: rings on the top, vertical grain and a knot on the side.     */
/* ---------------------------------------------------------------- */

static const BlockLook kLogLook = {
    /* Cube R 0x99 G 0x66 B 0x33: darker wood than planks. */
    BLOCK_HUE(0x9999, 0x6666, 0x3333),
    /* A single growth ring, ~25%. */
    { 0x18, 0x24, 0x42, 0x81, 0x81, 0x42, 0x24, 0x18 },
    /* Vertical fibres with a dark knot in the middle rows, ~34%. */
    { 0x22, 0x22, 0x22, 0x18, 0x18, 0x22, 0x22, 0x22 }
};

/* ---------------------------------------------------------------- */
/* Leaves: bright green with holes, denser than grass.               */
/* ---------------------------------------------------------------- */

static const BlockLook kLeavesLook = {
    /* Cube R 0x33 G 0xCC B 0x33: brighter green than grass. */
    BLOCK_HUE(0x3333, 0xCCCC, 0x3333),
    /* Scattered gaps, ~14%. */
    { 0x42, 0x08, 0x40, 0x00, 0x24, 0x00, 0x08, 0x41 },
    /* Gaps in vertical sprays, ~12%. */
    { 0x81, 0x00, 0x18, 0x00, 0x81, 0x00, 0x18, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Brick: red with running-bond mortar.                              */
/* ---------------------------------------------------------------- */

static const BlockLook kBrickLook = {
    /* Cube R 0x99 G 0x33 B 0x33: brick red. */
    BLOCK_HUE(0x9999, 0x3333, 0x3333),
    /* Course line every four rows, head joints staggered, ~28%. */
    { 0xFF, 0x00, 0x08, 0x00, 0xFF, 0x00, 0x80, 0x00 },
    /* The same bond shifted half a course down, ~28%. */
    { 0x00, 0xFF, 0x00, 0x80, 0x00, 0xFF, 0x00, 0x08 }
};

/* ---------------------------------------------------------------- */
/* Snow: near white with a faint blue speckle.                       */
/* ---------------------------------------------------------------- */

static const BlockLook kSnowLook = {
    /* Cube 0xCC grey: near white, one step below FF so it never clips. */
    BLOCK_HUE(0xCCCC, 0xCCCC, 0xCCCC),
    /* Two flecks only, ~3%: snow must not look sandy. */
    { 0x00, 0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x04, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Bedrock: very dark grey, dense noise.                             */
/* ---------------------------------------------------------------- */

static const BlockLook kBedrockLook = {
    /* Cube 0x33 grey: the only near-black kind; 0x00 would hide the grain. */
    BLOCK_HUE(0x3333, 0x3333, 0x3333),
    /* Dense broken rock, ~72%. */
    { 0x6F, 0xBD, 0xF6, 0xDB, 0x7E, 0xD5, 0xAB, 0xED },
    /* A second dense scatter so sides never repeat the top, ~73%. */
    { 0xB7, 0x6E, 0xDB, 0x7D, 0xE7, 0xBB, 0x6D, 0xF6 }
};

/* ---------------------------------------------------------------- */
/* Water: blue with horizontal highlight bands.                      */
/* ---------------------------------------------------------------- */

static const BlockLook kWaterLook = {
    /* Cube R 0x33 G 0x66 B 0xCC: saturated blue. */
    BLOCK_HUE(0x3333, 0x6666, 0xCCCC),
    /* Two broken highlight bands, ~12%. */
    { 0x00, 0x3C, 0x00, 0x00, 0x00, 0xC3, 0x00, 0x00 },
    /* Bands a few rows apart from the top's, ~12%. */
    { 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0xC3, 0x00 }
};

/* ---------------------------------------------------------------- */
/* Glass: very pale cyan, almost no grain.                           */
/* ---------------------------------------------------------------- */

static const BlockLook kGlassLook = {
    /* Cube R 0xCC G 0xFF B 0xFF: pale cyan, a step away from snow. */
    BLOCK_HUE(0xCCCC, 0xFFFF, 0xFFFF),
    /* Two glints, ~3%: any more and glass reads as frosted stone. */
    { 0x00, 0x00, 0x10, 0x00, 0x00, 0x40, 0x00, 0x00 },
    { 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 }
};

/* Indexed by id - 1; the order must match the enum in blocks.h. */
/* Torch: warm wood with a bright flame tip. Only the hotbar swatch and the
 * mono fallback ever draw it as a block; the world pass draws a post. */
static const BlockLook kTorchLook = {
    /* Cube CC9900: warm flame orange. Display-legal levels only. */
    { 0xCCCC, 0x9999, 0x0000 },
    { 0xCCCC, 0x9999, 0x0000 },
    { 0xCCCC, 0x9999, 0x0000 },
    /* A flame band across the middle rows of the top, and a shaft on the side. */
    { 0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00 },
    { 0x3C, 0x3C, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x00 }
};

static const BlockLook kLooks[kBlockKinds] = {
    /* 1  */ kGrassLook,
    /* 2  */ kDirtLook,
    /* 3  */ kStoneLook,
    /* 4  */ kCobbleLook,
    /* 5  */ kSandLook,
    /* 6  */ kGravelLook,
    /* 7  */ kPlanksLook,
    /* 8  */ kLogLook,
    /* 9  */ kLeavesLook,
    /* 10 */ kBrickLook,
    /* 11 */ kSnowLook,
    /* 12 */ kBedrockLook,
    /* 13 */ kWaterLook,
    /* 14 */ kGlassLook,
    /* 15 */ kTorchLook
};

#undef BLOCK_HUE

/* Multiply one 16-bit channel by a percentage without FPU or overflow: the
   largest product is 65535 * 106, well inside 32 bits. */
static unsigned short ToneChannel(unsigned short value, unsigned short scale)
{
    unsigned long scaled = (unsigned long)value * (unsigned long)scale / 100UL;

    if (scaled > 0xFFFFUL)
        scaled = 0xFFFFUL;
    return (unsigned short)scaled;
}

/* Reverse the eight bits of one pattern row.  Used for the right face so the
   grain leans the other way and the two sides of a column differ. */
static unsigned char Reverse8(unsigned char bits)
{
    unsigned char result = 0;
    short i;

    for (i = 0; i < 8; i++) {
        result = (unsigned char)((result << 1) | (bits & 1));
        bits = (unsigned char)(bits >> 1);
    }
    return result;
}

/* Rotate a pattern row left by n bits (n taken mod 8). */
static unsigned char Rotate8(unsigned char bits, short n)
{
    n = (short)(n & 7);
    if (n == 0)
        return bits;
    return (unsigned char)((bits << n) | (bits >> (8 - n)));
}

const BlockLook *Block_Look(BlockID id)
{
    if (id >= kBlockGrass && id <= kBlockKinds)
        return &kLooks[id - kBlockGrass];
    return &kLooks[kBlockStone - kBlockGrass];
}

short Block_Variation(long x, long y, long z)
{
    /* Weighted sum modulo four.  All three weights are odd, so stepping any
       single coordinate by one always changes the low two bits: neighbours
       can never land on the same variation, which is what keeps a plateau
       from merging into one tone.  Unsigned arithmetic keeps the result
       well-defined for negative coordinates. */
    unsigned long hash = (unsigned long)x * 7UL
                       + (unsigned long)y * 11UL
                       + (unsigned long)z * 5UL;

    return (short)(hash & 3UL);
}

BlockColor Block_Tone(BlockColor base, short variation)
{
    BlockColor out;
    unsigned short scale = kToneScale[(unsigned short)variation % kToneSteps];

    out.red = ToneChannel(base.red, scale);
    out.green = ToneChannel(base.green, scale);
    out.blue = ToneChannel(base.blue, scale);
    return out;
}

/* Names in palette order, so the status strip and the selector agree with the
 * ids the world stores. */
static const char *const kNames[kBlockKinds + 1] = {
    "block",      /* 0, empty: never shown, but the table must be dense */
    "grass",
    "dirt",
    "stone",
    "cobblestone",
    "sand",
    "gravel",
    "planks",
    "log",
    "leaves",
    "brick",
    "snow",
    "bedrock",
    "water",
    "glass",
    "torch"
};

const char *Block_Name(BlockID id)
{
    if (id > kBlockKinds)
        return "block";
    return kNames[id];
}

void Block_Grain(BlockID id, short face, short variation,
                 unsigned char out[8])
{
    const BlockLook *look = Block_Look(id);
    const unsigned char *src = (face == kBlockFaceTop) ? look->grainTop
                                                       : look->grainSide;
    short step = (short)((unsigned short)variation & 3);
    short rotation = (short)(step * 2);
    short i;

    for (i = 0; i < 8; i++) {
        unsigned char bits = src[(i + step) & 7];

        if (face == kBlockFaceRight)
            bits = Reverse8(bits);
        out[i] = Rotate8(bits, rotation);
    }
}
