#ifndef ISO_H
#define ISO_H

#include <Types.h>

#define kTileW 32
#define kTileH 16

void Iso_Project(short x, short y, short z, short camX, short camY,
                 short *screenX, short *screenY);
Boolean Iso_Unproject(short screenX, short screenY, short camX, short camY,
                      short *x, short *y);

#endif
