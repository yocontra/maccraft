#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextUtils.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "../game/blocks.h"
#include "../game/iso.h"
#include "../game/scene.h"
#include "../game/sky.h"
#include "../game/light.h"
#include "../game/texture.h"
#include "../game/world.h"
#include "game_window.h"
#include "utils.h"

/* Module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;       /* Main content area */
static Rect sBlockSelectorRect; /* Block selector area at bottom */
static Rect sDisplayRect;       /* Game view area */

/* Camera offset in display-local coordinates (see Iso_Project). */
static short sCamX = 0;
static short sCamY = 0;

/* Selected swatch index; the placed block id is sBlockType + 1. */
static short sBlockType = 0;

/* Pending repaint: state changes record the smallest affected rect here and
 * GameWindow_Idle turns it into an InvalRect, so painting only ever happens
 * inside a BeginUpdate/EndUpdate pass. */
static Boolean sDirty = false;
static Rect sDirtyRect;

/* Intersection of the display rect with the current update region's bounds,
 * so an update pass only walks the tiles it can actually paint. */
static Rect sRenderRect;

/* Private function declarations */
static void MarkDirty(const Rect *r);
static void FlushDirty(void);
static void GameWindow_Update(void);
static void GameWindow_Render(void);
static void DrawWorld(Rect *view);
static void PaintQuad(short x0, short y0, short x1, short y1, short x2, short y2, short x3,
                      short y3, BlockID id, short face, short shade, short variation);

/* Scene and block palette. The patterns are for 1-bit displays; the colours
 * are what a colour display actually fills with. */
static const RGBColor kSceneBack   = {0x8800, 0xAA00, 0xDD00};
static const RGBColor kSceneGround = {0x9999, 0x8888, 0x6666};
/* Texture for the colour path. Sparse on purpose: a 50% pattern at 32x16
 * turns into a checkerboard of noise, while one dot per 8x8 cell reads as
 * grain and still lets the base colour dominate. */
static const Pattern kGrainSparse = {0x11, 0x00, 0x44, 0x00, 0x11, 0x00, 0x00, 0x22};
static const Pattern kGrainMedium = {0x11, 0x44, 0x00, 0x11, 0x88, 0x00, 0x22, 0x00};

static const RGBColor kOutline     = {0x0000, 0x0000, 0x0000};

static const RGBColor kGrassTop    = {0x0000, 0xBB00, 0x2200};
static const RGBColor kGrassLeft   = {0x0000, 0x5500, 0x0000};
static const RGBColor kGrassRight  = {0x0000, 0x8800, 0x1800};

static const RGBColor kDirtTop     = {0xAA00, 0x5500, 0x0000};
static const RGBColor kDirtLeft    = {0x8800, 0x4400, 0x1100};
static const RGBColor kDirtRight   = {0x8800, 0x4400, 0x0000};

static const RGBColor kStoneTop    = {0xAAAA, 0xAAAA, 0xAAAA};
static const RGBColor kStoneLeft   = {0x5555, 0x5555, 0x5555};
static const RGBColor kStoneRight  = {0x8888, 0x8888, 0x8888};

/* Filled polygons take their colour from the *pen pattern*, so a plain
 * 1-bit pattern can only ever be a shade of grey: PaintPoly with qd.white
 * yields white and RGBForeColor is ignored. MakeRGBPat builds a pattern that
 * renders as an actual colour on a colour display and as a dither on a 1-bit
 * one, which is what makes the faces distinguishable on both. */
/* Whether the display has colour. On a 1-bit screen a face is a pattern; on a
 * colour screen the same pattern's 1-bits take the foreground colour, so set
 * that colour and fill with an all-ones pattern. */
static Boolean sHasColor = false;

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
static void BlockFaces(BlockID id, Pattern **topPat, const RGBColor **topCol,
                       Pattern **leftPat, const RGBColor **leftCol,
                       Pattern **rightPat, const RGBColor **rightCol)
{
    switch (id) {
    case 2: /* stone */
        *topPat = &qd.ltGray;  *topCol = &kStoneTop;
        *leftPat = &qd.dkGray; *leftCol = &kStoneLeft;
        *rightPat = &qd.gray;  *rightCol = &kStoneRight;
        break;
    case 3: /* grass */
        *topPat = &qd.white;   *topCol = &kGrassTop;
        *leftPat = &qd.dkGray; *leftCol = &kGrassLeft;
        *rightPat = &qd.gray;  *rightCol = &kGrassRight;
        break;
    case 1: /* dirt */
    default:
        *topPat = &qd.gray;    *topCol = &kDirtTop;
        *leftPat = &qd.dkGray; *leftCol = &kDirtLeft;
        *rightPat = &qd.gray;  *rightCol = &kDirtRight;
        break;
    }
}

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

    Scene_BuildIsland();
}

