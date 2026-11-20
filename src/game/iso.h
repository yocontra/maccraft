#ifndef ISO_H
#define ISO_H

#include <Types.h>

#define kTileW 32
#define kTileH 16

void Iso_Project(short x, short y, short z, short camX, short camY,
                 short *screenX, short *screenY);
Boolean Iso_Unproject(short screenX, short screenY, short camX, short camY,
                      short *x, short *y);

/* Same inverse, but clamped to the world instead of reporting an outside point.
 * The renderer uses it to bound the visible tile range, so the clamping lives
 * with the projection rather than in a second copy of the maths. */
void Iso_UnprojectClamped(short screenX, short screenY, short camX, short camY,
                          short *x, short *y);

#endif
