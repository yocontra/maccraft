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
 * which is why every cut edge of the island rendered as a black rim. */
#define kOccluderStep 8
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
