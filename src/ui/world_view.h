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

/* Drawn *inside* the traversal, once the columns up to its own diagonal have
 * been painted: that is what puts a player in a doorway behind the wall in
 * front of him and under a canopy under the leaves, instead of over every
 * block on screen. `tileX`/`tileY` are the tile the figure stands on. */
typedef void (*WorldView_OverlayProc)(void *context);

void WorldView_DrawWithOverlay(const WorldView *v, const Rect *clip,
                               short tileX, short tileY,
                               WorldView_OverlayProc overlay, void *context);

void WorldView_Draw(const WorldView *v, const Rect *clip);

#endif
