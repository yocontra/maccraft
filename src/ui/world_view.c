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
long WorldView_Stats[2];   /* columns walked, blocks drawn */

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


/*
 * The polygon handle is allocated once and refilled per face.
 *
 * This used to OpenPoly/KillPoly every face, which allocates and frees a block
 * per face on the 1-bit path -- thousands of allocations a frame. A PolyHandle
 * is just a count word followed by points, so writing them directly is both
 * cheaper and allocation-free.
 */
static PolyHandle sQuadPoly = NULL;

static PolyHandle QuadPoly(short x0, short y0, short x1, short y1, short x2, short y2,
                           short x3, short y3)
{
    Point *pts;

    if (sQuadPoly == NULL) {
        sQuadPoly = OpenPoly();
        if (sQuadPoly == NULL) {
            return NULL;
        }
        ClosePoly();          /* size the handle for four points */
    }

    /* (*poly) is { short polySize; Point polyPoints[]; }. */
    pts = (*sQuadPoly)->polyPoints;
    pts[0].h = x0;  pts[0].v = y0;
    pts[1].h = x1;  pts[1].v = y1;
    pts[2].h = x2;  pts[2].v = y2;
    pts[3].h = x3;  pts[3].v = y3;
    (*sQuadPoly)->polySize = 4;

    return sQuadPoly;
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


/* The inverse projection lives in iso.c: this used to be a second copy of it,
 * which had already drifted (it clamped silently, Iso_Unproject reports). */
static void UnprojectClamped(const WorldView *v, short px, short py, short *outX, short *outY)
{
    Iso_UnprojectClamped(px, py, v->camX, v->camY, outX, outY);
}


/*
 * The tiles that can appear in a view, computed from the projection rather than
 * from clamped corner unprojections.
 *
 *   screenX  = (x - y) * 16 + camX   ->  x - y = (screenX - camX) / 16
 *   screenY  = (x + y) *  8 + camY   ->  x + y = (screenY - camY) /  8
 *
 * So the view rectangle bounds the diagonal term from its left and right edges
 * and the sum from its top and bottom, exactly. A column up to kWorldZ levels
 * tall draws its top that many tile-sums above its ground, which is what the
 * sum lower bound allows for. Clamping to the world happens per tile in the
 * traversal, not here: clamping first is what turned the diagonal bounds into
 * +-world and made the walk six times the visible tiles.
 */
static void ComputeVisibleRange(const WorldView *v, const Rect *view, short *xMin,
                                short *yMin, short *xMax, short *yMax,
                                short *sumMin, short *sumMax,
                                short *diffMin, short *diffMax)
{
    short left   = (short)(view->left - v->display.left);
    short top    = (short)(view->top - v->display.top);
    short right  = (short)(view->right - v->display.left);
    short bottom = (short)(view->bottom - v->display.top);

    /* A column kWorldZ tall draws its top kWorldZ * kTileH pixels above its
     * ground, which is 2 * kWorldZ tile sums now that a level is a whole
     * tile height. */
    *sumMin  = (short)(((int)top - (int)v->camY) / (kTileH / 2) - 2 * kWorldZ - 2);
    *sumMax  = (short)(((int)bottom - (int)v->camY) / (kTileH / 2) + 2);
    *diffMin = (short)(((int)left - (int)v->camX) / (kTileW / 2) - 2);
    *diffMax = (short)(((int)right - (int)v->camX) / (kTileW / 2) + 2);

    *xMin = 0;
    *yMin = 0;
    *xMax = (short)(kWorldX - 1);
    *yMax = (short)(kWorldY - 1);
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
    /* One sun, one direction: the face's sun factor, dimmed a little where
     * blocks crowd it. The multiplier is deliberately gentle (8 points per
     * occluder out of 256) so the sun still decides which way the light comes
     * from -- the earlier per-corner term dimmed a face by up to a fifth and
     * made the shading depend on its neighbours instead. */
    if (showTop) {
        topShade = Light_Combine(Light_FaceFactor(kLightFaceTop),
                                 Light_Occlusion(x, y, z, kLightFaceTop));
    }
    if (showLeft) {
        leftShade = Light_Combine(Light_FaceFactor(kLightFaceLeft),
                                  Light_Occlusion(x, y, z, kLightFaceLeft));
    }
    if (showRight) {
        rightShade = Light_Combine(Light_FaceFactor(kLightFaceRight),
                                   Light_Occlusion(x, y, z, kLightFaceRight));
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
            short step = Sprite_ShadeStep(topShade);

            /* Water shimmers: alternating between two adjacent shade bands a few
             * times a second reads as moving water without a second bake or a
             * second mask. Everything else is static. */
            if (id == kBlockWater) {
                short phase = (short)(((unsigned long)TickCount() >> 3) & 1);

                step = (short)(step + phase);
                if (step >= 4) {
                    step = 3;
                }
            }
            Sprite_Draw((short)(sx - kTileW / 2), (short)(sy - kTileH / 2), id,
                        kBlockFaceTop, step);
        }
        return;
    }

    /* Top face: diamond centred at (sx, sy), kTileW x kTileH. */
    if (showTop) {
        PaintQuad(v, sx, sy - kTileH / 2, sx + kTileW / 2, sy, sx, sy + kTileH / 2,
                  sx - kTileW / 2, sy, id, kBlockFaceTop, topShade, topVariation);
    }

    /* Left face. A cube face is 16 rows deep, the same as the baked sprite and
     * what pick.c's SideContains assumes; these quads were half height, so on a
     * non-32-bit display every block drew as a squat slab and a click up to 15 px
     * below a side's top edge resolved to a face nothing had drawn. */
    if (showLeft) {
        PaintQuad(v, sx - kTileW / 2, sy, sx, sy + kTileH / 2,
                  sx, sy + kTileH + kTileH / 2,
                  sx - kTileW / 2, sy + kTileH,
                  id, kBlockFaceLeft, leftShade, topVariation);
    }

    /* Right face. */
    if (showRight) {
        PaintQuad(v, sx, sy + kTileH / 2, sx + kTileW / 2, sy,
                  sx + kTileW / 2, sy + kTileH,
                  sx, sy + kTileH + kTileH / 2,
                  id, kBlockFaceRight, rightShade, topVariation);
    }

    /* No outline. A block is separated from its neighbours by the shade step
     * between the top face and the two sides plus the contact shadow, exactly
     * as in the reference art; a 1 px black grid over every face is what made
     * the world look like a spreadsheet. */
}


