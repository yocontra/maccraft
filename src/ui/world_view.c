/* world_view.c - the world pass.
 *
 * Split out of game_window.c: traversal, face culling, lighting lookup, the
 * baked-sprite path and the QuickDraw polygon fallback for one-bit screens. The
 * camera and the play area arrive as a WorldView, so this file needs nothing
 * from the window module.
 */

#include <Quickdraw.h>
#include <Windows.h>

#include "world_view.h"
#include "sprite_sheet.h"
#include "../game/blocks.h"
#include "../game/iso.h"
#include "../game/light.h"
#include "../game/world.h"



/* The palette module's colour into the QuickDraw one. */
static RGBColor FromBlockColor(BlockColor c)
{
    RGBColor result;

    result.red   = c.red;
    result.green = c.green;
    result.blue  = c.blue;
    return result;
}

static RGBColor Lightened(const RGBColor *base)
{
    RGBColor result;
    unsigned short bump = 0x1A1A;

    result.red   = (unsigned short)(base->red   > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->red + bump);
    result.green = (unsigned short)(base->green > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->green + bump);
    result.blue  = (unsigned short)(base->blue  > (unsigned short)(0xFFFF - bump) ? 0xFFFF : base->blue + bump);
    return result;
}


static RGBColor FaceBase(const WorldView *v, BlockID id, short face, short variation)
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


