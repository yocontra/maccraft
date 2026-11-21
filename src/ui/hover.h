#ifndef HOVER_H
#define HOVER_H

#include <Quickdraw.h>
#include <Types.h>

#include "../game/blocks.h"

/*
 * The block under the pointer, and the wireframe that marks it.
 *
 * Everything about the hover lives here so there is exactly one place that
 * decides which block is pointed at and exactly one that draws it. The caller
 * supplies the pieces it owns -- the pointer in window coordinates, the play
 * area rectangle, the camera -- and gets back a rectangle to damage when the
 * answer changed.
 */

/* Forget the current tile: the world or the camera moved under a stationary
 * pointer and the old answer is stale. */
void Hover_Reset(void);

/*
 * Resolve the pointer to a block. Returns true when the answer changed, and
 * fills `dirty` with the union of the box that was marked and the box that is
 * marked now, in window coordinates, ready for MarkDirty. The block level is
 * the one whose top face is pointed at, so the wireframe sits on the surface.
 */
Boolean Hover_Update(Point local, const Rect *display, short camX, short camY,
                     Rect *dirty);

Boolean Hover_Valid(void);

/* The block under the pointer, or 0 when there is none (an empty column). */
BlockID Hover_Block(void);

/* Which face of the block the pointer is on: kPickFaceTop/Left/Right. */
short Hover_Face(void);

/* The marked column and level. Only meaningful when Hover_Valid(). */
void Hover_Tile(short *x, short *y, short *z);

/*
 * Draw the wireframe. Call inside the pixel frame, with the sprite view set to
 * the play area; coordinates go through the same projection the world uses, so
 * the ring lands on the block it belongs to.
 */
void Hover_Draw(const Rect *display, short camX, short camY);


#endif
