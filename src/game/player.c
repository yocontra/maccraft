/*
 * player.c - fixed-point platformer physics for the isometric world.
 *
 * No floating point, no heap, no Toolbox: this file links into the host
 * tests and the game alike.  The player is a single box that is moved one
 * axis at a time (z, then x, then y) so that landing on a surface and
 * walking into a wall both resolve the same way every tick.
 *
 * The only mutable module state is the respawn point recorded by
 * Player_Spawn.  The game has a single player, so a single point is enough;
 * Player_Restore never changes it.
 */

#include "blocks.h"
#include "player.h"
#include "world.h"

/* 0.08 * 65536 == 5242.88, rounded up. */
#define kPlayerGravity 5243L
/* 0.42 * 65536 == 27525.12.  Peaks ~1.32 blocks up, so it clears a one-block
 * step but not a two-block one. */
#define kPlayerJumpSpeed 27525L
/* 0.10 * 65536 == 6553.6, rounded up; ten ticks travel 1.00006 blocks. */
#define kPlayerWalkSpeed 6554L
/* Jump feel: how long a jump request is remembered, and how long the ground
 * counts as still there after walking off it. Four ticks is about 65 ms, which
 * is roughly the error a player makes at 60 Hz. */
#define kJumpBufferTicks 5
#define kCoyoteTicks 4

/* In water: a slow sink, and a paddle that lifts the player a little. */
#define kPlayerWaterGravity 1311L   /* 0.02 blocks per tick */
#define kPlayerSwimSpeed 6554L      /* 0.10 blocks per tick */

/* Terminal velocity: 3.0 blocks per tick. */
#define kPlayerMaxFall 196608L
/* Largest movement allowed between collision tests, 0.25 blocks.  At the
 * terminal velocity this is twelve tests per tick, which cannot skip a
 * one-block column. */
#define kPlayerSubStep 16384L

static long sSpawnX;
static long sSpawnY;
static long sSpawnZ;

/* Round-down division for fixed point.  C's '/' truncates toward zero, which
 * would make a cell index for a negative coordinate one too high. */
static long FloorDiv(long a, long b)
{
    long q = a / b;

    if ((a % b) != 0 && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* First block index touched by a coordinate (inclusive lower edge). */
static long CellLo(long v)
{
    return FloorDiv(v, kPlayerOne);
}

/* Last block index touched by a coordinate (inclusive upper edge).  The
 * upper edge itself is open, so a box ending exactly on a block boundary
 * does not touch the block beyond it. */
static long CellHi(long v)
{
    return FloorDiv(v - 1, kPlayerOne);
}

static Boolean CellSolid(long bx, long by, long bz)
{
    BlockID id;

    /* Check the grid before calling World_Get so that no access ever leaves
     * the array, even for a player that has fallen far below the world. */
    if (bx < 0 || bx >= kWorldX || by < 0 || by >= kWorldY ||
        bz < 0 || bz >= kWorldZ)
        return false;
    id = World_Get((short)bx, (short)by, (short)bz);
    /* Water is not solid: you wade and swim in it rather than standing on the
     * surface, which is what the pond used to be. See FloatInWater. */
    if (id == kBlockWater)
        return false;
    return (Boolean)(id != 0);
}

/* True when the body is in water: the cell at the feet or the one above. */
static Boolean InWater(long x, long y, long z)
{
    long bx0 = CellLo(x - kPlayerHalfWidth);
    long bx1 = CellHi(x + kPlayerHalfWidth);
    long by0 = CellLo(y - kPlayerHalfWidth);
    long by1 = CellHi(y + kPlayerHalfWidth);
    long bx, by;

    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            long bz;

            for (bz = CellLo(z); bz <= CellHi(z + kPlayerHeight); bz++) {
                if (bz < 0 || bz >= kWorldZ || bx < 0 || bx >= kWorldX ||
                    by < 0 || by >= kWorldY)
                    continue;
                if (World_Get((short)bx, (short)by, (short)bz) == kBlockWater)
                    return true;
            }
        }
    }
    return false;
}

