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
static short sFace = kPickFaceTop;
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
    short face = kPickFaceTop;
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
        valid = Pick_TileFace((short)(local.h - display->left),
                              (short)(local.v - display->top),
                              camX, camY, &x, &y, &surface, &face);
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
        /* The ring marks the block, not the empty cell above it. With a level
         * worth a whole tile height, drawing at the surface level put the
         * wireframe one block above its block, which read as the picker being
         * one tile off. */
        sLevel = (short)(surface > 0 ? surface - 1 : 0);
        sFace = face;

        if (dirty == NULL) {
            return true;
        }
        if (!valid) {
            *dirty = oldBox;
            return true;
        }
        /* Only the box the ring is *leaving* is damaged. The ring at its new
         * tile is drawn by the same paint through the pixel path, which is not
         * clipped by the update region -- so unioning the two boxes (which for a
         * fast pointer spans everything in between) buys nothing. */
        BoxFor(sTileX, sTileY, sLevel, &newBox);
        if (wasValid) {
            *dirty = oldBox;
        }
        else {
            SetRect(dirty, 0, 0, 0, 0);
        }
    }
    return true;
}

Boolean Hover_Valid(void)
{
    return sValid;
}

short Hover_Face(void)
{
    return sFace;
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

/* Save-under for the ring: the pixels it covered, and where they were. Moving
 * the ring costs one restore plus one save, and damages nothing -- which is the
 * whole point: a pointer move used to schedule a repaint of the tiles under the
 * old ring, and those tiles were usually identical to what was already there. */
#define kRingW 40
#define kRingH 40
/* The saved box covers the whole wireframe (32 rows) plus slack, starting 12 px
 * above the tile centre. */


static unsigned long sRingUnder[kRingW * kRingH];
static Boolean sRingSaved = false;
static Rect sRingRect;

static void RingRectFor(short sx, short sy, Rect *out)
{
    /* The cube wireframe spans sy-8 (diamond top) to sy+24 (side bottoms), so
     * the saved box has to cover all 32 rows plus slack. A box that was two rows
     * short left the bottom edge of the old ring on screen. */
    SetRect(out, (short)(sx - kRingW / 2), (short)(sy - 12),
            (short)(sx + kRingW / 2), (short)(sy + 28));
}

/* Forget the saved pixels: something else painted over them (a world repaint, a
 * pan), so they are no longer what is on screen. */
void Hover_Forget(void)
{
    sRingSaved = false;
}

Boolean Hover_RingRect(Rect *out)
{
    if (!sRingSaved || out == NULL) {
        return false;
    }
    *out = sRingRect;
    return true;
}

/* Put the ring back where it belongs, without a repaint.
 *
 * Called from the idle loop after Hover_Update: the old pixels are restored,
 * the ring is stroked at the current tile, and the pixels under it are saved for
 * next time. */
void Hover_Redraw(const Rect *display, short camX, short camY)
{
    short sx, sy;
    Rect box;

    if (!sValid || display == NULL) {
        if (sRingSaved) {
            Sprite_WriteRect(&sRingRect, sRingUnder, kRingW);
            sRingSaved = false;
        }
        return;
    }

    Iso_Project(sTileX, sTileY, sLevel, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);
    RingRectFor(sx, sy, &box);

    /* The cursor lives in the same framebuffer: saving or restoring over it
     * would paint a white block where the pointer used to be. Mac OS has to be
     * told to take it away while the pixels underneath are touched. */
    HideCursor();

    if (sRingSaved) {
        Sprite_WriteRect(&sRingRect, sRingUnder, kRingW);
        sRingSaved = false;
    }
    if (box.left == sRingRect.left && box.top == sRingRect.top && sRingSaved) {
        ShowCursor();
        return;
    }

    Sprite_BeginFrame();
    Sprite_SetView(display);
    Hover_Draw(display, camX, camY);
    Sprite_ReadRect(&box, sRingUnder, kRingW);
    Sprite_EndFrame();

    ShowCursor();

    sRingRect = box;
    sRingSaved = true;
}

void Hover_Draw(const Rect *display, short camX, short camY)
{
    short sx, sy;
    short i;

    if (!sValid || display == NULL) {
        return;
    }

    /* Project with the camera the frame is being drawn with, not the one cached
     * when the tile was picked: a pan between the two left the ring a whole
     * camera step away from its block. */
    Iso_Project(sTileX, sTileY, sLevel, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);

    /* Pixels, not QuickDraw strokes: a stroke is clipped to the update region,
     * and the ring is deliberately drawn outside the damaged box -- the box only
     * covers where the ring *was*. Two pixels per row along the 2:1 slopes is
     * the whole geometry.
     *
     * The cursor lives in the same framebuffer, so Mac OS restores a white patch
     * wherever the ring is written under it; hiding it for the few hundred
     * pixels of the ring is cheaper than the artefact. */
    HideCursor();
    for (i = 0; i <= kTileH / 2; i++) {
        short rowY = (short)(sy - kTileH / 2 + i);
        Rect run;

        SetRect(&run, (short)(sx - i * 2), rowY, (short)(sx - i * 2 + 2), (short)(rowY + 1));
        Sprite_FillRect(&run, 0x000000UL);
        SetRect(&run, (short)(sx + i * 2 - 1), rowY, (short)(sx + i * 2 + 1), (short)(rowY + 1));
        Sprite_FillRect(&run, 0x000000UL);
    }
    for (i = 1; i <= kTileH / 2; i++) {
        short rowY = (short)(sy + i - 1);
        Rect run;

        SetRect(&run, (short)(sx - kTileW / 2 + i * 2), rowY,
                (short)(sx - kTileW / 2 + i * 2 + 2), (short)(rowY + 1));
        Sprite_FillRect(&run, 0x000000UL);
        SetRect(&run, (short)(sx + kTileW / 2 - i * 2 - 2), rowY,
                (short)(sx + kTileW / 2 - i * 2), (short)(rowY + 1));
        Sprite_FillRect(&run, 0x000000UL);
    }

    /* Lighten the face a block would be placed against, so it is obvious which
     * side the next block lands on: the same baked face, redrawn in the
     * brightest shade band. There is no alpha on this display, so a brighter
     * opaque redraw is the readable version of "highlight". */
    {
        BlockID id = Hover_Block();

        if (id != 0) {
            switch (sFace) {
            case kPickFaceLeft:
                Sprite_Draw((short)(sx - kTileW / 2), sy, id, kBlockFaceLeft, 0);
                break;
            case kPickFaceRight:
                Sprite_Draw(sx, sy, id, kBlockFaceRight, 0);
                break;
            default:
                Sprite_Draw((short)(sx - kTileW / 2), (short)(sy - kTileH / 2), id,
                            kBlockFaceTop, 0);
                break;
            }
        }
    }

    /* The three vertical edges at the silhouette corners. */
    {
        Rect edge;

        SetRect(&edge, sx, (short)(sy + kTileH / 2), (short)(sx + 1),
                (short)(sy + kTileH / 2 + kTileH));
        Sprite_FillRect(&edge, 0x000000UL);
        SetRect(&edge, (short)(sx - kTileW / 2), sy, (short)(sx - kTileW / 2 + 1),
                (short)(sy + kTileH));
        Sprite_FillRect(&edge, 0x000000UL);
        SetRect(&edge, (short)(sx + kTileW / 2 - 1), sy, (short)(sx + kTileW / 2),
                (short)(sy + kTileH));
        Sprite_FillRect(&edge, 0x000000UL);
    }

    ShowCursor();
}
