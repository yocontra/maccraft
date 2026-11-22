#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextUtils.h>
#include <Processes.h>
#include <string.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "../game/blocks.h"
#include "../game/iso.h"
#include "../game/scene.h"
#include "../game/sky.h"
#include "../game/pick.h"
#include "../game/player.h"
#include "../game/save.h"
#include "../sound/sfx.h"
#include "../game/world.h"
#include "game_window.h"
#include "hover.h"
#include "particles.h"
#include "hud.h"
#include "remote.h"
#include "world_view.h"
#include "avatar.h"
#include "sprite_sheet.h"

/* Module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;       /* Main content area */
static Rect sBlockSelectorRect;

/* The surface under the mouse. The selection box and the click handler both use
 * it, so what is highlighted is exactly what a click acts on. */
static Rect sTitleStripRect;
static unsigned long sSwingUntil;
static short Swing(void)
{
    long left = (long)sSwingUntil - (long)TickCount();

    if (left <= 0) {
        return 0;
    }
    if (left > 8) {
        left = 8;
    }
    return (short)left;
}
static Rect sDisplayRect;       /* Game view area */

/* Camera offset in display-local coordinates (see Iso_Project). */
static Player sPlayer;
static short sIntentX = 0;
static short sIntentY = 0;
static Boolean sJumpHeld = false;
/* Movement intent stays alive until this tick, refreshed by every repeat, so a
 * held key walks instead of taking one step per repeat. */
static unsigned long sIntentUntil = 0;

#define kIntentHoldTicks 12

/* Harness counters: where the seconds actually go. */
long Sprite_Work[6];
/* Pixels the world pass covered in total: mean repaint area per frame is this
 * divided by Sprite_Work[3], and it is what tells a small hover box from a full
 * frame without guessing. */
long Sprite_PaintedPixels;
/* Times the selection box has been drawn: tells "never called" from "drawn but
 * invisible". */
/* Ticks spent in each phase of the last paint: setup, world pass, chrome. */
long Sprite_Phase[3];
/* Last painted rectangle: width and height, for the harness receipt. */





static short sWalkPhase = 0;
/* Whether the player was in water last tick, so entering it can be heard once
 * rather than every tick. */
static Boolean sWasInWater = false;

/* Hold-to-break: shift-click starts mining the tile under the cursor, the idle
 * loop advances it while the button is still down, and the block comes away when
 * it is finished. A single click used to delete a block outright, which is not
 * what a Minecraft-like should feel like. */
static Boolean sBreaking = false;
static short sBreakX = 0;
static BlockID sBreakId = 0;
static short sBreakY = 0;
static short sBreakZ = 0;
static short sBreakTicks = 0;
#define kBreakTicksNeeded 20
static Boolean sWasOnGround = true;
static short sLastPlayerTileX = -1;
static short sLastPlayerTileY = -1;

/* Last frame's camera, so a pan can scroll the previous image instead of
 * recomposing the whole view. */
static short sLastCamX = 0;
static short sLastCamY = 0;
static Boolean sHaveFrame = false;

static short sCamX = 0;
static short sCamY = 0;

/* Selected swatch index; the placed block id is sBlockType + 1. */
static short sBlockType = 0;

/* Pending repaint: state changes record the smallest affected rect here and
 * GameWindow_Idle turns it into an InvalRect, so painting only ever happens
 * inside a BeginUpdate/EndUpdate pass. */
static Boolean sDirty = false;
/* The status strip is redrawn directly from the idle loop instead of through an
 * invalidation. Every InvalRect lands in the same update region, and the region's
 * bounding box is what the paint uses -- so invalidating the strip together with
 * a selection box over the island made the world repaint from the top of the view
 * down to the pointer, on every move between tiles. */
static Boolean sStripDirty = false;
static Rect sDirtyRect;

/* Intersection of the display rect with the current update region's bounds,
 * so an update pass only walks the tiles it can actually paint. */
static Rect sRenderRect;

/* Private function declarations */
static void MarkDirty(const Rect *r);
static void FlushDirty(void);
static void GameWindow_Update(void);
static void GameWindow_Render(void);
static void SpawnPlayerOnClearGround(void);

static long AbsLong(long v)
{
    return (v < 0) ? -v : v;
}

/* Filled polygons take their colour from the *pen pattern*, so a plain
 * 1-bit pattern can only ever be a shade of grey: PaintPoly with qd.white
 * yields white and RGBForeColor is ignored. MakeRGBPat builds a pattern that
 * renders as an actual colour on a colour display and as a dither on a 1-bit
 * one, which is what makes the faces distinguishable on both. */
/* Whether the display has colour. On a 1-bit screen a face is a pattern; on a
 * colour screen the same pattern's 1-bits take the foreground colour, so set
 * that colour and fill with an all-ones pattern. */
static Boolean sHasColor = false;

/* The damage this paint is for, before it was clamped to the play area. */
static Rect sDamageBox;

static void DetectColor(void)
{
    PixMapHandle pm;

    sHasColor = false;
    pm = GetGDevice() != NULL ? (*GetGDevice())->gdPMap : NULL;
    if (pm != NULL && *pm != NULL && (*pm)->pixelSize > 1) {
        sHasColor = true;
    }
}

/* Top / left / right pattern and colour for one block type. */

/* Create the game window and lay out the play area and the selector strip. */
void GameWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight, windowWidth, windowHeight;
    short displayWidth, displayHeight;

    if (sInitialized) {
        return;
    }

    screenWidth  = (short)(qd.screenBits.bounds.right - qd.screenBits.bounds.left);
    screenHeight = (short)(qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);

    windowWidth  = (short)((screenWidth * 4) / 5);
    windowHeight = (short)(screenHeight - 80);

    SetRect(&windowRect, (short)((screenWidth - windowWidth) / 2), 40,
            (short)((screenWidth - windowWidth) / 2 + windowWidth), (short)(40 + windowHeight));

    sWindow = NewWindow(NULL, &windowRect, "\pMacCraft - Game", true, documentProc,
                        (WindowPtr)-1, true, 0);
    if (sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    sContentRect = sWindow->portRect;

    /* Block selector strip along the bottom. */
    SetRect(&sBlockSelectorRect, kWindowMargin,
            (short)(sContentRect.bottom - kGameBlockSelectorHeight - kWindowMargin),
            (short)(sContentRect.right - kWindowMargin),
            (short)(sContentRect.bottom - kWindowMargin));

    /* Play area above it. */
    SetRect(&sDisplayRect, kGameViewMargin, kGameViewMargin + kGameTitleStripHeight,
            (short)(sContentRect.right - kGameViewMargin),
            (short)(sBlockSelectorRect.top - kGameViewMargin));

    /* Title strip: the band between the frame and the play area. */
    SetRect(&sTitleStripRect, sDisplayRect.left, kGameViewMargin,
            sDisplayRect.right, (short)(sDisplayRect.top - 1));

    /* The HUD works inside these; it never guesses at window geometry. */
    HUD_Layout(&sTitleStripRect, &sBlockSelectorRect, &sDisplayRect);

    /* Centre tile (24,24) in the play area: Iso_Project(24,24,0,0,0) is
     * (0, 384), so the camera offset is the display centre minus that. */
    displayWidth  = (short)(sDisplayRect.right - sDisplayRect.left);
    displayHeight = (short)(sDisplayRect.bottom - sDisplayRect.top);
    sCamX = (short)(displayWidth / 2);
    sCamY = (short)(displayHeight / 2 - 384);

    sBlockType  = 0;
    sDirty      = false;
    sInitialized = true;
    sIsVisible   = false;

    /* Resume the last build if there is one; otherwise the island. */
    /* Count what came back: a save that parses but holds nothing useful (a file
     * written by a broken build, or by a session that started before the scene
     * existed) must not leave the player standing in an empty world. */
    if (Save_Exists() && Save_Read() == noErr) {
        short x, y, solid = 0;

        for (x = 0; x < kWorldX && solid < 200; x++) {
            for (y = 0; y < kWorldY && solid < 200; y++) {
                if (World_TopZ(x, y) >= 0) {
                    solid++;
                }
            }
        }
        if (solid < 200) {
            Scene_BuildIsland();
        }
    }
    else {
        Scene_BuildIsland();
    }

    /* Spawn after the world exists, on ground that is actually clear. */
    SpawnPlayerOnClearGround();
    /* Centre before the first paint: the camera starts at zero, so the first
     * frame would otherwise draw the island off-screen and the partial repaints
     * that follow never cover the world area again. */
    {
        short px = (short)(sPlayer.x >> 16);
        short py = (short)(sPlayer.y >> 16);
        short pz = (short)(sPlayer.z >> 16);

        sCamX = (short)((sDisplayRect.right - sDisplayRect.left) / 2 -
                        (px - py) * (kTileW / 2));
        sCamY = (short)((sDisplayRect.bottom - sDisplayRect.top) / 2 -
                        ((px + py) * (kTileH / 2) - pz * kTileH));
    }
    Sfx_Init();

    /* Take the front. The game is launched from Startup Items, so the Finder
     * comes forward afterwards and the window would sit inactive: its clicks
     * would go to the desktop and its menu bar would belong to the Finder. */
    {
        ProcessSerialNumber psn;

        if (GetCurrentProcess(&psn) == noErr) {
            SetFrontProcess(&psn);
        }
    }
    Sprite_SetView(&sDisplayRect);
    /* The window sits at windowRect; the sprite layer needs that offset because
     * it writes to screen memory rather than through the port. */
    Sprite_SetOrigin((short)windowRect.left, (short)windowRect.top);
    Sprite_Init();
}

