/* world_view.h - the world pass: what to draw, in what order, with what light.
 *
 * It takes the camera and the play area as parameters rather than reading the
 * window module's state, which is what let it leave game_window.c at all: the
 * renderer no longer has to know how a window is created or where the HUD sits.
 */

#ifndef WORLD_VIEW_H
#define WORLD_VIEW_H

#include <Quickdraw.h>

#include "../game/world.h"

typedef struct {
    Rect display;       /* play area, in the window's local coordinates */
    short camX, camY;   /* camera offsets, as Iso_Project consumes them */
    Boolean hasColor;   /* colour path draws baked sprites; mono draws polygons */
} WorldView;

/* Draw every block whose tiles overlap `clip` (in window coordinates), back to
 * front. The caller owns the screen lock and the sprite view. */
extern long WorldView_Stats[2];   /* columns walked, blocks drawn */

void WorldView_Draw(const WorldView *v, const Rect *clip);

#endif
