#ifndef PLAYER_H
#define PLAYER_H

#include <Types.h>

/*
 * Fixed-point player state for the isometric world.
 *
 * All positions and velocities are 16.16 fixed point (kPlayerOne == 1.0
 * block).  There is no floating point in the simulation: the 68k has no FPU,
 * and integer maths keeps Player_Step reproducible tick for tick.
 *
 * x, y, z is the centre of the player's feet.  The collision box is 0.6
 * blocks square and 1.8 blocks tall, so the box spans
 * [x - kPlayerHalfWidth, x + kPlayerHalfWidth] on x and y and
 * [z, z + kPlayerHeight] on z.
 */

#define kPlayerOne 0x00010000L /* 16.16 fixed point: one block */

/* 0.3 * 65536 == 19660.8, rounded up so the box is never narrower than the
 * fractional representation allows.  1.8 * 65536 == 117964.8, likewise. */
#define kPlayerHalfWidth 19661L
#define kPlayerHeight 117965L

typedef struct {
    long x, y, z;    /* centre of the player's feet, 16.16 */
    long vx, vy, vz; /* velocity in blocks per tick, 16.16 */
    Boolean onGround;
    short respawns; /* how many times the player fell out of the world */
} Player;

void Player_Spawn(Player *p, short x, short y, short z);
/* Advance one tick. moveX/moveY are -1, 0 or 1 (screen-relative grid intent),
   jump is true while the jump control is held. */
void Player_Step(Player *p, short moveX, short moveY, Boolean jump);
void Player_Restore(Player *p, const Player *saved); /* for tests */

#endif