/* Dispose of the window and free what it owned. */
void GameWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    Sfx_Dispose();
    Sprite_Dispose();

    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
    sDirty       = false;
}

/* Record a rect that needs repainting. */
static void MarkDirty(const Rect *r)
{
    if (r == NULL) {
        return;
    }

    if (sDirty) {
        UnionRect(r, &sDirtyRect, &sDirtyRect);
    }
    else {
        sDirtyRect = *r;
        sDirty     = true;
    }
}

/* Turn a pending repaint into an InvalRect, so drawing only ever happens
 * inside a BeginUpdate/EndUpdate pass. */
static void FlushDirty(void)
{
    Rect r;

    if (!sDirty || !sInitialized || sWindow == NULL) {
        return;
    }

    r = sDirtyRect;
    sDirty = false;



    {
        GrafPtr saved;

        GetPort(&saved);
        SetPort(sWindow);
        InvalRect(&r);
        SetPort(saved);
    }
}



/* Grain patterns: the second tone of each face. On a colour display the base
 * is a solid colour and this pattern is filled in a darker shade of it; on a
 * 1-bit display the base is a pattern and this one is filled in black, so a
 * face still reads as textured on a Macintosh Plus. */



/* The grain is the same hue, one ramp step down: enough to read as material,
 * not enough to compete with the base colour. */

/* Fill one face. FillPoly takes an explicit pattern: on a colour display the
 * pattern's 1-bits take the foreground colour, so an all-ones pattern plus
 * RGBForeColor is a solid colour; on a 1-bit display the pattern itself is the
 * fill. Painting the polygon any other way filled with the pen colour and
 * produced the black play area. */
/* Apply a light factor to a palette colour. The two colour structs have the
 * same field layout on purpose, and this is the only place that relies on it. */

/* Build the quad as a fresh PolyHandle. FillPoly cannot be called twice on one
 * handle here: the second call filled the whole polygon with garbage, which is
 * why every face went black when the grain overlay was added. One polygon per
 * fill is cheap and always correct. */


/* Parse a small integer out of a command line, advancing the cursor. */


static void InvalidateColumn(short x, short y, short z);

/* Take the block away and tell the world it changed. */
static void FinishBreak(void)
{
    if (!sBreaking) {
        return;
    }
    if (World_Set(sBreakX, sBreakY, sBreakZ, 0)) {
        Boolean hard = (Boolean)(sBreakId == kBlockStone ||
                                 sBreakId == kBlockCobblestone ||
                                 sBreakId == kBlockBrick ||
                                 sBreakId == kBlockBedrock);

        Sfx_Play(hard ? kSfxBreakStone : kSfxBreak);
        Hover_Reset();
        Particles_Spawn(sBreakX, sBreakY, sBreakZ, sBreakId);
    }
    sBreaking = false;
    sBreakTicks = 0;
    InvalidateColumn(sBreakX, sBreakY, sBreakZ);
}

/* Harness hook: mine a block to completion through the same path the mouse
 * uses. The keyboard and mouse cannot be driven from the host, so this is how
 * hold-to-break gets proven. */
void GameWindow_MineForTest(short x, short y, short z, short ticks)
{
    if (World_TopZ(x, y) < 0) {
        return;
    }
    sBreaking = true;
    sBreakX = x;
    sBreakY = y;
    sBreakZ = World_TopZ(x, y);
    sBreakTicks = (short)(ticks > 0 ? ticks : 0);
    if (sBreakTicks >= kBreakTicksNeeded) {
        FinishBreak();
    }
    FlushDirty();
}

/* The crack overlay: dark pixels over the tile being mined, more of them the
 * longer the button is held. Drawn as small fills because the sprite layer has no
 * alpha; at this size a sparse scatter reads as damage. */
static void DrawCrackOverlay(short x, short y, short z, short ticks)
{
    short sx, sy;
    short step = (short)((ticks * 8) / kBreakTicksNeeded);
    short i;
    static const signed char kSpots[8][2] = {
        {-4, -2}, {3, -4}, {-8, 2}, {7, 3}, {-2, 6}, {5, -1}, {-6, -5}, {1, 2}
    };

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx = (short)(sx + sDisplayRect.left);
    sy = (short)(sy + sDisplayRect.top);

    if (step > 8) {
        step = 8;
    }
    for (i = 0; i < step; i++) {
        Rect r;

        SetRect(&r, (short)(sx + kSpots[i][0]), (short)(sy + kSpots[i][1]),
                (short)(sx + kSpots[i][0] + 3), (short)(sy + kSpots[i][1] + 2));
        Sprite_FillRect(&r, 0x101010UL);
    }
}

/* The rectangle the held block occupies, so the swing can repaint just that
 * instead of the whole display. */
static void HeldBlockRect(Rect *out)
{
    SetRect(out, (short)(sDisplayRect.right - 130), (short)(sDisplayRect.bottom - 100),
            (short)(sDisplayRect.right - 10), (short)(sDisplayRect.bottom - 8));
}

/* The rectangle the player's figure occupies at its current position. */
static void PlayerFigureRect(Rect *out)
{
    short px = (short)(sPlayer.x >> 16);
    short py = (short)(sPlayer.y >> 16);
    short pz = (short)(sPlayer.z >> 16);
    short sx, sy;

    Iso_Project(px, py, pz, sCamX, sCamY, &sx, &sy);
    sx = (short)(sx + sDisplayRect.left);
    sy = (short)(sy + sDisplayRect.top);

    SetRect(out, (short)(sx - 10), (short)(sy - 32), (short)(sx + 10), (short)(sy + 6));
}

/* Keep the player in the middle of the play area. The camera offsets are what
 * Iso_Project adds, so the inverse is a subtract: project the player at camera
 * zero and move the camera to cancel it. */
static void CentreOnPlayer(void)
{
    short px = (short)(sPlayer.x >> 16);
    short py = (short)(sPlayer.y >> 16);
    short pz = (short)(sPlayer.z >> 16);
    short sx = 0;
    short sy = 0;
    short marginX = 64;
    short marginY = 48;

    Iso_Project(px, py, pz, sCamX, sCamY, &sx, &sy);
    sx = (short)(sx + sDisplayRect.left);
    sy = (short)(sy + sDisplayRect.top);

    /* The camera does not track the player. Tracking meant a pan every step, and
     * a pan cannot be cheaper than moving every pixel of the screen: 61 ticks a
     * frame in the emulator, a second per step, which is what made walking
     * unusable. The view holds most of the island, so it stays put and only
     * re-centres when the player is about to walk out of it -- one full frame
     * every few dozen steps instead of one per step. */
    if (sx > (short)(sDisplayRect.left + marginX) &&
        sx < (short)(sDisplayRect.right - marginX) &&
        sy > (short)(sDisplayRect.top + marginY) &&
        sy < (short)(sDisplayRect.bottom - marginY)) {
        return;
    }

    {
        short targetX = (short)((sDisplayRect.right - sDisplayRect.left) / 2);
        short targetY = (short)((sDisplayRect.bottom - sDisplayRect.top) / 2 + kTileH);

        /* One axis at a time. A diagonal pan uncovers an L, and one rectangle
         * cannot describe an L -- the paint would have to recompose the whole
         * view. Moving x on one tick and y on the next keeps every pan a single
         * band, and the scroll path already repaints a band cheaply. */
        {
            short wantX = (short)(targetX - (short)((px - py) * (kTileW / 2)));
            short wantY = (short)(targetY - (short)((px + py) * (kTileH / 2) - pz * kTileH));
            long stepX = (long)wantX - (long)sCamX;
            long stepY = (long)wantY - (long)sCamY;

            if (stepX != 0 && stepY != 0) {
                if (AbsLong(stepX) >= AbsLong(stepY)) {
                    stepY = 0;
                }
                else {
                    stepX = 0;
                }
            }
            /* Keep the step inside the view: the pan path can only scroll by
             * less than a display, and a bigger step would recompose the whole
             * play area. A long jump (a respawn, a teleport) then converges over
             * a couple of frames as bands instead of one full repaint. */
            {
                short halfW = (short)((sDisplayRect.right - sDisplayRect.left) / 2);
                short halfH = (short)((sDisplayRect.bottom - sDisplayRect.top) / 2);

                if (stepX > halfW) stepX = halfW;
                if (stepX < -halfW) stepX = -halfW;
                if (stepY > halfH) stepY = halfH;
                if (stepY < -halfH) stepY = -halfH;
            }
            sCamX = (short)(sCamX + stepX);
            sCamY = (short)(sCamY + stepY);
        }
        MarkDirty(&sDisplayRect);
    }
}

