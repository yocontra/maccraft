/* test_world.c - host tests for the static voxel world. */

#include <stdio.h>
#include <stdlib.h>

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

static void TestEmptyBlock(void)
{
    CHECK(World_Get(48, 0, 0) == 0);
    CHECK(World_Get(-1, 0, 0) == 0);
    CHECK(World_Get(0, 48, 0) == 0);
    CHECK(World_Get(0, -1, 0) == 0);
    CHECK(World_Get(0, 0, 8) == 0);
    CHECK(World_Get(0, 0, -1) == 0);
}

static void TestSetOutOfRange(void)
{
    World_Reset();

    CHECK(World_Set(-1, 0, 0, 1) == false);
    CHECK(World_Set(0, -1, 0, 1) == false);
    CHECK(World_Set(0, 0, -1, 1) == false);
    CHECK(World_Set(48, 0, 0, 1) == false);
    CHECK(World_Set(0, 48, 0, 1) == false);
    CHECK(World_Set(0, 0, 8, 1) == false);

    /* Nothing leaked through. */
    CHECK(World_Get(0, 0, 0) == 0);
}

static void TestPlateau(void)
{
    World_Reset();

    /* Plateau columns: x,y in 18..29, dirt under grass (ids 2 and 1). */
    CHECK(World_Get(24, 24, 0) == 2);
    CHECK(World_Get(24, 24, 1) == 1);
    CHECK(World_Get(18, 18, 0) == 2);
    CHECK(World_Get(29, 29, 1) == 1);
    CHECK(World_Get(29, 18, 0) == 2);
    CHECK(World_Get(18, 29, 1) == 1);

    /* Plateau is exactly two blocks tall and nothing above. */
    CHECK(World_Get(24, 24, 2) == 0);
    CHECK(World_TopZ(24, 24) == 1);
    CHECK(World_Height(24, 24) == 2);

    /* Just outside the plateau. */
    CHECK(World_Get(17, 24, 0) == 0);
    CHECK(World_Get(30, 24, 0) == 0);
    CHECK(World_Get(24, 17, 0) == 0);
    CHECK(World_Get(24, 30, 0) == 0);
    CHECK(World_TopZ(17, 24) == -1);
    CHECK(World_Height(17, 24) == 0);
    CHECK(World_TopZ(0, 0) == -1);
    CHECK(World_Height(0, 0) == 0);

    /* Corners of the world are empty. */
    CHECK(World_TopZ(0, 0) == -1);
    CHECK(World_TopZ(47, 47) == -1);
}

static void TestColumnHeightAndTop(void)
{
    short z;

    World_Reset();

    /* A floating block counts for TopZ but not for Height, because
       World_Height stops at the first hole from z=0. */
    CHECK(World_Set(0, 0, 5, 2) == true);
    CHECK(World_TopZ(0, 0) == 5);
    CHECK(World_Height(0, 0) == 0);

    /* Fill the column solid from the ground up. */
    for (z = 0; z <= 5; z++)
        CHECK(World_Set(0, 0, z, 2) == true);

    CHECK(World_Height(0, 0) == 6);
    CHECK(World_TopZ(0, 0) == 5);

    /* Remove the top block; both follow. */
    CHECK(World_Set(0, 0, 5, 0) == true);
    CHECK(World_TopZ(0, 0) == 4);
    CHECK(World_Height(0, 0) == 5);

    /* Removing a middle block stops the height count there. */
    CHECK(World_Set(0, 0, 2, 0) == true);
    CHECK(World_Height(0, 0) == 2);
    CHECK(World_TopZ(0, 0) == 4);
}

static void TestFullColumn(void)
{
    short z;

    World_Reset();

    /* kWorldZ == 8: eight blocks fit, the ninth (z=8) is rejected. */
    for (z = 0; z < kWorldZ; z++)
        CHECK(World_Set(47, 47, z, 1) == true);

    CHECK(World_Set(47, 47, kWorldZ, 1) == false);
    CHECK(World_Height(47, 47) == kWorldZ);
    CHECK(World_TopZ(47, 47) == kWorldZ - 1);
}

static void TestResetClearsEdits(void)
{
    World_Reset();
    CHECK(World_Set(0, 0, 0, 1) == true);
    CHECK(World_Get(0, 0, 0) == 1);

    World_Reset();
    CHECK(World_Get(0, 0, 0) == 0);
    CHECK(World_Get(24, 24, 1) == 1);
}

int main(void)
{
    TestEmptyBlock();
    TestSetOutOfRange();
    TestPlateau();
    TestColumnHeightAndTop();
    TestFullColumn();
    TestResetClearsEdits();

    if (sFailures != 0) {
        fprintf(stderr, "test_world: %d failure(s)\n", sFailures);
        return 1;
    }
    return 0;
}