/* Does the box centred at (x,y) with feet at z overlap any solid block? */
static Boolean BoxHits(long x, long y, long z)
{
    long bx0 = CellLo(x - kPlayerHalfWidth);
    long bx1 = CellHi(x + kPlayerHalfWidth);
    long by0 = CellLo(y - kPlayerHalfWidth);
    long by1 = CellHi(y + kPlayerHalfWidth);
    long bz0 = CellLo(z);
    long bz1 = CellHi(z + kPlayerHeight);
    long bx, by, bz;

    for (bz = bz0; bz <= bz1; bz++)
        for (by = by0; by <= by1; by++)
            for (bx = bx0; bx <= bx1; bx++)
                if (CellSolid(bx, by, bz))
                    return true;
    return false;
}

/* Highest block top at or below the feet among the columns the box covers.
 * Returns -1 when there is nothing underneath.  Scanning all z levels (not
 * just the top one) keeps an overhang from hiding the floor below it. */
Boolean Player_IsNearWater(const Player *p, short tiles)
{
    long bx = p->x >> 16;
    long by = p->y >> 16;
    long bx0 = (bx - tiles < 0) ? 0 : bx - tiles;
    long by0 = (by - tiles < 0) ? 0 : by - tiles;
    long bx1 = (bx + tiles >= kWorldX) ? kWorldX - 1 : bx + tiles;
    long by1 = (by + tiles >= kWorldY) ? kWorldY - 1 : by + tiles;
    long x, y, z;

    for (y = by0; y <= by1; y++) {
        for (x = bx0; x <= bx1; x++) {
            for (z = 0; z < kWorldZ; z++) {
                if (World_Get((short)x, (short)y, (short)z) == kBlockWater) {
                    return true;
                }
            }
        }
    }
    return false;
}

Boolean Player_InWater(const Player *p)
{
    return InWater(p->x, p->y, p->z);
}

static long SurfaceBelow(long x, long y, long feet)
{
    long bx0 = CellLo(x - kPlayerHalfWidth);
    long bx1 = CellHi(x + kPlayerHalfWidth);
    long by0 = CellLo(y - kPlayerHalfWidth);
    long by1 = CellHi(y + kPlayerHalfWidth);
    long best = -1;
    long bx, by, bz;

    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            for (bz = 0; bz < kWorldZ; bz++) {
                long top;

                if (!CellSolid(bx, by, bz))
                    continue;
                top = (bz + 1) * kPlayerOne;
                if (top <= feet && top > best)
                    best = top;
            }
        }
    }
    return best;
}

/* Lowest block bottom at or above the player's head, or -1. */
static long CeilingAbove(long x, long y, long feet)
{
    long head = feet + kPlayerHeight;
    long bx0 = CellLo(x - kPlayerHalfWidth);
    long bx1 = CellHi(x + kPlayerHalfWidth);
    long by0 = CellLo(y - kPlayerHalfWidth);
    long by1 = CellHi(y + kPlayerHalfWidth);
    long best = -1;
    long bx, by, bz;

    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            for (bz = 0; bz < kWorldZ; bz++) {
                long bottom;

                if (!CellSolid(bx, by, bz))
                    continue;
                bottom = bz * kPlayerOne;
                if (bottom >= head && (best < 0 || bottom < best))
                    best = bottom;
            }
        }
    }
    return best;
}

typedef enum { kAxisX = 0, kAxisY = 1 } Axis;

/* Nearest blocking face along one horizontal axis for the destination box.
 * direction is +1 or -1; positive movement stops at the smallest block face,
 * negative movement at the largest. */
