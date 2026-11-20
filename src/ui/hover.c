/*
 * hover.c - which block the pointer is over, and the wireframe that marks it.
 *
 * One state, one projection, one draw. The old code had the pick and the draw
 * in two places with two ideas of the coordinate space, which is why the ring
 * never landed on the block under the pointer.
 *
 * The wireframe is written through the sprite layer (the same path the figure
 * and the terrain use) rather than with QuickDraw strokes: a stroke is clipped
 * to the update region, so a repaint that did not cover the ring left the old
 * one on screen.
 */

#include <Quickdraw.h>
#include <Types.h>

#include "hover.h"
#include "../game/iso.h"
#include "../game/pick.h"
#include "../game/world.h"
#include "sprite_sheet.h"

/* Slack around the tile's screen box: the box is what gets damaged, and a
 * couple of pixels of margin keeps the wireframe's ends inside it. */
#define kBoxSlack 2

static Boolean sValid = false;
/* The last query, so a pass that both handles an event and then idles does not
 * run the picker twice for the same pointer. */
static Point sLastQuery = {-1, -1};
static short sLastCamX = 0;
static short sLastCamY = 0;
static Boolean sHaveQuery = false;
static short sTileX = 0;
static short sTileY = 0;
static short sLevel = 0;
static Rect sDisplay;
static short sCamX = 0;
static short sCamY = 0;

static void BoxFor(short x, short y, short z, Rect *out)
{
    short sx, sy;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx = (short)(sx + sDisplay.left);
    sy = (short)(sy + sDisplay.top);

    /* Top diamond plus the two side faces: the wireframe runs kTileH below the
     * centre, so the damaged box does too. */
    SetRect(out,
            (short)(sx - kTileW / 2 - kBoxSlack),
            (short)(sy - kTileH / 2 - kBoxSlack),
            (short)(sx + kTileW / 2 + kBoxSlack),
            (short)(sy + kTileH / 2 + kTileH + kBoxSlack));
}

void Hover_Reset(void)
{
    sValid = false;
    sHaveQuery = false;
}

Boolean Hover_Update(Point local, const Rect *display, short camX, short camY,
                     Rect *dirty)
{
    short x = 0;
    short y = 0;
    short surface = 0;
    Boolean valid = false;
    Rect oldBox;
    Rect newBox;

    if (display == NULL) {
        return false;
    }
    sDisplay = *display;
    sCamX = camX;
    sCamY = camY;

    if (sHaveQuery && local.h == sLastQuery.h && local.v == sLastQuery.v &&
        camX == sLastCamX && camY == sLastCamY) {
        return false;
    }
    sLastQuery = local;
    sLastCamX = camX;
    sLastCamY = camY;
    sHaveQuery = true;

    if (PtInRect(local, display)) {
        valid = Pick_Tile((short)(local.h - display->left),
                          (short)(local.v - display->top),
                          camX, camY, &x, &y, &surface);
        /* Pick_Tile reports the level a new block would occupy; the wireframe
         * marks the block whose top face was pointed at. */
        surface = (short)(surface - 1);
        if (surface < 0) {
            valid = false;
        }
    }

    if (valid == sValid && valid &&
        x == sTileX && y == sTileY && surface == sLevel) {
        return false;
    }
    if (!valid && !sValid) {
        return false;
    }

    /* Capture whether a box existed before the state is overwritten: the union
     * below must never read an uninitialised rectangle, which is what happened
     * the first time the pointer found a tile after being over nothing. */
    {
        Boolean wasValid = sValid;

        if (wasValid) {
            BoxFor(sTileX, sTileY, sLevel, &oldBox);
        }
        sValid = valid;
        sTileX = x;
        sTileY = y;
        sLevel = surface;

        if (dirty == NULL) {
            return true;
        }
        if (!valid) {
            *dirty = oldBox;
            return true;
        }
        BoxFor(sTileX, sTileY, sLevel, &newBox);
        if (wasValid) {
            UnionRect(&newBox, &oldBox, dirty);
        }
        else {
            *dirty = newBox;
        }
    }
    return true;
}

Boolean Hover_Valid(void)
{
    return sValid;
}

BlockID Hover_Block(void)
{
    if (!sValid) {
        return 0;
    }
    return World_Get(sTileX, sTileY, sLevel);
}

void Hover_Tile(short *x, short *y, short *z)
{
    if (x != NULL) {
        *x = sTileX;
    }
    if (y != NULL) {
        *y = sTileY;
    }
    if (z != NULL) {
        *z = sLevel;
    }
}

void Hover_Draw(const Rect *display, short camX, short camY)
{
    short sx, sy;
    short top, left, right;

    if (!sValid || display == NULL) {
        return;
    }

    /* Project with the camera the frame is being drawn with, not the one cached
     * when the tile was picked: a pan between the two left the ring a whole
     * camera step away from its block. */
    Iso_Project(sTileX, sTileY, sLevel, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);

    top   = (short)(sy - kTileH / 2);
    left  = (short)(sx - kTileW / 2);
    right = (short)(sx + kTileW / 2);

    /* QuickDraw, inside the update pass: the strip's text proves that path
     * draws here, and the damage rect that carries this repaint is exactly the
     * box below, so the strokes are not clipped away. */
    PenNormal();
    PenSize(1, 1);
    ForeColor(blackColor);
    PenPat(&qd.black);

    MoveTo(sx, top);
    LineTo(right, sy);
    LineTo(sx, (short)(sy + kTileH / 2));
    LineTo(left, sy);
    LineTo(sx, top);

    MoveTo(sx, (short)(sy + kTileH / 2));
    LineTo(sx, (short)(sy + kTileH / 2 + kTileH));
    MoveTo(left, sy);
    LineTo(left, (short)(sy + kTileH));
    MoveTo(right, sy);
    LineTo(right, (short)(sy + kTileH));

}
