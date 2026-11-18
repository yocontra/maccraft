/*
 * world.c - block storage for the isometric voxel world.
 *
 * The world is a fixed 48x48x8 grid held in a single static array
 * (18,432 bytes); nothing here touches the heap.  All access goes
 * through the range-checked accessors below.
 */

#include "world.h"

#define kEmptyBlock 0
#define kGrassBlock 3

#define kPlateauMin 18
#define kPlateauMax 29
#define kPlateauTop 1

static BlockID sBlocks[kWorldX][kWorldY][kWorldZ];

static Boolean InRange(short x, short y, short z)
{
    return (Boolean)(x >= 0 && x < kWorldX &&
                     y >= 0 && y < kWorldY &&
                     z >= 0 && z < kWorldZ);
}

void World_Reset(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                sBlocks[x][y][z] = kEmptyBlock;

    /* Flat 12x12 grass plateau centred on (24,24), two blocks tall. */
    for (x = kPlateauMin; x <= kPlateauMax; x++)
        for (y = kPlateauMin; y <= kPlateauMax; y++)
            for (z = 0; z <= kPlateauTop; z++)
                sBlocks[x][y][z] = kGrassBlock;
}

BlockID World_Get(short x, short y, short z)
{
    if (!InRange(x, y, z))
        return kEmptyBlock;
    return sBlocks[x][y][z];
}

Boolean World_Set(short x, short y, short z, BlockID id)
{
    if (!InRange(x, y, z))
        return false;
    sBlocks[x][y][z] = id;
    return true;
}

short World_Height(short x, short y)
{
    short z, height = 0;

    if (x < 0 || x >= kWorldX || y < 0 || y >= kWorldY)
        return 0;

    for (z = 0; z < kWorldZ; z++) {
        if (sBlocks[x][y][z] == kEmptyBlock)
            break;
        height++;
    }
    return height;
}

short World_TopZ(short x, short y)
{
    short z;

    if (x < 0 || x >= kWorldX || y < 0 || y >= kWorldY)
        return -1;

    for (z = kWorldZ - 1; z >= 0; z--) {
        if (sBlocks[x][y][z] != kEmptyBlock)
            return z;
    }
    return -1;
}
