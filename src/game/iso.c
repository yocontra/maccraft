/*
 * iso.c - integer-only isometric projection for 32x16 tiles.
 *
 * A tile occupies a diamond kTileW wide and kTileH tall, so the centre
 * of tile (x,y) sits half a tile right and a quarter tile down for each
 * step in x, and half a tile left and a quarter tile down for each step
 * in y.  Both coordinates are scaled by kTileH/2 = 8, which is the
 * vertical distance between adjacent tile centres (true 2:1 isometric).
 *
 *   screenX = (x - y) * (kTileW / 2) + camX
 *   screenY = (x + y) * (kTileH / 2) - z * (kTileH / 2) + camY
 *
 * with kTileW/2 = 16 and kTileH/2 = 8.  The largest magnitude produced
 * for x=y=47, z=7 is 696, far inside a 16-bit short.
 *
 * Iso_Unproject inverts the z=0 plane of that mapping:
 *
 *   sx = FloorDiv(screenX - camX, kTileW / 2)  = x - y
 *   sy = FloorDiv(screenY - camY, kTileH / 2)  = x + y
 *   x  = FloorDiv(sx + sy, 2)
 *   y  = FloorDiv(sy - sx, 2)
 *
 * All arithmetic is int until the final store.  C's / truncates toward
 * zero, which would pick the wrong tile for negative offsets, hence the
 * explicit floor division.
 */

#include "iso.h"
#include "world.h" /* kWorldX / kWorldY bounds */

static short FloorDiv(short a, short b)
{
    short q = (short)(a / b);

    /* Truncation rounded toward zero; step down when it overshot. */
    if ((short)(a % b) != 0 && ((a < 0) != (b < 0)))
        q--;
    return q;
}

void Iso_Project(short x, short y, short z, short camX, short camY,
                 short *screenX, short *screenY)
{
    int sx = ((int)x - (int)y) * (kTileW / 2) + (int)camX;
    int sy = ((int)x + (int)y) * (kTileH / 2) - (int)z * (kTileH / 2) + (int)camY;

    *screenX = (short)sx;
    *screenY = (short)sy;
}

Boolean Iso_Unproject(short screenX, short screenY, short camX, short camY,
                      short *x, short *y)
{
    short sx, sy, tx, ty;

    sx = FloorDiv((short)((int)screenX - (int)camX), kTileW / 2);
    sy = FloorDiv((short)((int)screenY - (int)camY), kTileH / 2);

    tx = FloorDiv((short)(sx + sy), 2);
    ty = FloorDiv((short)(sy - sx), 2);

    if (tx < 0 || tx >= kWorldX || ty < 0 || ty >= kWorldY)
        return false;

    *x = tx;
    *y = ty;
    return true;
}

void Iso_UnprojectClamped(short screenX, short screenY, short camX, short camY,
                          short *x, short *y)
{
    short sx, sy, tx, ty;

    sx = FloorDiv((short)((int)screenX - (int)camX), kTileW / 2);
    sy = FloorDiv((short)((int)screenY - (int)camY), kTileH / 2);

    tx = FloorDiv((short)(sx + sy), 2);
    ty = FloorDiv((short)(sy - sx), 2);

    if (tx < 0) {
        tx = 0;
    }
    else if (tx >= kWorldX) {
        tx = kWorldX - 1;
    }
    if (ty < 0) {
        ty = 0;
    }
    else if (ty >= kWorldY) {
        ty = kWorldY - 1;
    }

    *x = tx;
    *y = ty;
}
