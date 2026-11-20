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

#include <stddef.h>   /* NULL, for the callers that do not want a face */

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

/*
 * Is (dx, dy), relative to the top-face centre, inside the visible side quad?
 * The left face occupies (-16,0)..(0,24) as a parallelogram sloping down to the
 * right; the right face is its mirror. `which` is 0 for left, 1 for right.
 */
static Boolean SideContains(short dx, short dy, short which)
{
    short u = dx;
    short top, bottom;

    if (which == 0) {
        if (u < -kTileW / 2 || u > 0)
            return false;
        top = (short)((u + kTileW / 2) / 2);
    }
    else {
        if (u < 0 || u > kTileW / 2)
            return false;
        top = (short)((kTileW / 2 - u) / 2);
    }

    bottom = (short)(top + kTileH);
    return (Boolean)(dy >= top && dy < bottom);
}

Boolean Pick_Tile(short screenX, short screenY, short camX, short camY,
                  short *outX, short *outY, short *outZ)
{
    return Pick_TileFace(screenX, screenY, camX, camY, outX, outY, outZ, NULL);
}

Boolean Pick_TileFace(short screenX, short screenY, short camX, short camY,
                      short *outX, short *outY, short *outZ, short *outFace)
{
    short gx, gy;
    short bestX = 0, bestY = 0, bestZ = -1, bestFace = kPickFaceTop;
    Boolean found = false;
    int x, y;

    if (!Iso_Unproject(screenX, screenY, camX, camY, &gx, &gy))
        return false;

    for (x = (int)gx - kPickWindow; x <= (int)gx + kPickWindow; x++) {
        if (x < 0 || x >= kWorldX)
            continue;

        for (y = (int)gy - kPickWindow; y <= (int)gy + kPickWindow; y++) {
            short top, z;

            if (y < 0 || y >= kWorldY)
                continue;

            top = World_TopZ((short)x, (short)y);
            if (top < 0)
                continue;

            /* Down the column: the first face that contains the point, from the
             * top of the column downwards, is the one being pointed at. Only
             * faces that are actually drawn can be hit, so a buried side is
             * skipped -- otherwise a point on a hill selects the block behind
             * it. */
            for (z = top; z >= 0; z--) {
                short px, py, face;

                if (World_Get((short)x, (short)y, z) == 0)
                    continue;

                Iso_Project((short)x, (short)y, z, camX, camY, &px, &py);

                if (World_Get((short)x, (short)y, (short)(z + 1)) == 0 &&
                    DiamondContains(screenX, screenY, (short)x, (short)y, z,
                                    camX, camY)) {
                    face = kPickFaceTop;
                }
                else if (World_Get((short)x, (short)(y + 1), z) == 0 &&
                         SideContains(screenX - px, screenY - py, 0)) {
                    face = kPickFaceLeft;
                }
                else if (World_Get((short)(x + 1), (short)y, z) == 0 &&
                         SideContains(screenX - px, screenY - py, 1)) {
                    face = kPickFaceRight;
                }
                else {
                    continue;
                }

                /* Nearest wins: a higher block, then the later tile in painter
                 * order. */
                if (!found || z > bestZ ||
                    (z == bestZ && (x + y) > (bestX + bestY))) {
                    found = true;
                    bestX = (short)x;
                    bestY = (short)y;
                    bestZ = z;
                    bestFace = face;
                }
                break;
            }
        }
    }

    if (!found)
        return false;

    *outX = bestX;
    *outY = bestY;
    /* Callers expect the surface level; the block itself is one below. */
    *outZ = (short)(bestZ + 1);
    if (outFace != NULL)
        *outFace = bestFace;
    return true;
}