static Boolean ScanFace(Axis axis, long x, long y, long z, int direction,
                        long *face)
{
    long bx0 = CellLo(x - kPlayerHalfWidth);
    long bx1 = CellHi(x + kPlayerHalfWidth);
    long by0 = CellLo(y - kPlayerHalfWidth);
    long by1 = CellHi(y + kPlayerHalfWidth);
    long bz0 = CellLo(z);
    long bz1 = CellHi(z + kPlayerHeight);
    Boolean found = false;
    long bx, by, bz;

    for (bz = bz0; bz <= bz1; bz++) {
        for (by = by0; by <= by1; by++) {
            for (bx = bx0; bx <= bx1; bx++) {
                long column;
                long candidate;

                if (!CellSolid(bx, by, bz))
                    continue;
                column = (axis == kAxisX) ? bx : by;
                candidate = (direction > 0) ? column * kPlayerOne
                                            : (column + 1) * kPlayerOne;
                if (!found ||
                    (direction > 0 ? candidate < *face : candidate > *face)) {
                    *face = candidate;
                    found = true;
                }
            }
        }
    }
    return found;
}

static long ClampStep(long remaining)
{
    if (remaining > kPlayerSubStep)
        return kPlayerSubStep;
    if (remaining < -kPlayerSubStep)
        return -kPlayerSubStep;
    return remaining;
}

static void SolveZ(Player *p)
{
    long remaining = p->vz;

    while (remaining != 0) {
        long step = ClampStep(remaining);
        long nz = p->z + step;

        if (!BoxHits(p->x, p->y, nz)) {
            p->z = nz;
            remaining -= step;
            continue;
        }

        if (step < 0) {
            long surface = SurfaceBelow(p->x, p->y, p->z);

            /* Snap to the highest floor the box is standing over.  Because
             * the sub-step is short, that floor is at most one block below
             * the feet, so this cannot teleport the player down a shaft. */
            if (surface >= 0 && surface <= p->z) {
                p->z = surface;
                p->onGround = true;
            }
        } else {
            long ceiling = CeilingAbove(p->x, p->y, p->z);

            if (ceiling >= 0)
                p->z = ceiling - kPlayerHeight;
            p->onGround = false;
        }
        p->vz = 0;
        break;
    }
}

/*
 * Auto-step: a grounded player walks up a single block without jumping.
 *
 * The renderer draws a one-block step and a two-block step eight pixels apart,
 * so they look alike; requiring a jump for every rise made single-block terrain
 * feel arbitrary. One block, and only while grounded, is the rule -- two blocks
 * still needs a jump and cannot be climbed by walking.
 */
static Boolean TryStepUp(long x, long y, long z)
{
    long nz = z + kPlayerOne;

    if (BoxHits(x, y, nz)) {
        return false;
    }
    /* Headroom above the stepped-up position: the box must fit after the rise
     * too, which BoxHits has just checked. Standing on the step is enough. */
    return true;
}

static void SolveX(Player *p)
{
    long remaining = p->vx;

    while (remaining != 0) {
        long step = ClampStep(remaining);
        long nx = p->x + step;
        long face;

        if (!BoxHits(nx, p->y, p->z)) {
            p->x = nx;
            remaining -= step;
            continue;
        }

        /* Blocked at this height: a grounded player steps up one block. */
        if (p->onGround && TryStepUp(nx, p->y, p->z)) {
            p->z = p->z + kPlayerOne;
            p->x = nx;
            remaining -= step;
            continue;
        }

        if (ScanFace(kAxisX, nx, p->y, p->z, step > 0 ? 1 : -1, &face)) {
            if (step > 0)
                p->x = face - kPlayerHalfWidth;
            else
                p->x = face + kPlayerHalfWidth;
        }
        p->vx = 0;
        break;
    }
}

static void SolveY(Player *p)
{
    long remaining = p->vy;

    while (remaining != 0) {
        long step = ClampStep(remaining);
        long ny = p->y + step;
        long face;

        if (!BoxHits(p->x, ny, p->z)) {
            p->y = ny;
            remaining -= step;
            continue;
        }

        /* Same rule on the other axis, so a step is climbable whichever way it
         * faces. */
        if (p->onGround && TryStepUp(p->x, ny, p->z)) {
            p->z = p->z + kPlayerOne;
            p->y = ny;
            remaining -= step;
            continue;
        }

        if (ScanFace(kAxisY, p->x, ny, p->z, step > 0 ? 1 : -1, &face)) {
            if (step > 0)
                p->y = face - kPlayerHalfWidth;
            else
                p->y = face + kPlayerHalfWidth;
        }
        p->vy = 0;
        break;
    }
}

