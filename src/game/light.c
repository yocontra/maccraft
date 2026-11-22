/*
 * light.c - integer-only lighting for the isometric renderer.
 *
 * The model has three parts: a constant sun contribution per face, ambient
 * occlusion sampled from the blocks around a face, and integer shading of a
 * colour.  Everything is deterministic and reads only the static world, so
 * the renderer can call these per face without touching QuickDraw state.
 */

#include "light.h"
#include "world.h"

/* Sun factors.  The top catches the full sun; the left face (normal +y,
   pointing down-left on screen) is lit next; the right face (normal +x,
   pointing down-right) turns away from the sun and is darkest. */
/* Minecraft's fixed directional shade, which is what makes its blocks read as
 * blocks: the top face gets full light, the two sides get 80% and 60%. The
 * left side is the brighter one because the sun sits up and to the left. */
#define kSunTop   256 /* 1.00 */
#define kSunLeft  205 /* 0.80 */
#define kSunRight 154 /* 0.60 */

/* Occlusion is quantised to four levels so faces in the same shadow band
   share a colour; beyond three occluders the face is as dark as it gets. */
/* Minecraft's smooth lighting dims a face by at most about a fifth: a corner
 * tucked against two other blocks is darker, never black. A 32-per-occluder
 * step took a side face from 0.6 to 0.3 once the sun factor was applied on top,
 * which is why every cut edge of the island rendered as a black rim.
 *
 * Nor is eight enough: the four shade steps sit at 231, 180 and 128, so eight
 * points per occluder could not move a face from one step to the next and the
 * whole occlusion pass was invisible -- no shadow where a wall meets the
 * ground, none under a canopy. Twenty crosses a step at two occluders and
 * lands a fully tucked corner two steps down, which is a fifth at worst. */
#define kOccluderStep 20
#define kMaxOccluders 3

static short SolidAt(short x, short y, short z)
{
    /* World_Get returns 0 outside the world, which is exactly the right
       answer here: nothing exists beyond the edge to cast a shadow. */
    return (short)(World_Get(x, y, z) != 0);
}

/* Count the blocks that sit in front of the given face.  The top face looks
   at the block directly above plus the four diagonals one level up; the two
   side faces look at the cell directly in front, one level up in front, and
   the two diagonals flanking that upper cell. */
static short CountOccluders(short x, short y, short z, short face)
{
    if (face == kLightFaceTop) {
        return SolidAt(x, y, z + 1)
             + SolidAt(x - 1, y, z + 1)
             + SolidAt(x + 1, y, z + 1)
             + SolidAt(x, y - 1, z + 1)
             + SolidAt(x, y + 1, z + 1);
    }

    if (face == kLightFaceLeft) {
        /* Left face: outward normal +y. */
        return SolidAt(x, y + 1, z)
             + SolidAt(x, y + 1, z + 1)
             + SolidAt(x - 1, y + 1, z + 1)
             + SolidAt(x + 1, y + 1, z + 1);
    }

    /* Right face: outward normal +x. */
    return SolidAt(x + 1, y, z)
         + SolidAt(x + 1, y, z + 1)
         + SolidAt(x + 1, y - 1, z + 1)
         + SolidAt(x + 1, y + 1, z + 1);
}

short Light_FaceFactor(short face)
{
    if (face == kLightFaceTop)
        return kSunTop;
    if (face == kLightFaceLeft)
        return kSunLeft;
    return kSunRight;
}

short Light_Occlusion(short x, short y, short z, short face)
{
    short count = CountOccluders(x, y, z, face);

    if (count > kMaxOccluders)
        count = kMaxOccluders;

    return (short)(kLightMax - count * kOccluderStep);
}

/* A face corner has three possible occluders.  Two of them box the corner in
   completely, one leaves it half lit, none leaves it open.  Counting the
   diagonal as an occluder as well is deliberate: a block tucked diagonally
   against the corner also blocks the light that would reach it, and this is
   the version of the rule that produces the continuous dark seam rather than
   four isolated dark spots. */
static short CornerValue(short solid1, short solid2, short solid3)
{
    short solid = solid1 + solid2 + solid3;

    if (solid >= 2)
        return 0;
    if (solid == 1)
        return 128;
    return kLightMax;
}

