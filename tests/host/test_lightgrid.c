/*
 * test_lightgrid -- the sky pass and the torch flood fill.
 *
 * The renderer turns a stored level into a shade step, so the grid is the whole
 * lighting model: if a torch does not light its own cell, or sky light leaks
 * under a roof, the picture is wrong in a way no pixel test would localise.
 */

#include <stdio.h>

#include "world.h"
#include "blocks.h"
#include "light.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  CHECK failed: %s (line %d)\n", #cond, __LINE__);       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void ClearWorld(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, kBlockEmpty);
}

/* 1. Open ground is full sky; a solid block stops it dead. */
static void TestSkyPass(void)
{
    ClearWorld();
    Light_SetSky(15);
    CHECK(World_Set(10, 10, 0, kBlockStone));
    CHECK(World_Set(10, 10, 1, kBlockGrass));
    Light_Rebuild();

    CHECK(Light_Level(10, 10, 11) == 15);
    CHECK(Light_Level(10, 10, 2) == 15);
    CHECK(Light_Level(10, 10, 1) == 0);     /* inside the block */
    CHECK(Light_Level(10, 10, 0) == 0);     /* under it */
    CHECK(Light_Level(11, 11, 0) == 15);    /* open ground beside it */
}

/* 2. Night is the same grid at a lower sun level. */
static void TestNightIsDimmer(void)
{
    ClearWorld();
    Light_SetSky(15);
    Light_Rebuild();
    {
        short day = Light_Level(20, 20, 5);

        Light_SetSky(4);
        CHECK(Light_Level(20, 20, 5) == 4);
        CHECK(day == 15);
    }
    Light_SetSky(15);
}

/* 3. A torch lights its own cell and decays one per step, through air only. */
static void TestTorchFalloff(void)
{
    ClearWorld();
    CHECK(World_Set(30, 30, 1, kBlockTorch));
    Light_SetSky(0);                        /* night: torches are the only light */
    Light_Rebuild();
    CHECK(Light_TorchCount() == 1);

    CHECK(Light_Level(30, 30, 1) == 15);
    CHECK(Light_Level(30, 30, 2) == 14);
    CHECK(Light_Level(31, 30, 1) == 14);
    CHECK(Light_Level(32, 30, 1) == 13);
    CHECK(Light_Level(33, 30, 1) == 12);
    CHECK(Light_Level(30, 30, 1) > Light_Level(35, 30, 1));

    /* Out of range, and past the falloff, is dark. */
    CHECK(Light_Level(30 + 15, 30, 1) <= 1);
    Light_SetSky(15);
}

/* 4. A wall does not pass torch light, and a roof does not pass sky. */
static void TestOcclusion(void)
{
    short i;

    ClearWorld();
    for (i = 0; i < 6; i++) {
        CHECK(World_Set(40, 40, (short)(1 + i), kBlockPlanks));
    }
    CHECK(World_Set(43, 40, 1, kBlockTorch));
    Light_SetSky(0);
    Light_Rebuild();

    /* The wall sits between the torch and (40,40). */
    CHECK(Light_Level(43, 40, 1) == 15);
    CHECK(Light_Level(41, 40, 1) == 13);
    CHECK(Light_Level(40, 40, 1) < 4);

    /* Roof: sky over a covered cell. */
    CHECK(World_Set(50, 50, 3, kBlockPlanks));
    Light_SetSky(15);
    Light_Rebuild();
    CHECK(Light_Level(50, 50, 4) == 15);
    CHECK(Light_Level(50, 50, 2) == 0);
    CHECK(Light_Level(50, 50, 3) == 0);
}

/* 5. Relight after an edit matches a full rebuild. */
static void TestRelightMatchesRebuild(void)
{
    short x, y, z;

    ClearWorld();
    CHECK(World_Set(20, 20, 1, kBlockTorch));
    CHECK(World_Set(24, 20, 1, kBlockTorch));
    Light_SetSky(15);
    Light_Rebuild();

    /* An edit inside both torches' reach. */
    CHECK(World_Set(22, 20, 1, kBlockStone));
    Light_Relight(22, 20, 1);

    for (x = 16; x < 30; x++)
        for (y = 16; y < 25; y++)
            for (z = 0; z < 6; z++) {
                short before = Light_Level(x, y, z);
                short after;

                Light_Rebuild();
                after = Light_Level(x, y, z);
                if (before != after) {
                    printf("  relight mismatch at %d,%d,%d: %d vs %d\n",
                           x, y, z, before, after);
                    failures++;
                }
                Light_Relight(22, 20, 1);
            }
}

int main(void)
{
    TestSkyPass();
    TestNightIsDimmer();
    TestTorchFalloff();
    TestOcclusion();
    TestRelightMatchesRebuild();

    if (failures != 0) {
        printf("test_lightgrid: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_lightgrid: ok\n");
    return 0;
}