void WorldView_Draw(const WorldView *v, const Rect *clip)
{
    WorldView_DrawWithOverlay(v, clip, 0, 0, NULL, NULL);
}

void WorldView_DrawWithOverlay(const WorldView *v, const Rect *clip,
                               short tileX, short tileY,
                               WorldView_OverlayProc overlay, void *context)
{
    short xMin, yMin, xMax, yMax, sum, x, y, sumMax, z, top;
    short sumMin, diffMin, diffMax;
    Rect box, hit;

    (void)xMin; (void)yMin; (void)xMax; (void)yMax;
    ComputeVisibleRange(v, clip, &xMin, &yMin, &xMax, &yMax,
                        &sumMin, &sumMax, &diffMin, &diffMax);

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
    WorldView_Stats[0] = 0;
    WorldView_Stats[1] = 0;

    /* The overlay belongs after everything at or behind its own diagonal, so
     * the first sum past it is where it goes. If the walk starts beyond the
     * player (the view scrolled past him) it is still drawn, in front. */
    short overlaySum = (short)(tileX + tileY);
    Boolean overlayDone = (Boolean)(overlay == NULL);

    for (sum = sumMin; sum <= sumMax; sum++) {
        if (!overlayDone && sum > overlaySum) {
            overlay(context);
            overlayDone = true;
        }

        /* x - y = 2x - sum, so the diagonal bounds give the on-screen slice of
         * this row; one tile of slack covers the rounding, and the per-column
         * screen test still culls what is left. */
        short lo = (short)((sum + diffMin) / 2 - 1);
        short hi = (short)((sum + diffMax) / 2 + 1);

        if (lo < 0) {
            lo = 0;
        }
        if (hi > (short)(kWorldX - 1)) {
            hi = (short)(kWorldX - 1);
        }

        for (x = lo; x <= hi; x++) {
            short sx, sy0;

            WorldView_Stats[0]++;

            y = (short)(sum - x);
            if (y < 0 || y >= kWorldY) {
                continue;
            }

            /* Projection hoisted out of the block loop: one multiply pair per
             * column, and each level is one tile height up. A block's side face
             * is kTileH tall (the baked quad is 16 rows), so the step between
             * levels must be kTileH as well -- at kTileH/2 every block overlapped
             * the one under it and a column read as a stack of slabs. */
            sx  = (short)(((x - y) * (kTileW / 2)) + v->camX + v->display.left);
            sy0 = (short)(((x + y) * (kTileH / 2)) + v->camY + v->display.top);

            /* Conservative box first (as if the column were at full height),
             * so the columns off screen are rejected before World_TopZ is paid
             * for. It is a superset of the exact box below, so it can never
             * reject a column that would have drawn. */
            SetRect(&box, (short)(sx - kTileW / 2 - 1),
                    (short)(sy0 - ((kWorldZ - 1) * kTileH) - (kTileH / 2) - 1),
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
                    (short)(sy0 - (top * kTileH) - (kTileH / 2) - 1),
                    (short)(sx + kTileW / 2 + 1), (short)(sy0 + kTileH + 1));
            if (!SectRect(&box, clip, &hit)) {
                continue;
            }

            for (z = 0; z <= top; z++) {
                BlockID id = World_Get(x, y, z);

                if (id != 0) {
                    DrawBlock(v, x, y, z, id, sx, (short)(sy0 - (z * kTileH)));
                }
            }
        }
    }

    if (!overlayDone) {
        overlay(context);
    }
}