static void PaintQuad(const WorldView *v, short x0, short y0, short x1, short y1, short x2,
                      short y2, short x3, short y3, BlockID id, short face, short shade,
                      short variation)
{
    PolyHandle poly;

    poly = QuadPoly(x0, y0, x1, y1, x2, y2, x3, y3);
    if (poly == NULL) {
        return;
    }

    if (v->hasColor) {
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
        RGBColor raw = FaceBase(v, id, face, variation);
        RGBColor base = Shaded(&raw, shade);
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


static short DarkestCorner(const short corners[kLightCorners])
{
    short darkest = 256;
    short i;

    for (i = 0; i < kLightCorners; i++) {
        if (corners[i] < darkest) {
            darkest = corners[i];
        }
    }
    return darkest;
}


static short FloorDivInt(int a, int b)
{
    int q = a / b;

    if ((a % b) != 0 && ((a < 0) != (b < 0))) {
        q--;
    }
    return (short)q;
}


static void UnprojectClamped(const WorldView *v, short px, short py, short *outX, short *outY)
{
    short sx, sy, x, y;

    sx = FloorDivInt((int)px - (int)v->camX, kTileW / 2);
    sy = FloorDivInt((int)py - (int)v->camY, kTileH / 2);

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


static void ComputeVisibleRange(const WorldView *v, const Rect *view, short *xMin,
                                short *yMin, short *xMax, short *yMax)
{
    short left, top, right, bottom, x, y;

    left   = (short)(view->left - v->display.left);
    top    = (short)(view->top - v->display.top);
    right  = (short)(view->right - v->display.left);
    bottom = (short)(view->bottom - v->display.top);

    UnprojectClamped(v, left, top, xMin, yMin);
    *xMax = *xMin;
    *yMax = *yMin;

    UnprojectClamped(v, right, top, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(v, left, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(v, right, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    /* Far side: a column one tile past the clip corner can still have its top
     * diamond poke into the clip, so one tile of slack (two for safety). Near
     * side: a column up to kWorldZ levels tall draws its top up to
     * kWorldZ * kTileH/2 pixels above its own ground, which is kWorldZ more
     * tile-sums, so the near edges need that much more room. The screen-box
     * test in the traversal makes an over-wide range harmless (it costs an
     * iteration, not a face), while an under-wide one would clip geometry. */
    *xMin -= 2;
    *yMin -= 2;
    *xMax += kWorldZ + 2;
    *yMax += kWorldZ + 2;

    if (*xMin < 0) *xMin = 0;
    if (*yMin < 0) *yMin = 0;
    if (*xMax >= kWorldX) *xMax = kWorldX - 1;
    if (*yMax >= kWorldY) *yMax = kWorldY - 1;
}


static void DrawBlock(const WorldView *v, short x, short y, short z, BlockID id, short sx,
                      short sy)
{
    short topShade   = 256;
    short leftShade  = 256;
    short rightShade = 256;
    short topVariation = 0;

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

    /* A block with every face buried needs no lighting and no drawing. Most of
     * a filled world is like this, and occlusion is five neighbour lookups per
     * face. */
    if (!showTop && !showLeft && !showRight) {
        return;
    }

    topVariation = Block_Variation(x, y, z);
    /* Occlusion comes from the four corners of the face, not one number for the
     * whole quad: a face with a boxed corner is darker than an open one, which
     * is what gives Minecraft its seams where blocks meet. */
    if (showTop) {
        short corners[kLightCorners];

        Light_FaceCorners(x, y, z, kLightFaceTop, corners);
        topShade = Light_Combine(Light_FaceFactor(kLightFaceTop), DarkestCorner(corners));
    }
    if (showLeft) {
        short corners[kLightCorners];

        Light_FaceCorners(x, y, z, kLightFaceLeft, corners);
        leftShade = Light_Combine(Light_FaceFactor(kLightFaceLeft), DarkestCorner(corners));
    }
    if (showRight) {
        short corners[kLightCorners];

        Light_FaceCorners(x, y, z, kLightFaceRight, corners);
        rightShade = Light_Combine(Light_FaceFactor(kLightFaceRight), DarkestCorner(corners));
    }

    if (Sprite_Ready()) {
        /* Textured path: three baked, pre-shaded faces per block. The sprite
         * layer takes local coordinates and trims to the play area itself. */
        /* Sides before the top: the diamond's lower vertex meets the sides'
         * upper edges, and painting the top last covers the one-pixel seam that
         * otherwise shows as a dark speck at every tile junction. */
        if (showLeft) {
            Sprite_Draw((short)(sx - kTileW / 2), sy, id, kBlockFaceLeft,
                        Sprite_ShadeStep(leftShade));
        }
        if (showRight) {
            Sprite_Draw(sx, sy, id, kBlockFaceRight, Sprite_ShadeStep(rightShade));
        }
        if (showTop) {
            Sprite_Draw((short)(sx - kTileW / 2), (short)(sy - kTileH / 2), id,
                        kBlockFaceTop, Sprite_ShadeStep(topShade));
        }
        return;
    }

    /* Top face: diamond centred at (sx, sy), kTileW x kTileH. */
    if (showTop) {
        PaintQuad(v, sx, sy - kTileH / 2, sx + kTileW / 2, sy, sx, sy + kTileH / 2,
                  sx - kTileW / 2, sy, id, kBlockFaceTop, topShade, topVariation);
    }

    /* Left face. */
    if (showLeft) {
        PaintQuad(v, sx - kTileW / 2, sy, sx, sy + kTileH / 2, sx, sy + kTileH,
                  sx - kTileW / 2, sy + kTileH / 2, id, kBlockFaceLeft, leftShade, topVariation);
    }

    /* Right face. */
    if (showRight) {
        PaintQuad(v, sx, sy + kTileH / 2, sx + kTileW / 2, sy, sx + kTileW / 2, sy + kTileH / 2,
                  sx, sy + kTileH, id, kBlockFaceRight, rightShade, topVariation);
    }

    /* No outline. A block is separated from its neighbours by the shade step
     * between the top face and the two sides plus the contact shadow, exactly
     * as in the reference art; a 1 px black grid over every face is what made
     * the world look like a spreadsheet. */
}


void WorldView_Draw(const WorldView *v, const Rect *clip)
{
    short xMin, yMin, xMax, yMax, sum, x, y, sumMax, z, top;
    Rect box, hit;

    ComputeVisibleRange(v, clip, &xMin, &yMin, &xMax, &yMax);

    sumMax = (short)(xMax + yMax);

    /* Painter order: far columns first, bottom to top within a column. Walking a
     * column at a time costs one TopZ lookup per column and nothing at all for
     * the empty ones outside the island; the old shape walked all 48x48x8 cells
     * with a bounds-checked World_Get for each.
     *
     * The tile range above is a bounding box in tile space, and a 45-degree
     * field seen through a rectangular clip makes that box much larger than the
     * screen: with a 48x48 field a full frame can put all 2304 columns in the
     * range while only about a third of them are on screen. Each of those
     * columns used to pay for its lighting and a sprite call that trimmed to
     * nothing. The column's own screen box is the exact test, so it runs before
     * any lighting or blitting and rejects them for the cost of one SectRect. */
    for (sum = (short)(xMin + yMin); sum <= sumMax; sum++) {
        for (x = xMin; x <= xMax; x++) {
            short sx, sy0;

            y = (short)(sum - x);
            if (y < yMin || y > yMax) {
                continue;
            }

            /* Projection hoisted out of the block loop: one multiply pair per
             * column, and each level is one subtract from sy0. */
            sx  = (short)(((x - y) * (kTileW / 2)) + v->camX + v->display.left);
            sy0 = (short)(((x + y) * (kTileH / 2)) + v->camY + v->display.top);

            /* Conservative box first (as if the column were at full height),
             * so the columns off screen are rejected before World_TopZ is paid
             * for. It is a superset of the exact box below, so it can never
             * reject a column that would have drawn. */
            SetRect(&box, (short)(sx - kTileW / 2 - 1),
                    (short)(sy0 - ((kWorldZ - 1) * (kTileH / 2)) - (kTileH / 2) - 1),
                    (short)(sx + kTileW / 2 + 1), (short)(sy0 + kTileH + 1));
            if (!SectRect(&box, clip, &hit)) {
                continue;
            }

            top = World_TopZ(x, y);
            if (top < 0) {
                continue;
            }

            /* Exact box now that the height is known: the top diamond of the
             * highest block (kTileH/2 above its centre) down to the bottom
             * vertex of the lowest block's side (kTileH below its centre). */
            SetRect(&box, (short)(sx - kTileW / 2 - 1),
                    (short)(sy0 - (top * (kTileH / 2)) - (kTileH / 2) - 1),
                    (short)(sx + kTileW / 2 + 1), (short)(sy0 + kTileH + 1));
            if (!SectRect(&box, clip, &hit)) {
                continue;
            }

            for (z = 0; z <= top; z++) {
                BlockID id = World_Get(x, y, z);

                if (id != 0) {
                    DrawBlock(v, x, y, z, id, sx, (short)(sy0 - (z * (kTileH / 2))));
                }
            }
        }
    }
}
