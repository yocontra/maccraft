/*
 * test_player.c - host tests for the fixed-point player simulation.
 *
 * The world is built by the test itself: a flat floor where needed, plus the
 * walls, steps and single blocks each case calls for.  Everything is checked
 * through the Player struct so the tests do not depend on player.c internals.
 */

#include <stdio.h>
#include <stdlib.h>

#include "blocks.h"
#include "player.h"
#include "world.h"

static int sFailures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            sFailures++;                                                     \
        }                                                                    \
    } while (0)

/* Fixed point to blocks, for readable tolerances. */
static double Blocks(long v)
{
    return (double)v / (double)kPlayerOne;
}

/* Local absolute value: keeps the test free of libm so the same command works
 * on hosts that do not link -lm. */
static double AbsD(double v)
{
    return (v < 0.0) ? -v : v;
}

static void ClearWorld(void)
{
    short x, y, z;

    /* World_Reset gives a defined starting point; then wipe the plateau so
     * each case starts from genuinely empty space. */
    World_Reset();
    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, 0);
}

static void FillGround(short x0, short x1, short y0, short y1, short z)
{
    short x, y;

    for (x = x0; x <= x1; x++)
        for (y = y0; y <= y1; y++)
            World_Set(x, y, z, 1);
}

/* 1. A player above solid ground falls, lands exactly on the surface and
 *    stays there. */
static void TestStanding(void)
{
    Player p;
    int i;

    ClearWorld();
    World_Set(24, 24, 0, 1);

    Player_Spawn(&p, 24, 24, 4);
    for (i = 0; i < 20; i++) {
        Player_Step(&p, 0, 0, false);
        CHECK(Blocks(p.z) >= 1.0);
    }
    CHECK(p.onGround);
    CHECK(p.z == kPlayerOne);
    CHECK(AbsD(Blocks(p.x) - 24.5) < 1e-6);
    CHECK(AbsD(Blocks(p.y) - 24.5) < 1e-6);
    CHECK(p.respawns == 0);

    /* Spawning with the feet exactly on the surface must settle on the very
     * next tick without sinking below it. */
    Player_Spawn(&p, 24, 24, 1);
    for (i = 0; i < 10; i++) {
        Player_Step(&p, 0, 0, false);
        CHECK(p.z == kPlayerOne);
    }
    CHECK(p.onGround);
}

/* 2. Straight walking covers one block in ten ticks; diagonal walking is
 *    slower per axis. */
static void TestWalking(void)
{
    Player p;
    long startX;
    int i;

    ClearWorld();
    FillGround(0, 47, 0, 47, 0);

    Player_Spawn(&p, 10, 10, 1);
    startX = p.x;
    for (i = 0; i < 10; i++)
        Player_Step(&p, 1, 0, false);
    CHECK(p.onGround);
    CHECK(p.z == kPlayerOne);
    CHECK(AbsD(Blocks(p.x - startX) - 1.0) <= 0.02);

    Player_Spawn(&p, 10, 10, 1);
    startX = p.x;
    for (i = 0; i < 10; i++)
        Player_Step(&p, 1, 1, false);
    CHECK(AbsD(Blocks(p.x - startX) - 0.75) <= 0.02);
    CHECK(Blocks(p.x - startX) < 0.98);
    CHECK(p.onGround);
}

/* 3. A jump peaks between one and two blocks and comes back down. */
static void TestJumping(void)
{
    Player p;
    long start, peak;
    int i;

    ClearWorld();
    FillGround(0, 47, 0, 47, 0);

    Player_Spawn(&p, 10, 10, 1);
    /* Spawning leaves the player ungrounded; settle for a few ticks so the
     * jump is taken from a real floor contact. */
    for (i = 0; i < 5; i++)
        Player_Step(&p, 0, 0, false);
    CHECK(p.onGround);

    start = p.z;
    peak = p.z;

    Player_Step(&p, 0, 0, true);
    if (p.z > peak)
        peak = p.z;

    for (i = 0; i < 60; i++) {
        Player_Step(&p, 0, 0, false);
        if (p.z > peak)
            peak = p.z;
    }

    CHECK(Blocks(peak - start) >= 1.0);
    CHECK(Blocks(peak - start) < 2.0);
    CHECK(p.onGround);
    CHECK(p.z == start);
}

/* 4. Walking into a one-block step while jumping climbs it; a two-block step
 *    is too tall. */
static void BuildStep(short stepHeight)
{
    ClearWorld();
    FillGround(20, 40, 20, 28, 0);
    /* The step is a single column in line with the player's y footprint. */
    if (stepHeight >= 1)
        World_Set(26, 24, 1, 1);
    if (stepHeight >= 2)
        World_Set(26, 24, 2, 1);
}

