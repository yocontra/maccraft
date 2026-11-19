/* avatar.h - the player's figure. */

#ifndef AVATAR_H
#define AVATAR_H

#include <Quickdraw.h>

/* Draw the figure standing on block (px,py,pz) in the given play area. */
void Avatar_Draw(short px, short py, short pz, short camX, short camY, const Rect *display);

#endif
