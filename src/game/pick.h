#ifndef PICK_H
#define PICK_H

#include <Types.h>

/*
 * Resolve a click in the play area to the visible block surface under it.
 *
 * screenX/screenY are relative to the play area's top-left corner, i.e. the
 * caller has already subtracted its display rect origin.  camX/camY are the
 * camera offsets used to render that area.
 *
 * On success the outX and outY pointers identify the column, and outZ is the
 * surface level (World_TopZ + 1): the level a new block would occupy.
 * Returns false and leaves the outputs untouched when no column surface lies
 * under the point.
 */
Boolean Pick_Tile(short screenX, short screenY, short camX, short camY,
                  short *outX, short *outY, short *outZ);

#endif