/* A player figure, two blocks tall: shadow on the ground, legs, torso, head.
 * Drawn with the HUD's pixel fills because it has to sit in front of the world
 * and behind the HUD, and because a block-textured avatar at this scale would
 * be one bright pixel. */


/* The cloud slabs, drawn through the pixel path: QuickDraw fills inside the
 * play area do not land on this port, and the sky is painted by the sprite
 * layer for exactly that reason. Positions come from sky.c, which owns the
 * layout, so the mono path and this one agree on where clouds are. */
/* Vertical sky gradient: deep blue overhead, pale at the horizon.
 *
 * A flat fill is what the first version did, and on a 1024x768 screen that is a
 * lot of flat colour. Bands cost nothing extra -- the same pixels are written,
 * just in strips -- and give the view a horizon. */
#define kSkyBands 16
#define kSkyTopColour 0x4A86E8UL
#define kSkyHorizonColour 0xC4DCF5UL

static unsigned long SkyBandColour(short band)
{
    long t = (long)band * 255L / (kSkyBands - 1);
    long r = ((long)((kSkyTopColour >> 16) & 0xFF) * (255 - t) +
              (long)((kSkyHorizonColour >> 16) & 0xFF) * t) / 255;
    long g = ((long)((kSkyTopColour >> 8) & 0xFF) * (255 - t) +
              (long)((kSkyHorizonColour >> 8) & 0xFF) * t) / 255;
    long b = ((long)(kSkyTopColour & 0xFF) * (255 - t) +
              (long)(kSkyHorizonColour & 0xFF) * t) / 255;

    return ((unsigned long)r << 16) | ((unsigned long)g << 8) | (unsigned long)b;
}

static void DrawClouds(void)
{
    short rectCount = Sky_CloudRectCount();
    short shift = Sky_CloudShift((long)TickCount());
    short i;
    short cx, cy, cw, ch, uy;
    long x;

    for (x = (long)sRenderRect.left - Sky_CloudPeriod(); x < (long)sRenderRect.right;
         x += Sky_CloudPeriod()) {
        for (i = 0; i < rectCount; i++) {
            Rect slab;

            Sky_CloudRect(i, &cx, &cy, &cw, &ch, &uy);
            slab.left = (short)(x + cx + shift);
            slab.right = (short)(slab.left + cw);
            slab.top = (short)(sRenderRect.top + uy);
            slab.bottom = (short)(slab.top + 1);

            if (slab.right <= sRenderRect.left || slab.left >= sRenderRect.right) {
                continue;
            }
            Sprite_FillRect(&slab, 0xC8C8D8UL); /* shaded underside */

            slab.top = (short)(sRenderRect.top + cy);
            slab.bottom = (short)(slab.top + ch);
            Sprite_FillRect(&slab, 0xFFFFFFUL);
        }
    }
}

/* Minecraft's crosshair: a small white plus with a dark backing so it reads on
 * both sky and grass. Drawn with QuickDraw after the world pass. */

/* The block in your hand: the selected kind, magnified twice, in the corner of
 * the play area. Drawn with the same sprite pipeline as the world, at scale 2,
 * which is the one place a block is shown larger than the world draws it. */

/* Bounding box of the selection box for a tile, with a pixel of slack for the
 * outline. Used to invalidate just the old and new hover cells when the mouse
 * moves. */
/* The screen box of a tile, for the crack overlay and for damage. */
static void TileBox(short x, short y, short z, Rect *out)
{
    short sx, sy;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx = (short)(sx + sDisplayRect.left);
    sy = (short)(sy + sDisplayRect.top);
    SetRect(out, (short)(sx - kTileW / 2 - 2), (short)(sy - kTileH / 2 - 2),
            (short)(sx + kTileW / 2 + 2), (short)(sy + kTileH / 2 + kTileH + 2));
}

/* The pick for a click: the same call the hover uses, plus which face of the
 * block the point is on, so a wall can be built against. */
static Boolean PickSurfaceAt(Point localPt, short *outX, short *outY, short *outZ)
{
    if (!PtInRect(localPt, &sDisplayRect)) {
        return false;
    }
    return Pick_Tile((short)(localPt.h - sDisplayRect.left),
                     (short)(localPt.v - sDisplayRect.top),
                     sCamX, sCamY, outX, outY, outZ);
}

static Boolean PickSurfaceAtFace(Point localPt, short *outX, short *outY, short *outZ,
                                 short *outFace)
{
    if (!PtInRect(localPt, &sDisplayRect)) {
        return false;
    }
    return Pick_TileFace((short)(localPt.h - sDisplayRect.left),
                         (short)(localPt.v - sDisplayRect.top),
                         sCamX, sCamY, outX, outY, outZ, outFace);
}

Boolean GameWindow_SelectBlockType(short index)
{
    if (index < 0 || index >= kGameNumBlockTypes) {
        return false;
    }
    sBlockType = index;
    Sfx_Play(kSfxClick);
    MarkDirty(&sBlockSelectorRect);
    sStripDirty = true;
    return true;
}

Boolean GameWindow_PickAt(short localX, short localY, short *outX, short *outY,
                          short *outZ, short *outFace)
{
    Point pt;

    pt.h = localX;
    pt.v = localY;
    return PickSurfaceAtFace(pt, outX, outY, outZ, outFace);
}

void GameWindow_CursorLocal(Point *out)
{
    GrafPtr saved;

    if (out == NULL || sWindow == NULL) {
        return;
    }
    GetPort(&saved);
    SetPort(sWindow);
    GetMouse(out);
    SetPort(saved);
}

void GameWindow_TileScreen(short x, short y, short z, short *sx, short *sy)
{
    short px = 0;
    short py = 0;

    Iso_Project(x, y, z, sCamX, sCamY, &px, &py);
    if (sx != NULL) {
        *sx = (short)(px + sDisplayRect.left);
    }
    if (sy != NULL) {
        *sy = (short)(py + sDisplayRect.top);
    }
}

/* Resolve the pointer through the hover module and damage what changed. */
static void UpdateHover(void)
{
    Point mouse;
    Rect dirty;

    /* GetMouse reports the pointer in the *current port's* coordinates, not
     * globally. Reading it with the window port current therefore already gives
     * window coordinates -- and the GlobalToLocal that used to follow subtracted
     * the window origin a second time, which is why the hover ring sat a whole
     * window width away from the pointer. */
    {
        GrafPtr saved;

        GetPort(&saved);
        SetPort(sWindow);
        GetMouse(&mouse);
        SetPort(saved);
    }

    if (Hover_Update(mouse, &sDisplayRect, sCamX, sCamY, &dirty)) {
        /* Only the ring's old box: the tiles under the new one are not damaged,
         * because the ring itself is drawn straight to the screen. */
        if (dirty.right > dirty.left && dirty.bottom > dirty.top) {
            MarkDirty(&dirty);
        }
        sStripDirty = true;
    }
}

/* The crosshair's box, for damage tests: an eleven-pixel cross at the centre of
 * the play area. */
static void CrosshairRect(Rect *out)
{
    short cx = (short)((sDisplayRect.left + sDisplayRect.right) / 2);
    short cy = (short)((sDisplayRect.top + sDisplayRect.bottom) / 2);

    SetRect(out, (short)(cx - 6), (short)(cy - 6), (short)(cx + 6), (short)(cy + 6));
}

/* True when any column under the rect holds water. Water sprites are drawn with
 * see-through pixels, so a repaint that skips the sky fill leaves the previous
 * frame showing through.
 *
 * The tile span comes from both corners of the rect, padded by one tile: the
 * old version unprojected one corner and scanned a fixed +/-kWorldZ window,
 * which is 441 columns by 8 levels -- 3,528 world reads before drawing anything,
 * for a repaint that covers about a dozen columns. */