static void TestStepUp(void)
{
    Player p;
    long peak;
    int i;
    int climbed;

    BuildStep(1);
    Player_Spawn(&p, 24, 24, 1);
    climbed = 0;
    for (i = 0; i < 200 && !climbed; i++) {
        Player_Step(&p, 1, 0, true);
        if (p.onGround && p.z == 2 * kPlayerOne)
            climbed = 1;
    }
    CHECK(climbed);

    BuildStep(2);
    Player_Spawn(&p, 24, 24, 1);
    peak = p.z;
    for (i = 0; i < 200; i++) {
        Player_Step(&p, 1, 0, true);
        if (p.z > peak)
            peak = p.z;
    }
    CHECK(Blocks(peak) < 3.0);
    CHECK(AbsD(Blocks(p.x) - 25.7) < 0.02);
}

/*
 * 4b. A single-block rise is walked up, no jump needed; two blocks is not, in
 * any direction. The renderer draws a one-block and a two-block step eight
 * pixels apart, so "why can I not get on this one" needs a rule that does not
 * depend on judging that difference by eye.
 */
static void TestWalkUpStep(void)
{
    static const int offs[3][2] = {{1, 0}, {0, 1}, {1, 1}};
    int k, i, climbed;

    for (k = 0; k < 3; k++) {
        Player p;

        ClearWorld();
        FillGround(20, 40, 20, 40, 0);
        World_Set((short)(24 + offs[k][0] * 2), (short)(24 + offs[k][1] * 2), 1, 1);
        Player_Spawn(&p, 24, 24, 1);

        climbed = 0;
        for (i = 0; i < 200 && !climbed; i++) {
            Player_Step(&p, (short)offs[k][0], (short)offs[k][1], false);
            if (p.onGround && p.z == 2 * kPlayerOne)
                climbed = 1;
        }
        CHECK(climbed);
    }

    /* Two blocks is a wall: the player stays at ground level. */
    {
        Player p;

        ClearWorld();
        FillGround(20, 40, 20, 40, 0);
        World_Set(26, 24, 1, 1);
        World_Set(26, 24, 2, 1);
        Player_Spawn(&p, 24, 24, 1);
        for (i = 0; i < 200; i++)
            Player_Step(&p, 1, 0, false);
        CHECK(p.z == kPlayerOne);
    }
}

/*
 * 4c. Jump feel. A press that arrives a few ticks before landing must still
 * jump, and a press a few ticks after walking off a ledge must still jump --
 * otherwise precise jumps feel like dropped input.
 */
static void TestJumpGrace(void)
{
    Player p;
    int i;
    int jumped;

    /* Buffered: press jump while falling, land within the buffer window. */
    ClearWorld();
    FillGround(0, 47, 0, 47, 0);
    Player_Spawn(&p, 10, 10, 1);
    for (i = 0; i < 5; i++)
        Player_Step(&p, 0, 0, false);
    CHECK(p.onGround);

    Player_Step(&p, 0, 0, true);       /* take off */
    for (i = 0; i < 30 && !p.onGround; i++)
        Player_Step(&p, 0, 0, false);
    CHECK(p.onGround);

    jumped = 0;
    Player_Step(&p, 0, 0, true);       /* take off again */
    for (i = 0; i < 40; i++) {
        /* Press just before touching down: that press is buffered, and fires on
         * landing a tick or two later. */
        if (p.vz < 0 && p.z <= (kPlayerOne + 3 * 5243L)) {
            Player_Step(&p, 0, 0, true);   /* the early press */
            for (i = 0; i < 4; i++) {
                Player_Step(&p, 0, 0, false);
                if (p.vz > 0 || !p.onGround) {
                    jumped = 1;
                }
            }
            break;
        }
        Player_Step(&p, 0, 0, false);
    }
    CHECK(jumped);

    /* Coyote: walk off the edge of the floor, then jump a tick later. */
    ClearWorld();
    {
        short x, y;

        for (x = 0; x < 18; x++)
            for (y = 0; y < 20; y++)
                World_Set(x, y, 0, 1);
    }
    /* Spawn near the edge and walk until the body is off it. */
    Player_Spawn(&p, 16, 10, 1);
    for (i = 0; i < 5; i++)
        Player_Step(&p, 0, 0, false);
    CHECK(p.onGround);
    for (i = 0; i < 30 && p.onGround; i++)
        Player_Step(&p, 1, 0, false);
    CHECK(!p.onGround);                /* the coyote window is open now */
    jumped = 0;
    Player_Step(&p, 1, 0, true);
    if (p.vz > 0)
        jumped = 1;
    CHECK(jumped);
}

/*
 * 4d. Water: not solid. The body sinks to the bed, holding jump paddles it up
 * past the surface, releasing sinks it again.
 */
static void TestSwimming(void)
{
    Player p;
    int i;
    short x, y;

    ClearWorld();
    for (x = 20; x < 30; x++)
        for (y = 20; y < 30; y++) {
            World_Set(x, y, 0, kBlockGrass);
            World_Set(x, y, 1, kBlockWater);
        }

    Player_Spawn(&p, 25, 25, 4);
    for (i = 0; i < 90; i++)
        Player_Step(&p, 0, 0, false);

    /* Standing on the pond bed, not on the water surface. */
    CHECK(p.z == kPlayerOne);
    CHECK(p.onGround);

    /* Paddling lifts the feet to the surface (water top is 2.0). The body bobs
     * there rather than sticking, so check the peak, not the last tick. */
    {
        long peak = p.z;

        for (i = 0; i < 120; i++) {
            Player_Step(&p, 0, 0, true);
            if (p.z > peak)
                peak = p.z;
        }
        CHECK(peak >= 2 * kPlayerOne);
    }

    /* Letting go sinks back to the bed. */
    for (i = 0; i < 200; i++)
        Player_Step(&p, 0, 0, false);
    CHECK(p.z == kPlayerOne);
}

