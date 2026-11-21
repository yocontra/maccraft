#ifndef PARTICLES_H
#define PARTICLES_H

#include <Quickdraw.h>
#include <Types.h>

#include "../game/blocks.h"

/*
 * Block-break particles: a handful of coloured specks that fly out of a block
 * when it comes apart, fall, and fade.
 *
 * Mining a block used to give no visual confirmation at all beyond it vanishing.
 * The budget is fixed -- a small static table, no allocation -- because this
 * runs on the same 8 MHz CPU as everything else.
 */

/* Break `id` at tile (x, y, z): coloured by the block's top face, thrown upward
 * and outward, then falling. */
void Particles_Spawn(short x, short y, short z, BlockID id);

/* Advance one tick. Returns true while any particle is alive. */
Boolean Particles_Update(void);

/* Union of where the live particles are on screen, in window coordinates, so
 * the caller can damage exactly that. Empty rect when nothing is alive. */
void Particles_Bounds(const Rect *display, short camX, short camY, Rect *out);

/* Draw inside the pixel frame, with the sprite view set to the play area. */
void Particles_Draw(const Rect *display, short camX, short camY);

/* Drop everything (world reset). */
void Particles_Clear(void);

#endif