static Boolean RectHasWater(const Rect *r)
{
    short ax, ay, bx, by, x, y;
    short xMin, xMax, yMin, yMax;

    if (r == NULL || r->right <= r->left || r->bottom <= r->top) {
        return false;
    }

    Iso_UnprojectClamped((short)(r->left - sDisplayRect.left),
                         (short)(r->top - sDisplayRect.top),
                         sCamX, sCamY, &ax, &ay);
    Iso_UnprojectClamped((short)(r->right - sDisplayRect.left),
                         (short)(r->bottom - sDisplayRect.top),
                         sCamX, sCamY, &bx, &by);

    xMin = (short)((ax < bx ? ax : bx) - 1);
    xMax = (short)((ax > bx ? ax : bx) + 1);
    yMin = (short)((ay < by ? ay : by) - 1);
    yMax = (short)((ay > by ? ay : by) + 1);

    /* A rect spanning the whole view still has to see every column, so clamp
     * to the world rather than trusting the corners. */
    if (xMin < 0) {
        xMin = 0;
    }
    if (yMin < 0) {
        yMin = 0;
    }
    if (xMax >= kWorldX) {
        xMax = (short)(kWorldX - 1);
    }
    if (yMax >= kWorldY) {
        yMax = (short)(kWorldY - 1);
    }

    for (y = yMin; y <= yMax; y++) {
        for (x = xMin; x <= xMax; x++) {
            short z;

            for (z = 0; z < kWorldZ; z++) {
                if (World_Get(x, y, z) == kBlockWater) {
                    return true;
                }
            }
        }
    }

    return false;
}






/* Draw one block: top diamond plus its two visible side faces, all centred on
 * Iso_Project(x, y, z, ...) and clipped nowhere (the update region clips). */
/* The darkest of a face's four corner occlusions: one boxed corner is enough to
 * drop the face a shade, the way Minecraft's smooth lighting reads. */


/* Inverse of Iso_Project's documented z=0 mapping, clamped to the world.
 * Used only to derive the visible tile range; clicks use Iso_Unproject. */


/* Visible tile range for the part of the display being repainted, in
 * display-local coordinates. Expanded by kWorldZ tiles so that raised columns
 * whose tops are in view but whose base tiles are below still render. */

/* Draw the world, bottom z level first, back-to-front inside each level. */

/* Compute the rect of one swatch in the selector strip. */
static void SelectorBlockRect(short index, Rect *outRect)
{
    Rect frameRect;
    short totalWidth, startX, startY;
    short blockSize = kGameBlockSwatchSize;
    short spacing   = kGameBlockSwatchSpacing;

    frameRect = sBlockSelectorRect;
    InsetRect(&frameRect, 1, 1);

    totalWidth = (blockSize + spacing) * kGameNumBlockTypes - spacing;
    startX     = frameRect.left + (frameRect.right - frameRect.left - totalWidth) / 2;
    startY     = frameRect.top + (frameRect.bottom - frameRect.top - blockSize) / 2;

    SetRect(outRect, startX + index * (blockSize + spacing), startY,
            startX + index * (blockSize + spacing) + blockSize, startY + blockSize);
}

/* Draw the block selector UI at the bottom of the window. */

/* Invalidate only the screen rect covered by one column at height z. */
static void InvalidateColumn(short x, short y, short z)
{
    short sx, sy;
    Rect r;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx += sDisplayRect.left;
    sy += sDisplayRect.top;


    SetRect(&r, sx - kTileW / 2 - 2, sy - kTileH / 2 - 2, sx + kTileW / 2 + 2, sy + kTileH + 2);
    MarkDirty(&r);
}

/* True when a block at this cell would be inside the player's box. Placing into
 * your own cell used to embed the player in geometry: every sub-step then hit the
 * block and the collision snap teleported it sideways by most of a block. */
static Boolean BlockFillsPlayer(short x, short y, short z)
{
    long loX = sPlayer.x - kPlayerHalfWidth;
    long hiX = sPlayer.x + kPlayerHalfWidth;
    long loY = sPlayer.y - kPlayerHalfWidth;
    long hiY = sPlayer.y + kPlayerHalfWidth;
    long loZ = sPlayer.z;
    long hiZ = sPlayer.z + kPlayerHeight;

    return (Boolean)((long)x << 16 < hiX && (long)(x + 1) << 16 > loX &&
                     (long)y << 16 < hiY && (long)(y + 1) << 16 > loY &&
                     (long)z << 16 < hiZ && (long)(z + 1) << 16 > loZ);
}

/* Put the player on the nearest clear column to the centre.
 *
 * The island's centre is the hut, whose roof is the topmost block the world can
 * hold (z = 7): spawning there put the player at z = 8, outside the world, so it
 * fell for ever. Both the initial spawn and Reset World go through here, so the
 * two cannot disagree again. */
static void SpawnPlayerOnClearGround(void)
{
    /* The village square is flat and open; start the search there and only
     * wander if something has been built on it. */
    short sx = 40;
    short sy = 46;
    short keep;

    /* Walk outwards from the chosen spot until a column has ground and no
     * structure above the plateau; 8 is comfortably clear of the hut. */
    for (keep = 0; keep < 16; keep++) {
        short tz = World_TopZ((short)(sx + keep), sy);

        if (tz >= 0 && tz < 7) {
            sx = (short)(sx + keep);
            break;
        }
    }
    Player_Spawn(&sPlayer, sx, sy, (short)(World_TopZ(sx, sy) + 1));
}

/* Handle mouse clicks in the content area of the game window. */
static Boolean GameWindow_HandleContentClick(Point localPt, Boolean shiftDown)
{
    short x, y, topZ, newZ;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Block placement/removal in the game display. The tile comes from the same
     * surface pick the selection box uses, so a click always acts on the block
     * that is highlighted. */
    if (PtInRect(localPt, &sDisplayRect)) {
        short face = 0;
        short placeX = 0, placeY = 0, placeZ = 0;

        if (!PickSurfaceAtFace(localPt, &x, &y, &newZ, &face)) {
            return true;
        }

        topZ = World_TopZ(x, y);

        /* Where a click would put a block. The top face stacks on the column;
         * a side face puts the block beside the one it belongs to, at the same
         * level, which is how you build into a wall instead of piling up. */
        if (face == kPickFaceLeft) {
            /* The left face points at +y, which is the neighbour that hides it. */
            placeX = x;
            placeY = (short)(y + 1);
            placeZ = (short)(newZ - 1);
        }
        else if (face == kPickFaceRight) {
            placeX = (short)(x + 1);
            placeY = y;
            placeZ = (short)(newZ - 1);
        }
        else {
            placeX = x;
            placeY = y;
            placeZ = (short)(topZ + 1);
        }

        if (shiftDown) {
            /* Start mining: the block comes away after the button has been held
             * long enough, see the idle loop. */
            if (topZ >= 0) {
                sBreaking = true;
                sBreakX = x;
                sBreakY = y;
                sBreakZ = topZ;
                sBreakId = World_Get(sBreakX, sBreakY, sBreakZ);
                sBreakTicks = 0;
            }
        }
        else if (placeX >= 0 && placeX < kWorldX &&
                 placeY >= 0 && placeY < kWorldY &&
                 placeZ >= 0 && placeZ < kWorldZ &&
                 World_Get(placeX, placeY, placeZ) == 0 &&
                 !BlockFillsPlayer(placeX, placeY, placeZ) &&
                 World_Set(placeX, placeY, placeZ, (BlockID)(sBlockType + 1))) {
            Sfx_Play(kSfxPlace);
            /* A small burst of the block's own colour: the placement reads even
             * where the new block is behind something. */
            Particles_Spawn(placeX, placeY, placeZ, (BlockID)(sBlockType + 1));
            Hover_Reset();
            sSwingUntil = (unsigned long)TickCount() + 8;
            InvalidateColumn(placeX, placeY, placeZ);
        }

        return true;
    }

    /* Block type selection */
    if (PtInRect(localPt, &sBlockSelectorRect)) {
        short i;
        Rect blockRect;

        for (i = 0; i < kGameNumBlockTypes; i++) {
            HUD_SlotRect(i, kGameNumBlockTypes, &blockRect);
            if (PtInRect(localPt, &blockRect)) {
                Sfx_Play(kSfxClick);
                sBlockType = i;
                MarkDirty(&sBlockSelectorRect);
                return true;
            }
        }

        return true;
    }

    return false;
}

/* Pan the camera by one tile step and repaint the display. */
static void PanCamera(short dx, short dy)
{
    int newX = (int)sCamX + (int)dx;
    int newY = (int)sCamY + (int)dy;

    if (newX < -kGameCameraLimit) newX = -kGameCameraLimit;
    if (newX > kGameCameraLimit) newX = kGameCameraLimit;
    if (newY < -kGameCameraLimit) newY = -kGameCameraLimit;
    if (newY > kGameCameraLimit) newY = kGameCameraLimit;

    sCamX = (short)newX;
    sCamY = (short)newY;

    MarkDirty(&sDisplayRect);
}

/*
 * Cursor-key character codes. Apple's current headers no longer name them
 * (they lived in the old Events.h); 0x1C..0x1F is what the key map reports
 * for left/right/up/down through charCodeMask, on every keyboard layout.
 */