/* 5. A two-block wall stops the player at its face while staying grounded. */
static void TestWalls(void)
{
    Player p;
    int i;

    ClearWorld();
    FillGround(20, 40, 20, 28, 0);
    World_Set(26, 24, 1, 1);
    World_Set(26, 24, 2, 1);

    Player_Spawn(&p, 24, 24, 1);
    for (i = 0; i < 60; i++)
        Player_Step(&p, 1, 0, false);

    CHECK(AbsD(Blocks(p.x) - 25.7) < 0.02);
    CHECK(p.onGround);
    CHECK(p.z == kPlayerOne);
}

/* 6. Walking off a platform starts a fall, and hitting the bottom respawns at
 *    the recorded spawn point. */
static void TestFallingOff(void)
{
    Player p;
    long spawnX, spawnY, spawnZ;
    int i;
    int fellAt = -1;

    ClearWorld();
    FillGround(20, 25, 22, 26, 0);

    Player_Spawn(&p, 22, 24, 1);
    spawnX = p.x;
    spawnY = p.y;
    spawnZ = p.z;

    for (i = 0; i < 80; i++) {
        Player_Step(&p, 1, 0, false);
        if (!p.onGround) {
            fellAt = i;
            break;
        }
    }

    CHECK(fellAt >= 0);
    /* The floor ends at x == 26; the box is 0.6 wide, so support is lost the
     * tick its left edge crosses 26.0, i.e. around x == 26.3.  That is the
     * same tick onGround clears. */
    CHECK(fellAt >= 0 && fellAt <= 45);
    CHECK(Blocks(p.x) >= 26.2 && Blocks(p.x) <= 26.4);
    CHECK(p.respawns == 0);

    for (i = 0; i < 80 && p.respawns == 0; i++)
        Player_Step(&p, 0, 0, false);

    CHECK(p.respawns == 1);
    CHECK(p.x == spawnX);
    CHECK(p.y == spawnY);
    CHECK(p.z == spawnZ);
    CHECK(p.vx == 0 && p.vy == 0 && p.vz == 0);
    CHECK(!p.onGround);
}

/* 7. A terminal-velocity fall onto a single block lands on top of it. */
static void TestNoTunnelling(void)
{
    Player p;
    int i;

    ClearWorld();
    World_Set(24, 24, 0, 1);

    Player_Spawn(&p, 24, 24, 7);
    for (i = 0; i < 60; i++)
        Player_Step(&p, 0, 0, false);

    CHECK(p.onGround);
    CHECK(p.z == kPlayerOne);
    CHECK(p.respawns == 0);
}

/* 8. Identical state plus identical inputs gives identical results. */
static void RunPattern(Player *q)
{
    int i;

    for (i = 0; i < 40; i++) {
        short moveX = (short)((i % 3) - 1);
        short moveY = (short)(((i / 3) % 3) - 1);
        Boolean jump = (Boolean)((i % 7) == 0);

        Player_Step(q, moveX, moveY, jump);
    }
}

static void TestDeterminism(void)
{
    Player p, a, b;
    int i;

    ClearWorld();
    FillGround(8, 40, 8, 40, 0);

    Player_Spawn(&p, 10, 10, 1);
    for (i = 0; i < 7; i++)
        Player_Step(&p, (short)((i % 3) - 1), (short)(((i + 1) % 3) - 1),
                    (Boolean)(i % 5 == 0));

    a = p;
    b = p;
    RunPattern(&a);
    RunPattern(&b);

    CHECK(a.x == b.x && a.y == b.y && a.z == b.z);
    CHECK(a.vx == b.vx && a.vy == b.vy && a.vz == b.vz);
    CHECK(a.onGround == b.onGround);
    CHECK(a.respawns == b.respawns);

    /* Player_Restore must reproduce that same state exactly. */
    Player_Restore(&b, &p);
    CHECK(b.x == p.x && b.y == p.y && b.z == p.z);
    CHECK(b.vx == p.vx && b.vy == p.vy && b.vz == p.vz);
    CHECK(b.onGround == p.onGround);
    CHECK(b.respawns == p.respawns);
}

int main(void)
{
    TestStanding();
    TestWalking();
    TestJumping();
    TestStepUp();
    TestWalkUpStep();
    TestJumpGrace();
    TestSwimming();
    TestWalls();
    TestFallingOff();
    TestNoTunnelling();
    TestDeterminism();

    if (sFailures != 0) {
        fprintf(stderr, "%d player check(s) failed\n", sFailures);
        return EXIT_FAILURE;
    }
    printf("test_player: all checks passed\n");
    return EXIT_SUCCESS;
}