/* Dispose of the window and free what it owned. */
void GameWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

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
    InvalRect(&r);
}

/* Face lookups come from src/game/texture.c, which owns the per-block,
 * per-face base colour, base pattern and grain for both display types. */
static Pattern *FacePattern(BlockID id, short face)
{
    return Texture_BasePattern(id, face);
}

/* Grain patterns: the second tone of each face. On a colour display the base
 * is a solid colour and this pattern is filled in a darker shade of it; on a
 * 1-bit display the base is a pattern and this one is filled in black, so a
 * face still reads as textured on a Macintosh Plus. */
static Pattern *FaceGrainPattern(BlockID id, short face, Boolean hasColor)
{
    return Texture_GrainPattern(id, face, hasColor);
}

static const RGBColor *FaceGrainColor(BlockID id, short face)
{
    return Texture_GrainColor(id, face, true);
}

static RGBColor Shaded(const RGBColor *base, short factor);

/* One palette step brighter, clamped: the ink of the face texture. */
static RGBColor Lightened(const RGBColor *base)
{
    RGBColor result;
    unsigned short bump = 0x1A1A;

    result.red   = (unsigned short)(base->red   > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->red + bump);
    result.green = (unsigned short)(base->green > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->green + bump);
    result.blue  = (unsigned short)(base->blue  > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->blue + bump);
    return result;
}

/* Convert the palette module's colour into the QuickDraw one. The two structs
 * share a field layout on purpose; this is the only place that relies on it. */
static RGBColor FromBlockColor(BlockColor c)
{
    RGBColor result;

    result.red   = c.red;
    result.green = c.green;
    result.blue  = c.blue;
    return result;
}

static RGBColor FaceBase(BlockID id, short face, short variation)
{
    const BlockLook *look = Block_Look(id);
    BlockColor base;

    switch (face) {
    case kBlockFaceLeft:
        base = look->left;
        break;
    case kBlockFaceRight:
        base = look->right;
        break;
    default:
        base = look->top;
        break;
    }
    return FromBlockColor(Block_Tone(base, variation));
}

/* The grain is the same hue, one ramp step down: enough to read as material,
 * not enough to compete with the base colour. */
static RGBColor FaceGrain(BlockID id, short face, short variation)
{
    RGBColor base = FaceBase(id, face, variation);

    return Shaded(&base, 200);
}

/* Fill one face. FillPoly takes an explicit pattern: on a colour display the
 * pattern's 1-bits take the foreground colour, so an all-ones pattern plus
 * RGBForeColor is a solid colour; on a 1-bit display the pattern itself is the
 * fill. Painting the polygon any other way filled with the pen colour and
 * produced the black play area. */
/* Apply a light factor to a palette colour. The two colour structs have the
 * same field layout on purpose, and this is the only place that relies on it. */
static RGBColor Shaded(const RGBColor *base, short factor)
{
    LightColor in;
    LightColor out;
    RGBColor result;

    in.red   = base->red;
    in.green = base->green;
    in.blue  = base->blue;
    out = Light_Shade(in, factor);

    result.red   = out.red;
    result.green = out.green;
    result.blue  = out.blue;
    return result;
}

/* Build the quad as a fresh PolyHandle. FillPoly cannot be called twice on one
 * handle here: the second call filled the whole polygon with garbage, which is
 * why every face went black when the grain overlay was added. One polygon per
 * fill is cheap and always correct. */
static PolyHandle QuadPoly(short x0, short y0, short x1, short y1, short x2, short y2,
                           short x3, short y3)
{
    PolyHandle poly = OpenPoly();

    if (poly == NULL) {
        return NULL;
    }
    MoveTo(x0, y0);
    LineTo(x1, y1);
    LineTo(x2, y2);
    LineTo(x3, y3);
    ClosePoly();
    return poly;
}