enum {
    kLeftArrowKey  = 0x1C,
    kRightArrowKey = 0x1D,
    kUpArrowKey    = 0x1E,
    kDownArrowKey  = 0x1F
};

/* Handle key events in the game window (menu keys are handled by MenuKey). */
static Boolean GameWindow_HandleKeyDown(char key)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    switch (key) {
    /* 1-9 pick the first nine hotbar slots, so the keyboard reaches the whole
     * palette the strip shows instead of only the first three kinds. */
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
        sBlockType = (short)(key - '1');
        if (sBlockType >= kGameNumBlockTypes) {
            sBlockType = (short)(kGameNumBlockTypes - 1);
        }
        Sfx_Play(kSfxClick);
        MarkDirty(&sBlockSelectorRect);
        return true;

    case 't':
    case 'T': /* Cycle block type */
        sBlockType = (short)((sBlockType + 1) % kGameNumBlockTypes);
        MarkDirty(&sBlockSelectorRect);
        return true;

    case kLeftArrowKey:
    case 'a':
    case 'A':
        sIntentX = -1;
        sIntentUntil = (unsigned long)TickCount() + kIntentHoldTicks;
        return true;

    case kRightArrowKey:
    case 'd':
    case 'D':
        sIntentX = 1;
        sIntentUntil = (unsigned long)TickCount() + kIntentHoldTicks;
        return true;

    case kUpArrowKey:
    case 'w':
    case 'W':
        sIntentY = -1;
        sIntentUntil = (unsigned long)TickCount() + kIntentHoldTicks;
        return true;

    case kDownArrowKey:
    case 's':
    case 'S':
        sIntentY = 1;
        sIntentUntil = (unsigned long)TickCount() + kIntentHoldTicks;
        return true;

    case ' ':
        sJumpHeld = true;
        sIntentUntil = (unsigned long)TickCount() + kIntentHoldTicks;
        return true;

    default:
        break;
    }

    return false;
}

/* Handle window activation/deactivation events */
static void GameWindow_HandleActivate(Boolean becomingActive)
{
    (void)becomingActive;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    /* Force window redraw */
    MarkDirty(&sWindow->portRect);
}

/* Append C string s to a Pascal string being built in dst, whose length byte
 * lives at dst[0] and whose current length is *len. No sprintf on a 68000: the
 * whole strip is assembled by hand. */

/* "MacCraft" on the left, block and camera readout on the right. */

/* Render the game window contents. Only called between BeginUpdate/EndUpdate. */