void Light_FaceCorners(short x, short y, short z, short face,
                       short out[kLightCorners])
{
    if (face == kLightFaceTop) {
        /* One level up, the two edge cells and the diagonal that meet each
           corner of the top diamond. */
        out[0] = CornerValue(SolidAt(x - 1, y, z + 1),
                             SolidAt(x, y - 1, z + 1),
                             SolidAt(x - 1, y - 1, z + 1));
        out[1] = CornerValue(SolidAt(x + 1, y, z + 1),
                             SolidAt(x, y - 1, z + 1),
                             SolidAt(x + 1, y - 1, z + 1));
        out[2] = CornerValue(SolidAt(x + 1, y, z + 1),
                             SolidAt(x, y + 1, z + 1),
                             SolidAt(x + 1, y + 1, z + 1));
        out[3] = CornerValue(SolidAt(x - 1, y, z + 1),
                             SolidAt(x, y + 1, z + 1),
                             SolidAt(x - 1, y + 1, z + 1));
        return;
    }

    if (face == kLightFaceLeft) {
        /* Outward normal +y: all occluders sit one row in front.  Each side
           corner takes the neighbour beside it at the face's own level, the
           neighbour above (or below) it in front, and the diagonal between
           them. */
        out[0] = CornerValue(SolidAt(x - 1, y + 1, z),
                             SolidAt(x, y + 1, z + 1),
                             SolidAt(x - 1, y + 1, z + 1));
        out[1] = CornerValue(SolidAt(x + 1, y + 1, z),
                             SolidAt(x, y + 1, z + 1),
                             SolidAt(x + 1, y + 1, z + 1));
        out[2] = CornerValue(SolidAt(x + 1, y + 1, z),
                             SolidAt(x, y + 1, z - 1),
                             SolidAt(x + 1, y + 1, z - 1));
        out[3] = CornerValue(SolidAt(x - 1, y + 1, z),
                             SolidAt(x, y + 1, z - 1),
                             SolidAt(x - 1, y + 1, z - 1));
        return;
    }

    /* Right face, outward normal +x; unknown faces share it, as they do for
       Light_FaceFactor. */
    out[0] = CornerValue(SolidAt(x + 1, y - 1, z),
                         SolidAt(x + 1, y, z + 1),
                         SolidAt(x + 1, y - 1, z + 1));
    out[1] = CornerValue(SolidAt(x + 1, y + 1, z),
                         SolidAt(x + 1, y, z + 1),
                         SolidAt(x + 1, y + 1, z + 1));
    out[2] = CornerValue(SolidAt(x + 1, y + 1, z),
                         SolidAt(x + 1, y, z - 1),
                         SolidAt(x + 1, y + 1, z - 1));
    out[3] = CornerValue(SolidAt(x + 1, y - 1, z),
                         SolidAt(x + 1, y, z - 1),
                         SolidAt(x + 1, y - 1, z - 1));
}

static short ClampCorner(short value)
{
    if (value < 0)
        return 0;
    if (value > kLightMax)
        return kLightMax;
    return value;
}

short Light_SampleCorners(const short corners[kLightCorners],
                          short u, short v)
{
    int left, right, top, bottom;
    int sum;

    /* The far corner is at 255 on each axis, not 256, so the sample is exact
       at all four corners; intermediate pixels are then at most half a
       percent off a perfect bilinear position, which is invisible and keeps
       the arithmetic small enough for a 68000. */
    if (u < 0)
        u = 0;
    if (u > 255)
        u = 255;
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;

    left = 255 - u;
    right = u;
    top = 255 - v;
    bottom = v;

    /* Bilinear interpolation.  Each weight is at most 255*255 = 65025 and
       each corner at most 256, so the sum stays well inside 32 bits.  The
       32512 is half of that denominator, so the division rounds to nearest. */
    sum = ClampCorner(corners[0]) * left * top
        + ClampCorner(corners[1]) * right * top
        + ClampCorner(corners[2]) * right * bottom
        + ClampCorner(corners[3]) * left * bottom;

    return (short)((sum + 32512) / 65025);
}

LightColor Light_Shade(LightColor in, short factor)
{
    LightColor out;

    if (factor < 0)
        factor = 0;
    if (factor > kLightMax)
        factor = kLightMax;

    /* Components are 16-bit, so the product fits comfortably in an int.
       Adding half the divisor before dividing rounds to the nearest step;
       at factor 256 the added 128 is discarded, leaving the input exact. */
    out.red = (unsigned short)(((int)in.red * factor + 128) / kLightMax);
    out.green = (unsigned short)(((int)in.green * factor + 128) / kLightMax);
    out.blue = (unsigned short)(((int)in.blue * factor + 128) / kLightMax);
    return out;
}

short Light_Combine(short faceFactor, short occlusion)
{
    int combined;

    if (faceFactor < 0)
        faceFactor = 0;
    if (faceFactor > kLightMax)
        faceFactor = kLightMax;
    if (occlusion < 0)
        occlusion = 0;
    if (occlusion > kLightMax)
        occlusion = kLightMax;

    /* 256 * 256 / 256 = 256 exactly, so two full factors stay full. */
    combined = ((int)faceFactor * occlusion + 128) / kLightMax;
    if (combined > kLightMax)
        combined = kLightMax;
    return (short)combined;
}