static void PaintQuad(short x0, short y0, short x1, short y1, short x2, short y2, short x3,
                      short y3, BlockID id, short face, short shade, short variation)
{
    PolyHandle poly;

    poly = QuadPoly(x0, y0, x1, y1, x2, y2, x3, y3);
    if (poly == NULL) {
        return;
    }

    if (sHasColor) {
        /* One patterned fill, two colours: the kind's grain stencil decides
         * which pixels take the highlight and which take the base. A second
         * FillPoly is not an option here -- it blacks the polygon out -- but a
         * single patterned fill is how the 1-bit path has always textured a
         * face, and the 0-bits take the background colour.
         *
         * Ink is one step brighter than the base with a sparse stencil, so a
         * face is its own colour with a speckle of highlight, which is
         * Minecraft's surface noise. Inverting that (ink = base, ground =
         * darker) made every face read as its own shadow and turned the plateau
         * almost black. */
        RGBColor base = Shaded(&(RGBColor){FaceBase(id, face, variation).red,
                                           FaceBase(id, face, variation).green,
                                           FaceBase(id, face, variation).blue},
                               shade);
        RGBColor highlight;
        Pattern stamp;

        highlight = Lightened(&base);
        Block_Grain(id, face, variation, (unsigned char *)&stamp);
        RGBForeColor(&highlight);
        RGBBackColor(&base);
        FillPoly(poly, &stamp);
    }
    else {
        /* One bit per pixel: the kind's grain stencil is the only thing that
         * tells one block from another, so it is the fill itself. */
        Pattern stamp;

        Block_Grain(id, face, variation, (unsigned char *)&stamp);
        FillPoly(poly, &stamp);
    }

    KillPoly(poly);
}

/* The selection box: Minecraft's black wireframe on the block you are looking
 * at. Drawn after the world so it is never covered. Three edges matter on an
 * isometric cube -- the three vertical ones at the silhouette corners -- plus
 * the top diamond; the hidden edges are not drawn, exactly as in the game. */
static void DrawSelectionBox(short x, short y, short z)
{
    short sx, sy;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx += sDisplayRect.left;
    sy += sDisplayRect.top;

    PenNormal();
    PenPat(&qd.black);
    MoveTo(sx, (short)(sy - kTileH / 2));
    LineTo((short)(sx + kTileW / 2), sy);
    LineTo(sx, (short)(sy + kTileH / 2));
    LineTo((short)(sx - kTileW / 2), sy);
    LineTo(sx, (short)(sy - kTileH / 2));

    /* Near vertical edge, then the two silhouette ones. */
    MoveTo(sx, (short)(sy + kTileH / 2));
    LineTo(sx, (short)(sy + kTileH / 2 + kTileH));
    MoveTo((short)(sx - kTileW / 2), sy);
    LineTo((short)(sx - kTileW / 2), (short)(sy + kTileH / 2));
    MoveTo((short)(sx + kTileW / 2), sy);
    LineTo((short)(sx + kTileW / 2), (short)(sy + kTileH / 2));
}

/* Draw one block: top diamond plus its two visible side faces, all centred on
 * Iso_Project(x, y, z, ...) and clipped nowhere (the update region clips). */