static void GameWindow_Render(void)
{
    Rect strip;
    PenState savedPen;
    Boolean fullPaint = false;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    GetPenState(&savedPen);
    {
        /* Paint timing for the harness: Sprite_Work[2] accumulates ticks spent
         * painting and [3] counts paints, so average frame cost is one divide. */
        static unsigned long sPaintStart = 0;

        sPaintStart = (unsigned long)TickCount();
        sPaintStart = sPaintStart; /* start */
        Sprite_Work[4] = (long)sPaintStart;
    }

    /* The window can be dragged, so the offset used for direct screen writes is
     * recomputed here rather than captured once at creation. */
    {
        Point origin;

        origin.h = 0;
        origin.v = 0;
        LocalToGlobal(&origin);
        Sprite_SetOrigin(origin.h, origin.v);
    }

    /* World drawing is limited to whole tiles, so it covers the display rect;
     * BeginUpdate has already clipped the port to the update region, which is
     * what keeps a one-column invalidation from repainting the rest. */
    /* Repaint only what the update region asks for: QuickDraw fills are clipped
     * by the port, and the world pass trims its direct pixel writes to this
     * rect, so a small invalidation -- the selection box moving under the mouse
     * -- costs a few hundred pixels instead of a full frame. */
    if (!sHaveFrame) {
        /* First paint after launch: repaint everything. The update region at that
         * moment can be smaller than the window (the first invalidate is often
         * just the strip), and the sky and world writes are scoped to this rect,
         * so taking it at face value left most of the window white. */
        sRenderRect = sDisplayRect;
    }
    else {
        /* The update region lives in the window's visRgn; GetClip returns the
         * port's clip, which is the whole port and says nothing about what this
         * paint is for. Reading the clip made every paint look like a full
         * window, so every pointer move repainted the world from the sky. */
        if (sWindow->visRgn != NULL) {
            Rect box = (*sWindow->visRgn)->rgnBBox;

            /* The chrome lives outside the play area, so the tests below need the
             * damage before it was clamped to the display: a repaint aimed at the
             * hotbar has an empty play-area rect by construction. */
            sDamageBox = box;
            if (!SectRect(&box, &sDisplayRect, &sRenderRect)) {
                SetRect(&sRenderRect, 0, 0, 0, 0);
            }
        }
        else {
            sRenderRect = sDisplayRect;
        }
    }

    /* Only a full repaint clears anything. A partial one -- the pointer moving
     * between tiles is the common case -- must draw over what is already there:
     * whitening the frame and filling sky first is what produced the pale square
     * around the selection box while the blocks were being drawn, and it left a
     * white square behind whenever the world pass drew no tiles for that rect. */
    fullPaint = (Boolean)(sRenderRect.left == sDisplayRect.left &&
                          sRenderRect.right == sDisplayRect.right &&
                          sRenderRect.top == sDisplayRect.top &&
                          sRenderRect.bottom == sDisplayRect.bottom);

    Sprite_Phase[0] = (long)((unsigned long)TickCount() - (unsigned long)Sprite_Work[4]);
    Sprite_Phase[1] = 0;
    Sprite_Phase[2] = 0;

    /* The play area is never whitened before it is drawn: the world pass covers
     * every pixel of it, and erasing first is exactly the white box that showed
     * up whenever a region was dirty. Only the margins outside the play area are
     * erased, below. */

    /* Play area background. Not black: with a colour port QuickDraw fills from
     * the pen pattern, so a dark background plus dark fills made the entire
     * view a black rectangle. White pattern + RGB background colour gives a
     * solid tint on colour displays and white on 1-bit ones. */
    DetectColor();
    /* A camera pan with nothing else dirty is the common case while walking.
     * Rather than recompose every visible face, shift the frame that is already
     * on screen and redraw only the strip that the shift exposes. */
    if (sHaveFrame && Sprite_Ready() &&
        sRenderRect.left == sDisplayRect.left && sRenderRect.right == sDisplayRect.right &&
        sRenderRect.top == sDisplayRect.top && sRenderRect.bottom == sDisplayRect.bottom &&
        (sCamX != sLastCamX || sCamY != sLastCamY)) {
        short dx = (short)(sCamX - sLastCamX);
        short dy = (short)(sCamY - sLastCamY);
        short w = (short)(sDisplayRect.right - sDisplayRect.left);
        short h = (short)(sDisplayRect.bottom - sDisplayRect.top);

        /* Any pan that stays inside the view may scroll. The old limit of two
         * tiles sent every real pan -- the camera leaves the dead zone a fifth of
         * the view away -- down the full-recompose path at 61 ticks a frame,
         * which is the second-per-step the game felt like. */
        if (dx > -w && dx < w && dy > -h && dy < h) {
            Rect exposed;

            Sprite_BeginFrame();
            Sprite_ScrollRect(&sDisplayRect, dx, dy);
            Sprite_EndFrame();

            /* The exposed area is the band the shift uncovered. With one axis
             * moving it is a single strip; with both it is an L, and one
             * rectangle cannot describe it -- the old code repainted only the
             * corner, which left a band of the previous frame on screen. The
             * camera derives both axes from grid coordinates, so most pans are
             * diagonal: those take the whole play area and are correct. */
            if (dx != 0 && dy != 0) {
                sRenderRect = sDisplayRect;
            }
            else {
                exposed = sDisplayRect;
                if (dx > 0) {
                    exposed.right = (short)(exposed.left + dx + kTileW);
                }
                else if (dx < 0) {
                    exposed.left = (short)(exposed.right + dx - kTileW);
                }
                else if (dy > 0) {
                    exposed.bottom = (short)(sDisplayRect.top + dy + kTileH * 2);
                }
                else {
                    exposed.top = (short)(sDisplayRect.bottom + dy - kTileH * 2);
                }
                sRenderRect = exposed;
            }
        }
    }

    Sprite_PaintedPixels +=
        (long)(sRenderRect.right - sRenderRect.left) *
        (long)(sRenderRect.bottom - sRenderRect.top);

    if (sHasColor && Sprite_Ready()) {
        /* The sky is rewritten when the view changed, and whenever the repainted
         * rect could contain water: water is drawn with see-through pixels, so
         * painting it over old content leaves whatever was there -- the pale
         * specks around a hovered water tile. */
        if (fullPaint || sCamX != sLastCamX || sCamY != sLastCamY ||
            RectHasWater(&sRenderRect)) {
            /* Minecraft's sky: one flat blue with flat white clouds, painted
             * through the pixel path so the colours are exactly what is asked
             * for. Scoped to the repaint rect: these writes bypass the update
             * clip, so filling the whole display here would erase the island on
             * every partial invalidation (a block placement, a hover move). */
            Sprite_BeginFrame();
            Sprite_SetView(&sRenderRect);
            {
                short height = (short)(sDisplayRect.bottom - sDisplayRect.top);
                short band;

                for (band = 0; band < kSkyBands; band++) {
                    Rect strip;
                    short top = (short)(sDisplayRect.top +
                                        (short)((long)height * band / kSkyBands));
                    short bottom = (short)(sDisplayRect.top +
                                           (short)((long)height * (band + 1) / kSkyBands));

                    strip = sRenderRect;
                    if (strip.top < top) {
                        strip.top = top;
                    }
                    if (strip.bottom > bottom) {
                        strip.bottom = bottom;
                    }
                    if (strip.bottom <= strip.top || strip.right <= strip.left) {
                        continue;
                    }
                    Sprite_FillRect(&strip, SkyBandColour(band));
                }
            }
            DrawClouds();
            Sprite_SetView(&sDisplayRect);
            Sprite_EndFrame();
        }
    }
    else if (fullPaint) {
        /* One-bit screen: Sky_Draw's dither ramp is the better picture. */
        Sky_Draw(sWindow, sDisplayRect, sHasColor, (long)TickCount());
    }

    /* World blocks (nothing to do when only the selector strip was dirty) */

    if (sRenderRect.right > sRenderRect.left && sRenderRect.bottom > sRenderRect.top) {
        /* The screen stays locked for the whole pass: the HUD draws through the
         * same pixel path as the world, so releasing it before the HUD left the
         * held block with nothing to write into. */
        Sprite_BeginFrame();
        Sprite_SetView(&sRenderRect);
        {
            WorldView view;

            view.display = sDisplayRect;
            view.camX = sCamX;
            view.camY = sCamY;
            view.hasColor = sHasColor;
            WorldView_Draw(&view, &sRenderRect);
        }
        Sprite_SetView(&sDisplayRect);

        if (sBreaking && sBreakTicks > 0) {
            DrawCrackOverlay(sBreakX, sBreakY, sBreakZ, sBreakTicks);
        }
        Particles_Draw(&sDisplayRect, sCamX, sCamY);
        /* Each overlay is drawn only when the damage can touch it. They were
         * unconditional, so a hover repaint (about 3,450 pixels) also redrew the
         * figure, the crosshair and the held block -- the last of those alone is
         * ~3,000 pixels at 2x magnification -- into pixels that were already
         * correct. */
        {
            Rect overlay;
            Rect touched;

            PlayerFigureRect(&overlay);
            if (SectRect(&overlay, &sRenderRect, &touched)) {
                Avatar_Draw((short)(sPlayer.x >> 16), (short)(sPlayer.y >> 16),
                            (short)(sPlayer.z >> 16), sCamX, sCamY, &sDisplayRect,
                            sWalkPhase, (Boolean)!sPlayer.onGround);
            }
            CrosshairRect(&overlay);
            if (SectRect(&overlay, &sRenderRect, &touched)) {
                HUD_DrawCrosshair();
            }
            HeldBlockRect(&overlay);
            if (SectRect(&overlay, &sRenderRect, &touched)) {
                HUD_DrawHeld((BlockID)(sBlockType + 1), Swing());
            }
        }

        /* The ring is drawn through the pixel path, so it is not clipped by the
         * update region: it lands even when the repaint only covers the box it
         * is leaving. */
        Hover_Draw(&sDisplayRect, sCamX, sCamY);
        Sprite_EndFrame();
    }
    else {
        /* A paint that only touches the strip still has to keep the HUD, which
         * lives inside the play area: the world pixels are written directly and
         * survive, but QuickDraw's clip can suppress these, so they are drawn
         * whenever the play area is anywhere in the clip. */
        Sprite_BeginFrame();
        Sprite_SetView(&sDisplayRect);
        Hover_Draw(&sDisplayRect, sCamX, sCamY);
        Sprite_EndFrame();
    }

    if (fullPaint) {
        /* Erase the margins so a partially visible edge tile cannot bleed outside
         * the display rect (no manual clipping anywhere). Only on a full repaint;
         * on a partial one the margins were not damaged. */
        BackColor(whiteColor);
        BackPat(&qd.white);

        /* Top strip */
        strip = sWindow->portRect;
        strip.bottom = sDisplayRect.top;
        EraseRect(&strip);

        /* Left strip */
        strip = sWindow->portRect;
        strip.top    = sDisplayRect.top;
        strip.bottom = sDisplayRect.bottom;
        strip.right  = sDisplayRect.left;
        EraseRect(&strip);

        /* Right strip */
        strip = sWindow->portRect;
        strip.top    = sDisplayRect.top;
        strip.bottom = sDisplayRect.bottom;
        strip.left   = sDisplayRect.right;
        EraseRect(&strip);

        /* Bottom strip: the gap between the display and the selector (the
         * selector repaints its own area afterwards). */
        strip = sWindow->portRect;
        strip.top = sDisplayRect.bottom;
        EraseRect(&strip);

        /* Display frame, then the chrome. The HUD draws through the pixel path, so
         * it needs the screen locked and its view narrowed to the rect it owns. */
        PenNormal();
        PenSize(1, 1);
        ForeColor(blackColor);
        FrameRect(&sDisplayRect);
    }

    Sprite_Phase[1] = (long)((unsigned long)TickCount() - (unsigned long)Sprite_Work[4]) - Sprite_Phase[0];

    /* The chrome is skipped when the damage cannot touch it: a pointer move
     * repaints two boxes in the play area, and redrawing the strip, the hotbar
     * and the held block for that costs as much again as the world pass. */
    {
        Rect chrome;
        Rect touched;
        Boolean wantsStrip = fullPaint || sStripDirty;
        Boolean wantsHotbar = fullPaint;

        /* Either strip is redrawn when the damage reaches it. The old test only
         * ever *cleared* these flags, so a repaint aimed at the hotbar (a menu
         * toggle, a click on a slot) redrew nothing and the white selection
         * frame stayed where it was. */
        if (SectRect(&sDamageBox, &sTitleStripRect, &touched)) {
            wantsStrip = true;
        }
        if (SectRect(&sDamageBox, &sBlockSelectorRect, &touched)) {
            wantsHotbar = true;
        }
        Sprite_BeginFrame();
        if (wantsStrip) {
            Sprite_SetView(&sTitleStripRect);
            {
                short hoverX = 0;
                short hoverY = 0;

                Hover_Tile(&hoverX, &hoverY, NULL);
                if (!Hover_Valid()) {
                    hoverX = (short)(sPlayer.x >> 16);
                    hoverY = (short)(sPlayer.y >> 16);
                }
                HUD_DrawStrip(Hover_Block(), (BlockID)(sBlockType + 1),
                              hoverX, hoverY);
            }
        }
        if (wantsHotbar) {
            Sprite_SetView(&sBlockSelectorRect);
            HUD_DrawHotbar((BlockID)(sBlockType + 1), kGameNumBlockTypes);
        }
        Sprite_SetView(&sDisplayRect);
    }
    Sprite_EndFrame();

    Sprite_Phase[2] = (long)((unsigned long)TickCount() - (unsigned long)Sprite_Work[4]) - Sprite_Phase[0] - Sprite_Phase[1];

    sLastCamX = sCamX;
    sLastCamY = sCamY;
    sHaveFrame = true;

    {
        unsigned long now = (unsigned long)TickCount();

        Sprite_Work[2] += (long)(now - (unsigned long)Sprite_Work[4]);
        Sprite_Work[3]++;
    }

    SetPenState(&savedPen);
}

/* Handle update events for the game window */
static void GameWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    GameWindow_Render();

    EndUpdate(sWindow);
}



void GameWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
    case keyUp:
        /* Releasing the key stops the walk on the spot rather than letting the
         * hold deadline expire. */
        sIntentX = 0;
        sIntentY = 0;
        sJumpHeld = false;
        sIntentUntil = 0;
        break;

    case keyDown:
    case autoKey:
        /* Command-key combinations belong to the menu bar (MenuKey). */
        if ((event->modifiers & cmdKey) == 0) {
            GameWindow_HandleKeyDown((char)(event->message & charCodeMask));
        }
        break;

    case mouseDown: {
        WindowRef clickedWindow;
        short part;
        Point localPt;
        GrafPtr savedPort;

        part = FindWindow(event->where, &clickedWindow);
        if (clickedWindow != sWindow || part != inContent) {
            break;
        }

        GetPort(&savedPort);
        SetPort(sWindow);
        localPt = event->where;
        GlobalToLocal(&localPt);
        GameWindow_HandleContentClick(localPt, (event->modifiers & shiftKey) != 0);
        SetPort(savedPort);
        break;
    }

    case activateEvt:
        if ((WindowPtr)event->message == sWindow) {
            GameWindow_HandleActivate((event->modifiers & activeFlag) != 0);
        }
        break;

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            GameWindow_Update();
        }
        break;

    default:
        break;
    }

    {
        unsigned long t0 = (unsigned long)TickCount();

        UpdateHover();
        Sprite_Work[0] += (long)((unsigned long)TickCount() - t0);
        Sprite_Work[1]++;
    }
    /* Keep the held block's swing animating: nothing else invalidates it, and
     * only the block's own corner needs repainting. */
    if (sSwingUntil > (unsigned long)TickCount()) {
        Rect swingRect;

        HeldBlockRect(&swingRect);
        MarkDirty(&swingRect);
    }
    FlushDirty();
}

/* Show, hide or dispose of the game window. */
void GameWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        ShowWindow(sWindow);
        SelectWindow(sWindow);
        sIsVisible = true;

        /* The update event performs the actual drawing. */
        MarkDirty(&sWindow->portRect);
        FlushDirty();
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
        sDirty     = false;
    }
}

/* Get the window reference for the game window */
WindowRef GameWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Determine if the window is visible */
/* Write the world now, for the quit path and the harness. */
OSErr GameWindow_SaveWorld(void)
{
    return Save_Write();
}

/* Harness hook: walk with the same intent the keyboard sets, for a number of
 * physics ticks, then recentre. This exists because the harness cannot deliver
 * keyboard events into the guest; it drives Player_Step exactly as the game
 * does, so the evidence is about the game and not about the driver. */
void GameWindow_WalkForTest(short dx, short dy, short ticks)
{
    short i;

    for (i = 0; i < ticks; i++) {
        Rect figure;

        /* Same damage the idle loop produces: the figure where it is and where
         * it lands, nothing else. */
        PlayerFigureRect(&figure);
        MarkDirty(&figure);
        Player_Step(&sPlayer, dx, dy, false);
        PlayerFigureRect(&figure);
        MarkDirty(&figure);
    }
    CentreOnPlayer();
    FlushDirty();
}

/* Harness hook: jump once and let it land. */
void GameWindow_JumpForTest(void)
{
    short i;

    Player_Step(&sPlayer, 0, 0, true);
    for (i = 0; i < 60; i++) {
        Player_Step(&sPlayer, 0, 0, false);
    }
    CentreOnPlayer();
    MarkDirty(&sDisplayRect);
    FlushDirty();
}

/* Player state for the harness: the simulation is provable, so the harness
 * asserts on it instead of trusting a picture. */
Boolean GameWindow_PlayerGrounded(void)
{
    return sPlayer.onGround;
}

/* Put the player on a tile, feet at the surface, for harness checks. */
void GameWindow_PlacePlayerForTest(short x, short y, short z)
{
    sPlayer.x = ((long)x << 16) + (kPlayerOne / 2);
    sPlayer.y = ((long)y << 16) + (kPlayerOne / 2);
    sPlayer.z = (long)z << 16;
    sPlayer.vx = 0;
    sPlayer.vy = 0;
    sPlayer.vz = 0;
    sPlayer.onGround = false;
    CentreOnPlayer();
    MarkDirty(&sDisplayRect);
}

short GameWindow_PlayerTileX(void)
{
    return (short)(sPlayer.x >> 16);
}

short GameWindow_PlayerTileY(void)
{
    return (short)(sPlayer.y >> 16);
}

short GameWindow_PlayerZ(void)
{
    return (short)(sPlayer.z >> 16);
}

short GameWindow_PlayerRespawns(void)
{
    return sPlayer.respawns;
}

/* Jump once, let it land, and report the highest point reached above the start,
 * in whole blocks. Drives the same Player_Step the game runs. */
short GameWindow_PlayerJumpPeakForTest(void)
{
    Player save;
    short start;
    short peak;
    short i;

    save = sPlayer;

    /* Settle first: a jump does nothing mid-air, and the window has only just
     * opened when the harness asks. */
    for (i = 0; i < 30; i++) {
        Player_Step(&sPlayer, 0, 0, false);
    }
    start = (short)(sPlayer.z >> 16);
    peak = 0;

    for (i = 0; i < 60; i++) {
        Player_Step(&sPlayer, 0, 0, i == 0);
        if ((short)(sPlayer.z >> 16) - start > peak) {
            peak = (short)((short)(sPlayer.z >> 16) - start);
        }
    }
    sPlayer = save;
    return peak;
}

/* Rebuild the island and respawn the player, for the Game menu's Reset World. */
void GameWindow_ResetWorld(void)
{
    Save_Delete();
    Scene_BuildIsland();
    SpawnPlayerOnClearGround();
    Hover_Reset();
    CentreOnPlayer();
    MarkDirty(&sDisplayRect);
    FlushDirty();
}

/* Repaint the play area `iterations` times and report how long each phase took,
 * so render cost can be hill-climbed from the harness instead of guessed at.
 * Ticks are 1/60 s. */
long Sprite_Timing[4];
extern long Sprite_Probe[2];

unsigned long GameWindow_TimeRender(short iterations, short *outBlocks)
{
    unsigned long tick;
    short i;

    Sprite_Timing[0] = 0;
    Sprite_Timing[1] = 0;
    if (outBlocks != NULL) {
        *outBlocks = 0;
    }
    if (!sInitialized || sWindow == NULL) {
        return 0;
    }

    SetPort(sWindow);
    for (i = 0; i < iterations; i++) {
        BeginUpdate(sWindow);
        sRenderRect = sDisplayRect;
        DetectColor();

        tick = (unsigned long)TickCount();
        Sky_Draw(sWindow, sDisplayRect, sHasColor, 0);
        Sprite_Timing[0] += (long)((unsigned long)TickCount() - tick);

        tick = (unsigned long)TickCount();
        Sprite_BeginFrame();
        {
            WorldView view;

            view.display = sDisplayRect;
            view.camX = sCamX;
            view.camY = sCamY;
            view.hasColor = sHasColor;
            WorldView_Draw(&view, &sDisplayRect);
        }
        Sprite_EndFrame();
        Sprite_Timing[1] += (long)((unsigned long)TickCount() - tick);

        EndUpdate(sWindow);
    }

    /* Second pass with the copy switched off: the difference is what the pixel
     * writes cost, the remainder is the traversal and the lighting. */
    Sprite_Timing[2] = Sprite_Timing[1];
    Sprite_Timing[1] = 0;
    Sprite_Probe[0] = 1;
    for (i = 0; i < iterations; i++) {
        BeginUpdate(sWindow);
        tick = (unsigned long)TickCount();
        Sprite_BeginFrame();
        {
            WorldView view;

            view.display = sDisplayRect;
            view.camX = sCamX;
            view.camY = sCamY;
            view.hasColor = sHasColor;
            WorldView_Draw(&view, &sDisplayRect);
        }
        Sprite_EndFrame();
        Sprite_Timing[1] += (long)((unsigned long)TickCount() - tick);
        EndUpdate(sWindow);
    }
    Sprite_Probe[0] = 0;

    if (outBlocks != NULL) {
        *outBlocks = World_Height(24, 24);
    }
    Sprite_Timing[3] = Sprite_Timing[1];
    return (unsigned long)(Sprite_Timing[0] + Sprite_Timing[2]);
}

Boolean GameWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return sIsVisible;
}

/* Toggle between block types */
void GameWindow_ToggleBlockType(void)
{
    if (!sInitialized || !sIsVisible) {
        return;
    }

    sBlockType = (short)((sBlockType + 1) % kGameNumBlockTypes);
    MarkDirty(&sBlockSelectorRect);
    sStripDirty = true;   /* the readout names the block that would be placed */
}

