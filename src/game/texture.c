/*
 * texture.c - two-tone block textures: a base fill plus a grain stencil.
 *
 * All data is static and const; the accessors are pure lookups (no QuickDraw
 * calls, no port/pen/pattern changes), so the renderer can ask for a face's
 * parts at any point in a paint pass.  Patterns are 8 bytes, MSB = leftmost
 * pixel, and each group is labelled with the ink density it carries.
 *
 * Density families used below (fraction of the 64 pattern bits set):
 *   50%   = 32/64, 25% = 16/64, 12.5% = 8/64.
 * The 1-bit base patterns use exactly those three densities and always rank
 * top < right < left, so a face stays lighter than the faces below/right of it
 * even when the grain pass is added on top (mono grain is at most 25% black).
 * Colour grain patterns are one density step lighter than their mono twins
 * because the colour screen already shows the base hue.
 */

#include "texture.h"

#include "world.h"

/* ---------------------------------------------------------------- */
/* Grass: light green top, mid green right, dark green left.         */
/* ---------------------------------------------------------------- */

static const RGBColor kGrassTopBase   = {0x3300, 0xCC00, 0x3300};
static const RGBColor kGrassLeftBase  = {0x0000, 0x8800, 0x2200};
static const RGBColor kGrassRightBase = {0x0000, 0x9900, 0x0000};

/* Grain inks on a colour screen: a second shade of the face's own hue (darker
 * where the face is light, lighter where the face is dark). */
static const RGBColor kGrassTopInk   = {0x0000, 0x9900, 0x0000};
static const RGBColor kGrassLeftInk  = {0x0000, 0x6600, 0x0000};
static const RGBColor kGrassRightInk = {0x0000, 0x9900, 0x0000};

/* 1-bit base: 12.5% top, 25% right, 50% left. */
static const Pattern kGrassTopBasePat = /* 12.5% sparse dots (8/64) */
    {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00};
static const Pattern kGrassLeftBasePat = /* 50% 1px checker (32/64) */
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
static const Pattern kGrassRightBasePat = /* 25% 2px checker (16/64) */
    {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22};

/* Colour grain: fine speckle on top, one-column vertical fibre on the sides. */
static const Pattern kGrassTopGrain = /* 12.5% fine speckle (8/64) */
    {0x80, 0x20, 0x08, 0x02, 0x80, 0x20, 0x08, 0x02};
static const Pattern kGrassLeftGrain = /* 12.5% vertical fibre, col 3 (8/64) */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
static const Pattern kGrassRightGrain = /* 12.5% vertical fibre, col 1 (8/64) */
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40};

/* 1-bit grain: coarser and darker than the colour grain (25% everywhere). */
static const Pattern kGrassTopGrainMono = /* 25% coarse speckle (16/64) */
    {0xAA, 0x00, 0x55, 0x00, 0xAA, 0x00, 0x55, 0x00};
