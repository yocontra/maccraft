/* test_iso.c - host tests for the integer isometric projection. */

#include <stdio.h>
#include <stdlib.h>

#include "iso.h"
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

static const short kCamX[] = { 0, -160, 160 };
static const short kCamY[] = { 0, -80, 80 };

static void TestOrigin(void)
{
    short sx, sy;

    Iso_Project(0, 0, 0, 0, 0, &sx, &sy);
    CHECK(sx == 0);
    CHECK(sy == 0);

    /* The 2:1 lattice: one x step is half a tile right, quarter down. */
    Iso_Project(1, 0, 0, 0, 0, &sx, &sy);
    CHECK(sx == kTileW / 2);
    CHECK(sy == kTileH / 2);

    Iso_Project(0, 1, 0, 0, 0, &sx, &sy);
    CHECK(sx == -(kTileW / 2));
    CHECK(sy == kTileH / 2);

    /* Raising a block moves it up by half a tile. */
    Iso_Project(0, 0, 1, 0, 0, &sx, &sy);
    CHECK(sx == 0);
    CHECK(sy == -kTileH);

    /* Cameras translate without changing shape. */
    Iso_Project(3, 5, 2, 100, -50, &sx, &sy);
    CHECK(sx == (3 - 5) * (kTileW / 2) + 100);
    CHECK(sy == (3 + 5) * (kTileH / 2) - 2 * kTileH - 50);
}

static void TestRoundTrip(void)
{
    short x, y, i, sx, sy, ux, uy;

    for (i = 0; i < 3; i++) {
        short camX = kCamX[i];
        short camY = kCamY[i];

        for (x = 0; x < kWorldX; x++) {
            for (y = 0; y < kWorldY; y++) {
                Iso_Project(x, y, 0, camX, camY, &sx, &sy);
                if (!Iso_Unproject(sx, sy, camX, camY, &ux, &uy)) {
                    fprintf(stderr,
                            "round-trip rejected x=%d y=%d cam=(%d,%d)\n",
                            x, y, camX, camY);
                    sFailures++;
                    continue;
                }
                if (ux != x || uy != y) {
                    fprintf(stderr,
                            "round-trip x=%d y=%d -> %d,%d cam=(%d,%d)\n",
                            x, y, ux, uy, camX, camY);
                    sFailures++;
                }
            }
        }
    }
}

static void TestNoNegativeTile(void)
{
    short x = -99, y = -99;

    /* Display origin is the top tile, never a negative coordinate. */
    CHECK(Iso_Unproject(0, 0, 0, 0, &x, &y) == true);
    CHECK(x == 0);
    CHECK(y == 0);
    CHECK(x >= 0 && y >= 0);

    /* Points left of / above the origin are outside the world. */
    CHECK(Iso_Unproject(-1, 0, 0, 0, &x, &y) == false);
    CHECK(Iso_Unproject(0, -1, 0, 0, &x, &y) == false);
    CHECK(Iso_Unproject(-(kTileW / 2), -(kTileH / 2), 0, 0, &x, &y) == false);
}

static void TestOutsideWorld(void)
{
    short x = -99, y = -99;
    short sx, sy;

    /* Projected point for tile 48 (one past the last column). */
    Iso_Project(kWorldX, 0, 0, 0, 0, &sx, &sy);
    CHECK(Iso_Unproject(sx, sy, 0, 0, &x, &y) == false);

    Iso_Project(0, kWorldY, 0, 0, 0, &sx, &sy);
    CHECK(Iso_Unproject(sx, sy, 0, 0, &x, &y) == false);

    Iso_Project(47, 47, 0, 0, 0, &sx, &sy);
    CHECK(Iso_Unproject(sx, sy, 0, 0, &x, &y) == true);
    CHECK(x == 47);
    CHECK(y == 47);
}

static void TestExtremesDoNotOverflowShort(void)
{
    short sx, sy;

    /* x=y=47, z=7: the largest magnitude the grid can produce. */
    Iso_Project(47, 47, 7, 0, 0, &sx, &sy);
    CHECK(sx == 0);
    CHECK(sy == (47 + 47) * (kTileH / 2) - 7 * kTileH);
    CHECK(sy == (47 + 47) * (kTileH / 2) - 7 * kTileH);

    /* Cameras are short by contract; the world extremes must fit too. */
    Iso_Project(47, 47, 7, 30000, 30000, &sx, &sy);
    CHECK(sx == 30000);
    CHECK(sy == (47 + 47) * (kTileH / 2) - 7 * kTileH + 30000);
}

int main(void)
{
    TestOrigin();
    TestRoundTrip();
    TestNoNegativeTile();
    TestOutsideWorld();
    TestExtremesDoNotOverflowShort();

    if (sFailures != 0) {
        fprintf(stderr, "test_iso: %d failure(s)\n", sFailures);
        return 1;
    }
    return 0;
}
