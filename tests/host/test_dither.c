/* test_dither.c - host tests for the 4x4 ordered dither. */

#include <stdio.h>
#include <stdlib.h>

#include "dither.h"

static int sFailures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            sFailures++;                                                     \
        }                                                                    \
    } while (0)

static void TestDensity(void)
{
    short level;

    for (level = 0; level <= 16; level++) {
        int count = 0;
        short x, y;

        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                if (Dither_On(x, y, level))
                    count++;

        /* level/16 of the 4096 pixels, allowing 2/16 of error as the
           contract requires.  The Bayer matrix is exactly balanced, so the
           real count is level*256. */
        CHECK(count * 16 >= (level - 2) * 4096);
        CHECK(count * 16 <= (level + 2) * 4096);
        CHECK(count >= 0);
        CHECK(count <= 4096);
    }

    /* The endpoints must be absolute, not approximately dark. */
    CHECK(Dither_On(0, 0, 0) == 0);
    for (level = 0; level < 16; level += 5) {
        short x, y;

        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                if (level == 0)
                    CHECK(Dither_On(x, y, 0) == 0);
                CHECK(Dither_On(x, y, 16) != 0);
                CHECK(Dither_On(x, y, (short)99) != 0);
                CHECK(Dither_On(x, y, (short)-1) == 0);
            }
    }
}

static void TestDeterministic(void)
{
    short x, y, level;

    for (level = 0; level <= 16; level += 4)
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                Boolean first = Dither_On(x, y, level);

                CHECK(Dither_On(x, y, level) == first);

                /* The pattern tiles every four pixels in both directions, so
                   a baked sprite is stable wherever it is placed. */
                CHECK(Dither_On((short)(x + 4), (short)(y + 4), level) ==
                      first);
            }

    /* Negative coordinates wrap through the same 4x4 tile instead of
       indexing outside the matrix. */
    CHECK(Dither_On((short)-1, (short)-1, 8) == Dither_On(3, 3, 8));
}

static void TestSpatialOrder(void)
{
    short y;

    /* At level 8 exactly half the pixels are dark.  A random pattern would
       average one transition per two horizontal neighbours; the ordered
       pattern alternates on every step, so require at least half. */
    for (y = 0; y < 4; y++) {
        short x;
        int transitions = 0;

        for (x = 0; x < 63; x++)
            if (Dither_On(x, y, 8) != Dither_On((short)(x + 1), y, 8))
                transitions++;
        CHECK(transitions >= 32);
    }

    /* Every 2x2 block at level 8 is a checkerboard: two dark and two light,
       not a cluster. */
    {
        short x, yy;

        for (yy = 0; yy < 4; yy++)
            for (x = 0; x < 4; x++) {
                int dark = 0;

                dark += Dither_On(x, yy, 8) ? 1 : 0;
                dark += Dither_On((short)(x + 1), yy, 8) ? 1 : 0;
                dark += Dither_On(x, (short)(yy + 1), 8) ? 1 : 0;
                dark += Dither_On((short)(x + 1), (short)(yy + 1), 8) ? 1 : 0;
                CHECK(dark == 2);
            }
    }
}

static void TestLevel(void)
{
    short span;

    for (span = 1; span <= 3; span++) {
        short factor;
        short previous;

        CHECK(Dither_Level(256, span) == 0);
        CHECK(Dither_Level(0, span) == 16);
        CHECK(Dither_Level((short)-40, span) == 16);
        CHECK(Dither_Level((short)400, span) == 0);

        /* Monotone: more light can never mean more darkening, and every
           result stays in range. */
        previous = 16;
        for (factor = -20; factor <= 300; factor++) {
            short level = Dither_Level(factor, span);

            CHECK(level >= 0);
            CHECK(level <= 16);
            CHECK(level <= previous);
            previous = level;
        }
    }

    /* A mid-grey factor at span 1 is half dark. */
    CHECK(Dither_Level(128, 1) == 8);

    /* Out-of-range spans clamp to the nearest usable band. */
    CHECK(Dither_Level(128, 0) == Dither_Level(128, 1));
    CHECK(Dither_Level(128, 4) == Dither_Level(128, 3));
}

int main(void)
{
    TestDensity();
    TestDeterministic();
    TestSpatialOrder();
    TestLevel();

    if (sFailures != 0) {
        fprintf(stderr, "test_dither: %d failure(s)\n", sFailures);
        return EXIT_FAILURE;
    }
    printf("test_dither: ok\n");
    return EXIT_SUCCESS;
}
