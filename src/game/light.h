#ifndef LIGHT_H
#define LIGHT_H

#include <Types.h>

/*
 * light.h - integer-only lighting model for the isometric renderer.
 *
 * A sun hangs above and to the upper left of the world.  Each visible face
 * gets two 0..256 factors: a fixed sun contribution (top brightest, left
 * face next, right face darkest) and an ambient-occlusion factor derived
 * from the real neighbouring blocks.  Multiplying the two gives the final
 * shade, which is applied to a face colour with Light_Shade.
 *
 * There is no float anywhere: every operation is integer, rounded to the
 * nearest step, so results are identical on every machine.
 *
 * The face numbers match the kBlockFace* enum in blocks.h (0/1/2)
 * but are declared here under their own names so this header stays
 * independent of the QuickDraw-dependent texture module.  A caller may pass
 * either set of constants.
 */

typedef struct {
    unsigned short red;
    unsigned short green;
    unsigned short blue;
} LightColor;

enum { kLightFaceTop = 0, kLightFaceLeft = 1, kLightFaceRight = 2 };
enum { kLightMax = 256 };

/* Four samples per face, one per corner.  Their order is the direction of
   increasing u then v in Light_SampleCorners: out[0] is the corner the face
   starts at, then u increases to out[1], v increases to out[3], and out[2]
   is diagonally opposite out[0].  On screen that is top, right, bottom,
   left for the top diamond; top-left, top-right, bottom-right, bottom-left
   for either side face. */
#define kLightCorners 4

/* Sun contribution for a face: 0 top, 1 left, 2 right.  Returns 0..256,
   strictly ordered top > left > right.  Unknown faces read as the darkest
   face so an unclassified face can never gain light. */
short Light_FaceFactor(short face);

/* Ambient occlusion for a face, 0..256.  Solid neighbours in front of the
   face darken it in fixed 32-point steps (256, 224, 192, 160).  world.h
   keeps a single static world, so there is no world-context parameter: the
   neighbours are read through World_Get, exactly as the rest of the game
   reads blocks. */
short Light_Occlusion(short x, short y, short z, short face);

/* Per-corner ambient occlusion for one face of block (x, y, z), each value
   0..256, in the kLightCorners order documented above.

   A face corner is lit by the three cells that touch it *around* the face
   plane; the cell directly in front of the face is excluded, because a block
   there would hide the face entirely instead of shading its edge.  Two or
   more solid neighbours box the corner in completely (0), one neighbour
   leaves it half lit (128), and an open corner keeps full light (256).  That
   is Minecraft's smooth-lighting rule, and it is what creates the dark seams
   where a wall meets a floor.

   The exact neighbour sets, with dx/dy/dz relative to the face's block:

     top face, one level up (z+1)
       out[0] top:    (x-1,y) (x,y-1) (x-1,y-1)
       out[1] right:  (x+1,y) (x,y-1) (x+1,y-1)
       out[2] bottom: (x+1,y) (x,y+1) (x+1,y+1)
       out[3] left:   (x-1,y) (x,y+1) (x-1,y+1)

     left face (outward normal +y), all at y+1
       out[0] top-left:     (x-1,z)   (x,z+1)   (x-1,z+1)
       out[1] top-right:    (x+1,z)   (x,z+1)   (x+1,z+1)
       out[2] bottom-right: (x+1,z)   (x,z-1)   (x+1,z-1)
       out[3] bottom-left:  (x-1,z)   (x,z-1)   (x-1,z-1)

     right face (outward normal +x), all at x+1
       out[0] top-left:     (y-1,z)   (y,z+1)   (y-1,z+1)
       out[1] top-right:    (y+1,z)   (y,z+1)   (y+1,z+1)
       out[2] bottom-right: (y+1,z)   (y,z-1)   (y+1,z-1)
       out[3] bottom-left:  (y-1,z)   (y,z-1)   (y-1,z-1)

   Reads outside the world count as empty, so an edge block stays lit.  An
   unknown face is treated as the right face, matching Light_FaceFactor. */
void Light_FaceCorners(short x, short y, short z, short face,
                       short out[kLightCorners]);

/* Bilinear sample of the four corner values at (u, v), both 0..255 and
   clamped.  (0,0) is out[0], (255,0) out[1], (255,255) out[2], (0,255)
   out[3]; the middle is approximately the average of the corners.  Integer
   weights only.
   The result is 0..256, exact at the four corners, and monotone: lowering a
   corner (or sampling closer to a darker corner) can never brighten a
   sample.  This is the value the renderer dithers down to the palette. */
short Light_SampleCorners(const short corners[kLightCorners],
                          short u, short v);

/* Scale a colour by factor/256 with rounding, clamped to 0xFFFF per
   component.  factor is clamped to 0..256 first, so 256 returns the input
   unchanged and 0 returns black. */
LightColor Light_Shade(LightColor in, short factor);

/* Multiply two 0..256 factors into one, rounded and clamped to 0..256. */
short Light_Combine(short faceFactor, short occlusion);

#endif /* LIGHT_H */
