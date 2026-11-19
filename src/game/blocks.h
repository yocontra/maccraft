#ifndef BLOCKS_H
#define BLOCKS_H

#include "world.h"

/*
 * blocks.h - how every block kind looks.
 *
 * This is the only place that knows a block's colours and grain patterns.
 * The renderer asks for a look, a per-block brightness step and a stamped
 * pattern; nothing here touches QuickDraw state or allocates memory, so the
 * calls are safe inside a paint pass.
 *
 * Colours are 16-bit RGBColor components.  System 7 quantises each component
 * to its high byte, so every value here is 0xNNNN with both bytes equal; a
 * value such as 0x1234 would silently lose its low byte.
 */

/* Block kind ids, shared with World_Get/World_Set.  0 is empty.  The world
   stores the id as-is and does not range-check it, so the renderer may see
   anything; Block_Look() maps unknown ids to stone. */
enum {
    kBlockEmpty = 0,
    kBlockGrass = 1,
    kBlockDirt = 2,
    kBlockStone = 3,
    kBlockCobblestone = 4,
    kBlockSand = 5,
    kBlockGravel = 6,
    kBlockPlanks = 7,
    kBlockLog = 8,
    kBlockLeaves = 9,
    kBlockBrick = 10,
    kBlockSnow = 11,
    kBlockBedrock = 12,
    kBlockWater = 13,
    kBlockGlass = 14,
    kBlockKinds = 14 /* ids 1..kBlockKinds have a look */
};

/* Face indices used by Block_Grain. */
enum {
    kBlockFaceTop = 0,
    kBlockFaceLeft = 1,
    kBlockFaceRight = 2
};

typedef struct {
    unsigned short red, green, blue;
} BlockColor;

typedef struct {
    BlockColor top, left, right;

    /* QuickDraw pattern bytes: 8 rows, MSB is the leftmost pixel, a set bit
       is ink.  Grain is drawn over the face colour in a second shade. */
    unsigned char grainTop[8];
    unsigned char grainSide[8];
} BlockLook;

/* Look up the colours and patterns for a kind.  Never returns NULL: empty,
   out-of-range and future ids all get the stone look so a corrupt world cell
   still draws as something solid. */
const BlockLook *Block_Look(BlockID id);

/* Position hash in 0..3.  Adjacent blocks get different values, which is what
   stops a field of identical tops from merging into one flat diamond. */
short Block_Variation(long x, long y, long z);

/* The base colour stepped by one of four brightness offsets (+6%, +2%, -2%,
   -6%) chosen by variation.  Channels clamp at 0xFFFF. */
BlockColor Block_Tone(BlockColor base, short variation);

/* Fill out[8] with the kind's pattern for a face, shifted by variation so two
   neighbouring blocks do not stamp the identical 8x8 tile. */
void Block_Grain(BlockID id, short face, short variation, unsigned char out[8]);

#endif /* BLOCKS_H */
