/*
 * dither.c - 4x4 ordered dithering.
 *
 * The 4x4 Bayer matrix assigns each pixel phase a rank 0..15 such that the
 * ranks grow as evenly as possible over the tile.  Darkening "everything
 * ranked below level" therefore adds one pixel phase at a time, with the new
 * pixels as far apart as the 4x4 tile allows: that is what turns four palette
 * steps into a smooth gradient without any colour arithmetic.
 *
 * The matrix is the canonical Bayer construction (recursively M2 = [[0,2],
 * [3,1]] doubled by 4 and offset by the base).  It is written out rather than
 * generated so the compiler can place it in read-only data and the host test
 * can check the exact ordering.
 */

#include "dither.h"

#define kBayerSize 4
#define kDitherLevels 16

static const unsigned char kBayer4x4[kBayerSize][kBayerSize] = {
    { 0,  8,  2, 10 },
    { 12, 4, 14,  6 },
    { 3, 11,  1,  9 },
    { 15, 7, 13,  5 }
};

Boolean Dither_On(short x, short y, short level)
{
    /* Convert to unsigned before masking: the matrix repeats forever, so
       negative screen coordinates must wrap consistently instead of indexing
       out of bounds. */
    unsigned int column = (unsigned short)x & (kBayerSize - 1);
    unsigned int row = (unsigned short)y & (kBayerSize - 1);

    if (level <= 0)
        return false;
    if (level >= kDitherLevels)
        return true;

    return (Boolean)(kBayer4x4[row][column] < (unsigned int)level);
}

short Dither_Level(short factor, short span)
{
    int dark;
    int level;

    if (span < 1)
        span = 1;
    if (span > 3)
        span = 3;

    /* How far below full light the factor sits, 0..256. */
    if (factor < 0)
        factor = 0;
    if (factor > 256)
        factor = 256;
    dark = 256 - factor;

    /* dark*span/256 is the darkening measured in whole ramp steps; times 16
       gives the fraction in sixteenths, so the level is (dark*span)/16.
       Adding half the divisor rounds to the nearest sixteenth, and the clamp
       keeps span 2 and 3 from exceeding one full step of dithering. */
    level = (dark * span + kDitherLevels / 2) / kDitherLevels;
    if (level > kDitherLevels)
        level = kDitherLevels;
    return (short)level;
}
