/* avatar.h - the player's figure. */

#ifndef AVATAR_H
#define AVATAR_H

#include <Quickdraw.h>

/* Draw the figure standing on block (px,py,pz) in the given play area.
 *
 * `phase` is a walking-cycle position 0..7 and `airborne` says whether the feet
 * are off the ground; the figure's legs swing with the phase and tuck up when it
 * is in the air. Both are derived by the caller from the simulation, so this
 * file stays a pure drawing routine. */
void Avatar_Draw(short px, short py, short pz, short camX, short camY,
                 const Rect *display, short phase, Boolean airborne);

#endif
