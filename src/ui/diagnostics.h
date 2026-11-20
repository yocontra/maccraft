#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

/*
 * Counters the harness reads out of the running application.
 *
 * They live with the code that increments them, but the receipt (remote.c) and
 * the self test (selftest.c) both need to read them; before this header each
 * consumer redeclared the arrays itself, so a rename or a size change compiled
 * cleanly and failed at link time -- or worse, read the wrong slots.
 *
 * Nothing in the game play path reads these.
 */

/* Per-idle accounting: hover ticks, hover calls, paint ticks, paint count,
 * paint start, physics steps. */
extern long Sprite_Work[6];

/* Pixels the world pass has covered in total, for mean repaint area. */
extern long Sprite_PaintedPixels;

/* Ticks in each phase of the last paint: setup, world pass, chrome. */
extern long Sprite_Phase[3];

/* Render benchmark results: sky, world, world-with-copies, blocks. */
extern long Sprite_Timing[4];

/* Set [0] non-zero to make the sprite blitter return without drawing. */
extern long Sprite_Probe[2];

#endif