static void Respawn(Player *p)
{
    p->x = sSpawnX;
    p->y = sSpawnY;
    p->z = sSpawnZ;
    p->vx = 0;
    p->vy = 0;
    p->vz = 0;
    p->onGround = false;
    p->respawns++;
}

void Player_Spawn(Player *p, short x, short y, short z)
{
    /* x and y name a block, so the feet go in the middle of that block; z is
     * already a feet height, so it is used as-is.  The respawn point is the
     * module's only mutable state. */
    sSpawnX = (long)x * kPlayerOne + kPlayerOne / 2;
    sSpawnY = (long)y * kPlayerOne + kPlayerOne / 2;
    sSpawnZ = (long)z * kPlayerOne;

    p->x = sSpawnX;
    p->y = sSpawnY;
    p->z = sSpawnZ;
    p->vx = 0;
    p->vy = 0;
    p->vz = 0;
    p->onGround = false;
    p->respawns = 0;
    p->jumpBuffer = 0;
    p->coyote = 0;
}

void Player_Step(Player *p, short moveX, short moveY, Boolean jump)
{
    long speed = kPlayerWalkSpeed;

    /* Diagonal input would otherwise move sqrt(2) times as fast; three
     * quarters per axis keeps a diagonal grid advance slower than a
     * straight one. */
    if (moveX != 0 && moveY != 0)
        speed = (kPlayerWalkSpeed * 3) / 4;

    p->vx = (long)moveX * speed;
    p->vy = (long)moveY * speed;

    /* Remember a jump for a few ticks, and grant a short grace period after
     * leaving the ground, so a jump pressed just before landing or just after
     * stepping off a ledge still happens. */
    if (jump) {
        p->jumpBuffer = kJumpBufferTicks;
    }
    else if (p->jumpBuffer > 0) {
        p->jumpBuffer--;
    }
    if (p->onGround) {
        p->coyote = kCoyoteTicks;
    }
    else if (p->coyote > 0) {
        p->coyote--;
    }

    /* The coyote window only *allows* a jump; it must never suppress gravity,
     * or the tick after take-off (when the window is still open) would cancel
     * the jump outright. */
    if ((p->onGround || p->coyote > 0) && !InWater(p->x, p->y, p->z) &&
        p->jumpBuffer > 0) {
        p->vz = kPlayerJumpSpeed;
        p->onGround = false; /* leave the floor this tick */
        p->jumpBuffer = 0;
        p->coyote = 0;
    }
    else if (p->onGround) {
        p->vz = 0; /* grounded and not jumping: no falling */
    }
    else if (InWater(p->x, p->y, p->z)) {
        /* Swimming: the pond is deep enough to stand in nowhere, so the water
         * holds the body up. Holding jump paddles upward, otherwise the player
         * sinks gently to the bottom instead of falling at full speed. */
        if (jump) {
            p->vz = kPlayerSwimSpeed;
        }
        else {
            p->vz -= kPlayerWaterGravity;
            if (p->vz < -kPlayerSwimSpeed)
                p->vz = -kPlayerSwimSpeed;
        }
        p->onGround = false;
    }
    else {
        p->vz -= kPlayerGravity;
        if (p->vz < -kPlayerMaxFall)
            p->vz = -kPlayerMaxFall;
    }

    SolveZ(p);

    /* Fell out of the bottom of the world. */
    if (p->z < 0) {
        Respawn(p);
        return;
    }

    SolveX(p);
    SolveY(p);

    /* Walking off a ledge leaves onGround set until the box no longer covers
     * a surface at exactly the feet height.  Checked after the horizontal
     * move so the flag is false on the very tick the player leaves. */
    if (p->onGround) {
        if (SurfaceBelow(p->x, p->y, p->z) != p->z)
            p->onGround = false;
    }
}

void Player_Restore(Player *p, const Player *saved)
{
    *p = *saved;
}