static void DrawBlock(short x, short y, short z, BlockID id)
{
    short sx, sy;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx += sDisplayRect.left;
    sy += sDisplayRect.top;

    /* One light factor per face: the sun's angle multiplied by however much of
     * the face is buried. Three neighbour lookups per block, no drawing cost. */
    short topShade   = Light_Combine(Light_FaceFactor(kLightFaceTop),
                                     Light_Occlusion(x, y, z, kLightFaceTop));
    short leftShade  = Light_Combine(Light_FaceFactor(kLightFaceLeft),
                                     Light_Occlusion(x, y, z, kLightFaceLeft));
    short rightShade = Light_Combine(Light_FaceFactor(kLightFaceRight),
                                     Light_Occlusion(x, y, z, kLightFaceRight));
    short topVariation = Block_Variation(x, y, z);

    /* Cull the faces that are buried. Without this every block paints three
     * faces, so the interior of a hill is a mass of hidden quads drawn under
     * the visible ones -- wasted work on a 25 MHz machine and, worse, visible
     * dark seams wherever the geometry is off by a pixel.
     *
     * Which neighbour hides which face follows from the projection:
     * screenX = (x - y) * 16, screenY = (x + y) * 8, so the block at (x, y + 1)
     * sits one tile down-left (it hides the left face) and (x + 1, y) one tile
     * down-right (it hides the right). */
    Boolean showTop   = World_Get(x, y, (short)(z + 1)) == 0;
    Boolean showLeft  = World_Get(x, (short)(y + 1), z) == 0;
    Boolean showRight = World_Get((short)(x + 1), y, z) == 0;

    /* Top face: diamond centred at (sx, sy), kTileW x kTileH. */
    if (showTop) {
        PaintQuad(sx, sy - kTileH / 2, sx + kTileW / 2, sy, sx, sy + kTileH / 2,
                  sx - kTileW / 2, sy, id, kBlockFaceTop, topShade, topVariation);
    }

    /* Left face. */
    if (showLeft) {
        PaintQuad(sx - kTileW / 2, sy, sx, sy + kTileH / 2, sx, sy + kTileH,
                  sx - kTileW / 2, sy + kTileH / 2, id, kBlockFaceLeft, leftShade, topVariation);
    }

    /* Right face. */
    if (showRight) {
        PaintQuad(sx, sy + kTileH / 2, sx + kTileW / 2, sy, sx + kTileW / 2, sy + kTileH / 2,
                  sx, sy + kTileH, id, kBlockFaceRight, rightShade, topVariation);
    }

    /* No outline. A block is separated from its neighbours by the shade step
     * between the top face and the two sides plus the contact shadow, exactly
     * as in the reference art; a 1 px black grid over every face is what made
     * the world look like a spreadsheet. */
}

/* Inverse of Iso_Project's documented z=0 mapping, clamped to the world.
 * Used only to derive the visible tile range; clicks use Iso_Unproject. */
static short FloorDivInt(int a, int b)
{
    int q = a / b;

    if ((a % b) != 0 && ((a < 0) != (b < 0))) {
        q--;
    }
    return (short)q;
}

static void UnprojectClamped(short px, short py, short *outX, short *outY)
{
    short sx, sy, x, y;

    sx = FloorDivInt((int)px - (int)sCamX, kTileW / 2);
    sy = FloorDivInt((int)py - (int)sCamY, kTileH / 2);

    x = FloorDivInt((int)sx + (int)sy, 2);
    y = FloorDivInt((int)sy - (int)sx, 2);

    if (x < 0) {
        x = 0;
    }
    else if (x >= kWorldX) {
        x = kWorldX - 1;
    }

    if (y < 0) {
        y = 0;
    }
    else if (y >= kWorldY) {
        y = kWorldY - 1;
    }

    *outX = x;
    *outY = y;
}

/* Visible tile range for the part of the display being repainted, in
 * display-local coordinates. Expanded by kWorldZ tiles so that raised columns
 * whose tops are in view but whose base tiles are below still render. */