/* Perform idle processing: turn a recorded dirty rect into an InvalRect. */
void GameWindow_Idle(void)
{
    static unsigned long sFrontFirst = 0;
    static unsigned long sFrontLast = 0;

    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    /* Take the front once, on the first idle pass. Doing it in Initialize loses
     * the race: a game started from Startup Items runs before the Finder has
     * finished launching, and the Finder then comes forward over it -- leaving
     * the window inactive, so its clicks went to the desktop and the menu bar
     * belonged to the Finder. */
    /* Take the front and keep it until we actually have it. A single request at
     * startup loses the race with the Finder, which comes forward after Startup
     * Items finish; the window then sits inactive, so its clicks go to the
     * desktop and its keys go nowhere. Asking again while somebody else owns the
     * front fixes that without fighting the user: the moment we are front, this
     * stops. */
    {
        static unsigned long sLastFrontTry = 0;
        unsigned long now = (unsigned long)TickCount();

        if (sLastFrontTry == 0 || (unsigned long)(now - sLastFrontTry) > 60) {
            ProcessSerialNumber mine;
            ProcessSerialNumber front;

            sLastFrontTry = now;
            if (GetCurrentProcess(&mine) == noErr &&
                (GetFrontProcess(&front) != noErr ||
                 (front.highLongOfPSN != mine.highLongOfPSN ||
                  front.lowLongOfPSN != mine.lowLongOfPSN))) {
                SetFrontProcess(&mine);
            }
        }
    }

    /* Physics, at a fixed 60 Hz whatever the event rate: gravity must not depend
     * on how often the user moves the mouse, and catch-up is capped so a long
     * pause cannot teleport the player. This is the only place the simulation
     * advances; the keyboard sets the intent, nothing else. */
    {
        static unsigned long sLastPhysics = 0;
        unsigned long now = (unsigned long)TickCount();
        short steps = 0;
        short wasCamX = sCamX;
        short wasCamY = sCamY;
        short wasTileX = (short)(sPlayer.x >> 16);
        short wasTileY = (short)(sPlayer.y >> 16);
        short wasZ = (short)(sPlayer.z >> 16);
        Boolean wasGrounded = sPlayer.onGround;
        Rect oldFigure;

        /* Where the figure is drawn right now: the step is about to move it, and
         * without damage there too the old drawing stayed behind as a trail. */
        PlayerFigureRect(&oldFigure);

        if (sLastPhysics == 0) {
            sLastPhysics = now;
        }
        while (sLastPhysics != now && steps < 3) {
            Player_Step(&sPlayer, sIntentX, sIntentY, sJumpHeld);
            Sprite_Work[5]++;
            sLastPhysics++;
            steps++;
        }

        if (steps > 0) {
            short tileX = (short)(sPlayer.x >> 16);
            short tileY = (short)(sPlayer.y >> 16);

            /* A footstep when the tile changes under a walking player, and one
             * when the feet come back down -- so a fall lands audibly too. */
            if ((tileX != wasTileX || tileY != wasTileY) && sPlayer.onGround) {
                /* The surface decides the sound: stone and brick ring, soil and
                 * grass thud. Read the block directly under the feet. */
                BlockID floor = World_Get(tileX, tileY, (short)((sPlayer.z >> 16) - 1));
                Boolean hard = (Boolean)(floor == kBlockStone || floor == kBlockCobblestone ||
                                         floor == kBlockBrick || floor == kBlockBedrock ||
                                         floor == kBlockGlass);

                Sfx_Play(hard ? kSfxStepStone : kSfxStep);
            }
            if (sPlayer.onGround && !wasGrounded) {
                Sfx_Play(sWasInWater ? kSfxSplash : kSfxLand);
            }
            /* Take-off: the tick the player leaves the ground under its own
             * power, which is not the same as walking off a ledge. */
            if (!sPlayer.onGround && wasGrounded && sPlayer.vz > 0) {
                Sfx_Play(kSfxJump);
            }
            /* Near water: a quiet blip every couple of seconds, so the pond is
             * audible before you are in it. */
            {
                static unsigned long sLastWaterSound = 0;
                unsigned long now = (unsigned long)TickCount();

                if (Player_IsNearWater(&sPlayer, 3) &&
                    (unsigned long)(now - sLastWaterSound) > 120) {
                    sLastWaterSound = now;
                    Sfx_Play(kSfxAmbientWater);
                }
            }

            /* Entering water is worth hearing once, not every tick. */
            {
                Boolean inWater = Player_InWater(&sPlayer);

                if (inWater && !sWasInWater) {
                    Sfx_Play(kSfxSplash);
                    /* Spray: water-coloured specks where the body broke the
                     * surface. */
                    Particles_Spawn((short)(sPlayer.x >> 16), (short)(sPlayer.y >> 16),
                                    (short)(sPlayer.z >> 16), kBlockWater);
                }
                sWasInWater = inWater;
            }

            /* Walk cycle: advance while moving on the ground, reset in the air. */
            if (!sPlayer.onGround) {
                sWalkPhase = 0;
            }
            else if (tileX != wasTileX || tileY != wasTileY) {
                sWalkPhase = (short)((sWalkPhase + 1) & 7);
            }

            CentreOnPlayer();

            /* Break particles: advance them and damage where they were, so the
             * specks leave no trail. */
            if (Particles_Update()) {
                Rect specks;

                Particles_Bounds(&sDisplayRect, sCamX, sCamY, &specks);
                if (specks.right > specks.left) {
                    MarkDirty(&specks);
                }
            }

            /* Repaint only when something a viewer can see moved. */
            if (sCamX != wasCamX || sCamY != wasCamY) {
                MarkDirty(&sDisplayRect);
            }
            else if (tileX != wasTileX || tileY != wasTileY ||
                     (short)(sPlayer.z >> 16) != wasZ) {
                Rect figure;

                MarkDirty(&oldFigure);
                PlayerFigureRect(&figure);
                MarkDirty(&figure);
            }

            /* The intent is cleared by key-up, or here when the repeats stop
             * arriving (the keyboard driver may go quiet mid-hold). */
            if ((unsigned long)TickCount() > sIntentUntil) {
                sIntentX = 0;
                sIntentY = 0;
                sJumpHeld = false;
            }
        }
    }

    /* Mining: advance while the button stays down on the same tile, cancel the
     * moment it is released. The crack overlay is repainted as it grows. */
    if (sBreaking) {
        if (Button()) {
            sBreakTicks++;
            if (sBreakTicks >= kBreakTicksNeeded) {
                FinishBreak();
            }
            else {
                Rect crackRect;

                /* A tick every few swings, so the hold sounds like progress
                 * instead of silence followed by a bang. */
                if ((sBreakTicks % 6) == 0) {
                    Sfx_Play(kSfxClick);
                }
                TileBox(sBreakX, sBreakY, sBreakZ, &crackRect);
                MarkDirty(&crackRect);
            }
        }
        else {
            Rect crackRect;

            TileBox(sBreakX, sBreakY, sBreakZ, &crackRect);
            MarkDirty(&crackRect);
            sBreaking = false;
            sBreakTicks = 0;
        }
    }

    /* Commands from the harness, at most once a second: see remote.h for why the
     * shared folder is the input channel and the keyboard is not. */
    {
        static unsigned long sLastRemote = 0;
        unsigned long now = (unsigned long)TickCount();

        if ((unsigned long)(now - sLastRemote) > 60) {
            char cmd[64];

            sLastRemote = now;
            if (Remote_Take(cmd, (short)sizeof(cmd))) {
                Remote_Apply(cmd);
                Remote_Ack(cmd);
            }
        }
    }

    /* Hover is polled here rather than only on events: the app is often behind
     * the Finder after a Startup Items launch, so it receives no events at all
     * and GetMouse is the only way to follow the pointer. */
    {
        unsigned long t0 = (unsigned long)TickCount();

        UpdateHover();
        Sprite_Work[0] += (long)((unsigned long)TickCount() - t0);
        Sprite_Work[1]++;
    }

    /* Keep the held block's swing animating: nothing else invalidates it, and
     * only the block's own corner needs repainting. */
    if (sSwingUntil > (unsigned long)TickCount()) {
        Rect swingRect;

        HeldBlockRect(&swingRect);
        MarkDirty(&swingRect);
    }

    if (sStripDirty) {
        GrafPtr saved;

        sStripDirty = false;
        GetPort(&saved);
        SetPort(sWindow);
        Sprite_BeginFrame();
        Sprite_SetView(&sTitleStripRect);
        {
            short hoverX = 0;
            short hoverY = 0;

            Hover_Tile(&hoverX, &hoverY, NULL);
            if (!Hover_Valid()) {
                hoverX = (short)(sPlayer.x >> 16);
                hoverY = (short)(sPlayer.y >> 16);
            }
            HUD_DrawStrip(Hover_Block(), (BlockID)(sBlockType + 1), hoverX, hoverY);
        }
        Sprite_SetView(&sDisplayRect);
        Sprite_EndFrame();
        SetPort(saved);
    }

    FlushDirty();
}

/* ---------------------------------------------------------------- */
/* Test hooks (see src/selftest.c)                                   */
/* ---------------------------------------------------------------- */

Boolean GameWindow_ClickForTest(short localX, short localY, Boolean shiftDown)
{
    Point pt;

    pt.h = localX;
    pt.v = localY;
    return GameWindow_HandleContentClick(pt, shiftDown);
}

Boolean GameWindow_KeyForTest(char key)
{
    return GameWindow_HandleKeyDown(key);
}

void GameWindow_StateForTest(short *camX, short *camY, short *blockType)
{
    if (camX != NULL) *camX = sCamX;
    if (camY != NULL) *camY = sCamY;
    if (blockType != NULL) *blockType = sBlockType;
}
