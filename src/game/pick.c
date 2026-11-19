/*
 * pick.c - resolve a click in the play area to the block surface under it.
 *
 * Unprojecting the point on the z=0 plane alone is wrong for raised terrain:
 * a surface z levels up projects z * kTileH/2 pixels higher, and inverting
 * the z=0 mapping shifts the answer by one tile along -x and -y per level.
 * A surface at the top of the world can therefore unproject up to kWorldZ
 * tiles away from its real column.  The search window is kWorldZ + 1 tiles
 * on each side: kWorldZ covers that shift and the extra tile absorbs the
 * half-tile lateral rounding of the diamond.
 *
 * The visible surface of a column is the face a block would be placed on,
 * World_TopZ() + 1; an empty column has surface 0, so bare ground remains a
 * valid placement target.  Of every surface whose top-face diamond contains
 * the point, the greatest z is the one nearest the camera (a hill hides the
 * ground behind it).  Diamonds of the same level only share edges, so a tie
 * means the point is on an edge; the greater x + y is the later tile in
 * painter order, i.e. the one drawn on top.
 */

#include "pick.h"

#include "iso.h"
#include "world.h"

#define kPickWindow (kWorldZ + 1)

/*
 * A tile's top face is a diamond kTileW wide and kTileH tall, so a point is
 * inside it when |dx|/(kTileW/2) + |dy|/(kTileH/2) <= 1, cross-multiplied to
 * keep the comparison in integers.
 */
static Boolean DiamondContains(short screenX, short screenY, short x, short y,
                               short z, short camX, short camY)
{
    short px, py;
    int dx, dy;

    Iso_Project(x, y, z, camX, camY, &px, &py);

    dx = (int)screenX - (int)px;
    dy = (int)screenY - (int)py;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    return (Boolean)(dx * (kTileH / 2) + dy * (kTileW / 2) <=
                     (kTileW / 2) * (kTileH / 2));
}

Boolean Pick_Tile(short screenX, short screenY, short camX, short camY,
                  short *outX, short *outY, short *outZ)
{
    short gx, gy;
    short bestX = 0, bestY = 0, bestZ = -1;
    Boolean found = false;
    int x, y;

    if (!Iso_Unproject(screenX, screenY, camX, camY, &gx, &gy))
        return false;

    for (x = (int)gx - kPickWindow; x <= (int)gx + kPickWindow; x++) {
        if (x < 0 || x >= kWorldX)
            continue;

        for (y = (int)gy - kPickWindow; y <= (int)gy + kPickWindow; y++) {
            short z;

            if (y < 0 || y >= kWorldY)
                continue;

            z = (short)(World_TopZ((short)x, (short)y) + 1);
            if (!DiamondContains(screenX, screenY, (short)x, (short)y, z,
                                 camX, camY))
                continue;

            if (!found || z > bestZ ||
                (z == bestZ && x + y > (int)bestX + (int)bestY)) {
                found = true;
                bestX = (short)x;
                bestY = (short)y;
                bestZ = z;
            }
        }
    }

    if (!found)
        return false;

    *outX = bestX;
    *outY = bestY;
    *outZ = bestZ;
    return true;
}