static const Pattern kGrassLeftGrainMono = /* 25% vertical fibre, cols 2+6 */
    {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const Pattern kGrassRightGrainMono = /* 25% vertical fibre, cols 1+5 */
    {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44};

/* ---------------------------------------------------------------- */
/* Dirt: mid brown top, brown right, dark brown left.                */
/* ---------------------------------------------------------------- */

static const RGBColor kDirtTopBase   = {0xAA00, 0x5500, 0x0000};
static const RGBColor kDirtLeftBase  = {0x8800, 0x4400, 0x1100};
static const RGBColor kDirtRightBase = {0x8800, 0x4400, 0x0000};

static const RGBColor kDirtTopInk   = {0x6600, 0x3300, 0x0000};
static const RGBColor kDirtLeftInk  = {0x3300, 0x1A00, 0x0000};
static const RGBColor kDirtRightInk = {0x5500, 0x2A00, 0x0000};

/* 1-bit base: 12.5% top, 25% right, 50% left. Dirt's dithers are arranged
 * differently from grass/stone so the blocks stay separable in black/white. */
static const Pattern kDirtTopBasePat = /* 12.5% coarse sparse dots (8/64) */
    {0x99, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00};
static const Pattern kDirtLeftBasePat = /* 50% horizontal bands (32/64) */
    {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
static const Pattern kDirtRightBasePat = /* 25% horizontal bands (16/64) */
    {0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Colour grain: coarse pebbles on top, thin horizontal strata on the sides. */
static const Pattern kDirtTopGrain = /* 25% 2x2 pebbles (16/64) */
    {0xCC, 0xCC, 0x00, 0x00, 0x33, 0x33, 0x00, 0x00};
static const Pattern kDirtLeftGrain = /* 12.5% strata, row 0 (8/64) */
    {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const Pattern kDirtRightGrain = /* 12.5% strata, row 1 (8/64) */
    {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* 1-bit grain: darker strata and chunkier pebbles. */
static const Pattern kDirtTopGrainMono = /* ~25% 3x3 pebbles (18/64) */
    {0xE0, 0xE0, 0xE0, 0x00, 0x0E, 0x0E, 0x0E, 0x00};
static const Pattern kDirtLeftGrainMono = /* 25% strata, rows 0+4 (16/64) */
    {0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00};
static const Pattern kDirtRightGrainMono = /* 25% strata, rows 2+6 (16/64) */
    {0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00};

/* ---------------------------------------------------------------- */
/* Stone: light grey top, mid grey right, dark grey left.            */
/* ---------------------------------------------------------------- */

static const RGBColor kStoneTopBase   = {0xAAAA, 0xAAAA, 0xAAAA};
static const RGBColor kStoneLeftBase  = {0x6666, 0x6666, 0x6666};
static const RGBColor kStoneRightBase = {0x8888, 0x8888, 0x8888};

static const RGBColor kStoneTopInk   = {0x7777, 0x7777, 0x7777};
static const RGBColor kStoneLeftInk  = {0x3333, 0x3333, 0x3333};
static const RGBColor kStoneRightInk = {0x5555, 0x5555, 0x5555};

/* 1-bit base: 12.5% top, 25% right, 50% left, phase-flipped against grass so
 * the two grey faces of adjacent block types do not line up. */
static const Pattern kStoneTopBasePat = /* 12.5% blocky sparse (8/64) */
    {0xCC, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00};
static const Pattern kStoneLeftBasePat = /* 50% 1px checker, flipped (32/64) */
    {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
static const Pattern kStoneRightBasePat = /* 25% 2px checker, flipped (16/64) */
    {0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88};

/* Colour grain: sparse 2x2 blocks on top, one-pixel diagonals on the sides. */
static const Pattern kStoneTopGrain = /* 12.5% sparse 2x2 blocks (8/64) */
    {0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const Pattern kStoneLeftGrain = /* 12.5% diagonal, '/' (8/64) */
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
static const Pattern kStoneRightGrain = /* 12.5% diagonal, '\' (8/64) */
    {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

/* 1-bit grain: one density step denser than the colour diagonals. */
static const Pattern kStoneTopGrainMono = /* 25% 2x2 blocks (16/64) */
    {0xCC, 0xCC, 0x00, 0x00, 0x33, 0x33, 0x00, 0x00};
static const Pattern kStoneLeftGrainMono = /* 25% diagonal, '/' (16/64) */
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x81};
static const Pattern kStoneRightGrainMono = /* 25% diagonal, '\' (16/64) */
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x81};

/* ---------------------------------------------------------------- */
/* Scene background.                                                 */
/* ---------------------------------------------------------------- */

static const RGBColor kSkyColor = {0x4400, 0x7700, 0xBB00};

static const Pattern kSkyPattern = /* 25% 2px checker (16/64) */
    {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22};

/* ---------------------------------------------------------------- */
/* Per-block lookup table, indexed by block id - 1 (ids 1..3).       */
/* ---------------------------------------------------------------- */

typedef struct {
    const RGBColor *baseColor[3];   /* solid fill, colour displays */
    const Pattern  *basePattern[3]; /* dither fill, 1-bit displays */
    const Pattern  *grainColor[3];  /* grain stencil, colour displays */
    const Pattern  *grainMono[3];   /* grain stencil, 1-bit displays */
    const RGBColor *grainInk[3];    /* grain ink, colour displays */
} BlockTexture;

static const BlockTexture kTextures[3] = {
    /* id 1: dirt */
    {{&kDirtTopBase, &kDirtLeftBase, &kDirtRightBase},
     {&kDirtTopBasePat, &kDirtLeftBasePat, &kDirtRightBasePat},
     {&kDirtTopGrain, &kDirtLeftGrain, &kDirtRightGrain},
     {&kDirtTopGrainMono, &kDirtLeftGrainMono, &kDirtRightGrainMono},
     {&kDirtTopInk, &kDirtLeftInk, &kDirtRightInk}},

    /* id 2: stone */
    {{&kStoneTopBase, &kStoneLeftBase, &kStoneRightBase},
     {&kStoneTopBasePat, &kStoneLeftBasePat, &kStoneRightBasePat},
     {&kStoneTopGrain, &kStoneLeftGrain, &kStoneRightGrain},
     {&kStoneTopGrainMono, &kStoneLeftGrainMono, &kStoneRightGrainMono},
     {&kStoneTopInk, &kStoneLeftInk, &kStoneRightInk}},

    /* id 3: grass */
    {{&kGrassTopBase, &kGrassLeftBase, &kGrassRightBase},
     {&kGrassTopBasePat, &kGrassLeftBasePat, &kGrassRightBasePat},
     {&kGrassTopGrain, &kGrassLeftGrain, &kGrassRightGrain},
     {&kGrassTopGrainMono, &kGrassLeftGrainMono, &kGrassRightGrainMono},
     {&kGrassTopInk, &kGrassLeftInk, &kGrassRightInk}}};

/* Grain ink for a 1-bit display: the ink is always black there, so the grain
 * can only darken the base dither. */
static const RGBColor kMonoInk = {0x0000, 0x0000, 0x0000};

/* Clamp a face index; only kFaceTop..kFaceRight are meaningful. */
static short FaceIndex(short face)
{
    if (face < kFaceTop || face > kFaceRight) {
        return kFaceTop;
    }
    return face;
}

/* Clamp a block id to the textured range. Empty (0) and anything unknown fall
 * back to dirt, matching the selector's default swatch. */
static const BlockTexture *TextureFor(BlockID id)
{
    if (id < 1 || id > 3) {
        id = 1;
    }
    return &kTextures[id - 1];
}

/* ---------------------------------------------------------------- */
/* Public lookups.                                                   */
/* ---------------------------------------------------------------- */

const RGBColor *Texture_BaseColor(BlockID id, short face)
{
    return TextureFor(id)->baseColor[FaceIndex(face)];
}

/* The table is const but the published API hands out Pattern *; no caller may
 * write through it, and the cast keeps the agreed signatures. */
Pattern *Texture_BasePattern(BlockID id, short face)
{
    return (Pattern *)TextureFor(id)->basePattern[FaceIndex(face)];
}

Pattern *Texture_GrainPattern(BlockID id, short face, Boolean hasColor)
{
    const BlockTexture *t;
    short f;

    t = TextureFor(id);
    f = FaceIndex(face);
    if (hasColor) {
        return (Pattern *)t->grainColor[f];
    }
    return (Pattern *)t->grainMono[f];
}

const RGBColor *Texture_GrainColor(BlockID id, short face, Boolean hasColor)
{
    if (!hasColor) {
        return &kMonoInk;
    }
    return TextureFor(id)->grainInk[FaceIndex(face)];
}

short Texture_GrainInset(BlockID id, short face)
{
    (void)id;
    (void)face;
    return 1;
}

const RGBColor *Texture_SkyColor(void)
{
    return &kSkyColor;
}

Pattern *Texture_SkyPattern(void)
{
    return (Pattern *)&kSkyPattern;
}
