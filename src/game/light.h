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
 * The face numbers match texture.h's kFaceTop/kFaceLeft/kFaceRight (0/1/2)
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

/* Scale a colour by factor/256 with rounding, clamped to 0xFFFF per
   component.  factor is clamped to 0..256 first, so 256 returns the input
   unchanged and 0 returns black. */
LightColor Light_Shade(LightColor in, short factor);

/* Multiply two 0..256 factors into one, rounded and clamped to 0..256. */
short Light_Combine(short faceFactor, short occlusion);

#endif /* LIGHT_H */