static void ComputeVisibleRange(Rect *view, short *xMin, short *yMin, short *xMax, short *yMax)
{
    short left, top, right, bottom, x, y;

    left   = (short)(view->left - sDisplayRect.left);
    top    = (short)(view->top - sDisplayRect.top);
    right  = (short)(view->right - sDisplayRect.left);
    bottom = (short)(view->bottom - sDisplayRect.top);

    UnprojectClamped(left, top, xMin, yMin);
    *xMax = *xMin;
    *yMax = *yMin;

    UnprojectClamped(right, top, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(left, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(right, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    *xMin -= kWorldZ;
    *yMin -= kWorldZ;
    *xMax += kWorldZ;
    *yMax += kWorldZ;

    if (*xMin < 0) *xMin = 0;
    if (*yMin < 0) *yMin = 0;
    if (*xMax >= kWorldX) *xMax = kWorldX - 1;
    if (*yMax >= kWorldY) *yMax = kWorldY - 1;
}

/* Draw the world, bottom z level first, back-to-front inside each level. */
static void DrawWorld(Rect *view)
{
    short xMin, yMin, xMax, yMax, z, sum, x, y, sumMax;
    BlockID id;

    ComputeVisibleRange(view, &xMin, &yMin, &xMax, &yMax);
    sumMax = xMax + yMax;

    for (z = 0; z < kWorldZ; z++) {
        for (sum = xMin + yMin; sum <= sumMax; sum++) {
            for (x = xMin; x <= xMax; x++) {
                y = sum - x;
                if (y < yMin || y > yMax) {
                    continue;
                }

                id = World_Get(x, y, z);
                if (id != 0) {
                    DrawBlock(x, y, z, id);
                }
            }
        }
    }
}

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
static void DrawBlockSelector(void)
{
    Rect frameRect  = sBlockSelectorRect;
    Rect shadowRect = frameRect;
    Rect blockRect;
    short i;

    /* Shadow effect */
    PenNormal();
    PenSize(1, 1);
    OffsetRect(&shadowRect, 1, 1);
    PenPat(&qd.gray);
    FrameRect(&shadowRect);
    PenNormal();

    /* Frame around the block selector area */
    ForeColor(blackColor);
    FrameRect(&frameRect);

    /* Light background */
    InsetRect(&frameRect, 1, 1);
    BackPat(&qd.white);
    BackColor(whiteColor);
    EraseRect(&frameRect);

    /* Draw the three block swatches (dirt, stone, grass) */
    for (i = 0; i < kGameNumBlockTypes; i++) {
        SelectorBlockRect(i, &blockRect);

        PenNormal();
        ForeColor(blackColor);
        FrameRect(&blockRect);

        if (sHasColor) {
            /* Swatches read from the same palette the world does, so the strip
             * always tells the truth about what a block looks like. */
            RGBColor swatch = FaceBase((BlockID)(i + 1), kBlockFaceTop, 0);

            RGBForeColor(&swatch);
            PenPat(&qd.black);
        }
        else {
            /* One bit per pixel: the kind's own grain stencil stands in for the
             * colour that a 1-bit screen cannot show. */
            Pattern stamp;

            Block_Grain((BlockID)(i + 1), kBlockFaceTop, 0, (unsigned char *)&stamp);
            PenPat(&stamp);
        }
        PaintRect(&blockRect);

        /* Highlight the selected block type */
        if (i == sBlockType) {
            Rect highlightRect = blockRect;
            InsetRect(&highlightRect, -2, -2);
            PenNormal();
            PenSize(2, 2);
            FrameRect(&highlightRect);
            PenSize(1, 1);
        }
    }

    PenNormal();
}

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

/* Handle mouse clicks in the content area of the game window. */
static Boolean GameWindow_HandleContentClick(Point localPt, Boolean shiftDown)
{
    short x, y, topZ, newZ;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Block placement/removal in the game display */
    if (PtInRect(localPt, &sDisplayRect)) {
        if (!Iso_Unproject(localPt.h - sDisplayRect.left, localPt.v - sDisplayRect.top, sCamX,
                           sCamY, &x, &y)) {
            return true;
        }

        topZ = World_TopZ(x, y);

        if (shiftDown) {
            /* Remove the top block of the column */
            if (topZ >= 0 && World_Set(x, y, topZ, 0)) {
                InvalidateColumn(x, y, topZ);
            }
        }
        else {
            /* Place the selected block on top of the column */
            newZ = (short)(topZ + 1);
            if (newZ < kWorldZ && World_Set(x, y, newZ, (BlockID)(sBlockType + 1))) {
                InvalidateColumn(x, y, newZ);
            }
        }

        return true;
    }

    /* Block type selection */
    if (PtInRect(localPt, &sBlockSelectorRect)) {
        short i;
        Rect blockRect;

        for (i = 0; i < kGameNumBlockTypes; i++) {
            SelectorBlockRect(i, &blockRect);
            if (PtInRect(localPt, &blockRect)) {
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
    case '1': /* Select dirt */
        sBlockType = 0;
        MarkDirty(&sBlockSelectorRect);
        return true;

    case '2': /* Select stone */
        sBlockType = 1;
        MarkDirty(&sBlockSelectorRect);
        return true;

    case '3': /* Select grass */
        sBlockType = 2;
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
        PanCamera(-kGameCameraPanX, 0);
        return true;

    case kRightArrowKey:
    case 'd':
    case 'D':
        PanCamera(kGameCameraPanX, 0);
        return true;

    case kUpArrowKey:
    case 'w':
    case 'W':
        PanCamera(0, -kGameCameraPanY);
        return true;

    case kDownArrowKey:
    case 's':
    case 'S':
        PanCamera(0, kGameCameraPanY);
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
static void AppendToStrip(unsigned char *dst, short *len, const char *s, short max)
{
    while (*s != '\0' && *len < max) {
        dst[++(*len)] = (unsigned char)*s++;
    }
}

/* "MacCraft" on the left, block and camera readout on the right. */
static void DrawTitleStrip(void)
{
    Rect strip;
    Str255 text;
    short i, len;
    short centreX, centreY;

    strip = sWindow->portRect;
    strip.top    = kGameViewMargin;
    strip.bottom = sDisplayRect.top - 1;
    strip.left   = sDisplayRect.left;
    strip.right  = sDisplayRect.right;

    /* A hairline under the strip, then the two labels. */
    PenNormal();
    ForeColor(blackColor);
    MoveTo(strip.left, strip.bottom);
    LineTo(strip.right, strip.bottom);

    TextFont(kFontIDGeneva);
    TextSize(9);

    TextFace(bold);
    MoveTo(strip.left + 2, strip.top + 11);
    DrawString("\pMacCraft");

    /* Tile under the middle of the play area. */
    if (!Iso_Unproject((short)((sDisplayRect.right - sDisplayRect.left) / 2),
                       (short)((sDisplayRect.bottom - sDisplayRect.top) / 2),
                       sCamX, sCamY, &centreX, &centreY)) {
        centreX = 0;
        centreY = 0;
    }

    /* "block: grass  cam: 24,24" built by hand: no sprintf on the 68000. The
     * name is the block under the middle of the view -- the one the selection
     * box is drawn on -- falling back to the kind a click would place when the
     * tile is empty. */
    len = 0;
    AppendToStrip(text, &len, "block: ", 200);
    {
        short top = World_TopZ(centreX, centreY);
        BlockID under = (top >= 0) ? World_Get(centreX, centreY, top) : 0;

        AppendToStrip(text, &len, Block_Name(under != 0 ? under : (BlockID)(sBlockType + 1)), 200);
    }
    AppendToStrip(text, &len, "  cam: ", 200);
    {
        Str255 num;
        short d;

        NumToString(centreX, num);
        for (d = 1; d <= num[0] && len < 200; d++) {
            text[++len] = num[d];
        }
        if (len < 200) {
            text[++len] = ',';
        }
        NumToString(centreY, num);
        for (d = 1; d <= num[0] && len < 200; d++) {
            text[++len] = num[d];
        }
    }
    text[0] = (unsigned char)len;

    TextFace(normal);
    MoveTo((short)(strip.right - StringWidth(text) - 2), strip.top + 11);
    DrawString(text);

    TextFace(normal);
}

/* Render the game window contents. Only called between BeginUpdate/EndUpdate. */

static void GameWindow_Render(void)
{
    Rect strip;
    PenState savedPen;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    GetPenState(&savedPen);

    /* World drawing is limited to whole tiles, so it covers the display rect;
     * BeginUpdate has already clipped the port to the update region, which is
     * what keeps a one-column invalidation from repainting the rest. */
    sRenderRect = sDisplayRect;

    /* White window background and content frame */
    BackPat(&qd.white);
    DrawStandardFrame(sWindow);

    /* Play area background. Not black: with a colour port QuickDraw fills from
     * the pen pattern, so a dark background plus dark fills made the entire
     * view a black rectangle. White pattern + RGB background colour gives a
     * solid tint on colour displays and white on 1-bit ones. */
    DetectColor();
    Sky_Draw(sWindow, sDisplayRect, sHasColor, (long)TickCount());

    /* World blocks (nothing to do when only the selector strip was dirty) */
    if (sRenderRect.right > sRenderRect.left && sRenderRect.bottom > sRenderRect.top) {
        DrawWorld(&sRenderRect);

        /* Selection box on whatever sits under the middle of the view. */
        {
            short midX, midY, top;

            if (Iso_Unproject((short)((sDisplayRect.right - sDisplayRect.left) / 2),
                              (short)((sDisplayRect.bottom - sDisplayRect.top) / 2),
                              sCamX, sCamY, &midX, &midY)) {
                top = World_TopZ(midX, midY);
                if (top >= 0 && World_Get(midX, midY, top) != 0) {
                    DrawSelectionBox(midX, midY, top);
                }
            }
        }
    }

    /* Erase the margins so a partially visible edge tile cannot bleed outside
     * the display rect (no manual clipping anywhere). */
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

    DrawTitleStrip();

    /* Display frame, then the selector on top of any remaining spill. */
    PenNormal();
    PenSize(1, 1);
    ForeColor(blackColor);
    FrameRect(&sDisplayRect);

    DrawBlockSelector();

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

/* Handle all events for the game window */
void GameWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
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
}

/* Perform idle processing: turn a recorded dirty rect into an InvalRect. */
void GameWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
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
