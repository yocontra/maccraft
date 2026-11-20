/*
 * test_pick.c - host tests for click picking against raised terrain.
 *
 * The failure this guards against: unprojecting a point on the z=0 plane
 * puts a click on a hill several columns behind it.  Each test builds its
 * own world with World_Set and checks the surface Pick_Tile resolves.
 */

#include <stdio.h>

#include "iso.h"
#include "pick.h"
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

static void ClearWorld(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, 0);
}

static void BuildColumn(short x, short y, short height)
{
    short z;

    for (z = 0; z < height; z++)
        CHECK(World_Set(x, y, z, 1));
}

/* Pick the centre of a column's top face and demand exactly that column. */
static void CheckColumnCentre(short x, short y, short surfaceZ, short camX,
                              short camY)
{
    short sx, sy, px = -1, py = -1, pz = -1;

    Iso_Project(x, y, surfaceZ, camX, camY, &sx, &sy);
    CHECK(Pick_Tile(sx, sy, camX, camY, &px, &py, &pz));
    if (px != x || py != y || pz != surfaceZ) {
        fprintf(stderr,
                "pick at centre of (%d,%d,z=%d) cam=(%d,%d) gave (%d,%d,%d)\n",
                x, y, surfaceZ, camX, camY, px, py, pz);
        sFailures++;
    }
}

static void TestFlatColumns(void)
{
    static const short kCamX[] = { 0, -160, 160 };
    static const short kCamY[] = { 0, -80, 80 };
    short i, x, y;

    ClearWorld();
    for (x = 20; x < 26; x++)
        for (y = 20; y < 26; y++)
            BuildColumn(x, y, 1);

    for (i = 0; i < 3; i++)
        for (x = 20; x < 26; x++)
            for (y = 20; y < 26; y++)
                CheckColumnCentre(x, y, 1, kCamX[i], kCamY[i]);
}

/*
 * A three-block stack on a one-block base sits three levels above the flat
 * area beside it, so its top face lands 8 screen pixels above the flat
 * column's top centre.  The point 4 pixels above that centre is inside both
 * diamonds: the old ground-plane unprojection would answer flat, the visible
 * surface is the stack.
 */
static void TestRaisedBeatsFlat(void)
{
    short sx, sy, px = -1, py = -1, pz = -1;

    ClearWorld();
    BuildColumn(10, 10, 1); /* flat, surface z = 1 */
    BuildColumn(11, 11, 4); /* stack, surface z = 4 */

    Iso_Project(10, 10, 1, 0, 0, &sx, &sy);
    CHECK(Pick_Tile(sx, sy - 4, 0, 0, &px, &py, &pz));
    CHECK(px == 11);
    CHECK(py == 11);
    CHECK(pz == 4);
}

/*
 * The same bug in its coarse form: 40 pixels above an empty column's top
 * (surface z = 0) is exactly the centre of the top face of the seven-block
 * stack two tiles nearer the camera.  Ground-plane unprojection names a
 * column far behind it; the visible surface is the stack.
 */
static void TestFortyPixelsAboveAPlainColumn(void)
{
    short sx, sy, gx = -1, gy = -1, px = -1, py = -1, pz = -1;

    ClearWorld();
    BuildColumn(10, 10, 0); /* empty column: surface z = 0 */
    BuildColumn(11, 11, 7); /* full-height stack in front */

    Iso_Project(10, 10, 0, 0, 0, &sx, &sy);
    CHECK(Iso_Unproject(sx, sy - 40, 0, 0, &gx, &gy));
    CHECK(gx == 7 && gy == 7); /* what the z=0 plane alone would pick */

    CHECK(Pick_Tile(sx, sy - 40, 0, 0, &px, &py, &pz));
    CHECK(px == 11);
    CHECK(py == 11);
    CHECK(pz == 7);
}

static void TestRejectsEmptySpace(void)
{
    short sx, sy;
    short px = -7, py = -7, pz = -7;

    ClearWorld();
    BuildColumn(24, 24, 1);

    /* Outside the play area on every side. */
    CHECK(Pick_Tile(-1, 0, 0, 0, &px, &py, &pz) == false);
    CHECK(Pick_Tile(0, -1, 0, 0, &px, &py, &pz) == false);

    Iso_Project(kWorldX, 0, 0, 0, 0, &sx, &sy);
    CHECK(Pick_Tile(sx, sy, 0, 0, &px, &py, &pz) == false);

    Iso_Project(0, kWorldY, 0, 0, 0, &sx, &sy);
    CHECK(Pick_Tile(sx, sy, 0, 0, &px, &py, &pz) == false);

    /* Far above the terrain: the ground candidate is above the world. */
    Iso_Project(24, 24, 0, 0, 0, &sx, &sy);
    CHECK(Pick_Tile(sx, sy - 400, 0, 0, &px, &py, &pz) == false);

    /* Failed picks leave the outputs untouched. */
    CHECK(px == -7 && py == -7 && pz == -7);
}

/*
 * Painter order must not leak into picking: each column's own top centre
 * belongs to that column even when a taller, more distant column or a
 * nearer, shorter one is close enough to share the search window.
 */
static void TestTallColumnDoesNotShadowNearerColumn(void)
{
    ClearWorld();
    BuildColumn(20, 20, 6); /* distant, tall */
    BuildColumn(22, 20, 1); /* nearer, short */

    CheckColumnCentre(20, 20, 6, 0, 0);
    CheckColumnCentre(22, 20, 1, 0, 0);
}

static void TestDeterminism(void)
{
    short sx, sy;
    short x1 = -1, y1 = -1, z1 = -1, x2 = -1, y2 = -1, z2 = -1;

    ClearWorld();
    BuildColumn(30, 30, 3);
    BuildColumn(31, 30, 1);

    Iso_Project(30, 30, 3, -160, -80, &sx, &sy);
    CHECK(Pick_Tile(sx, sy, -160, -80, &x1, &y1, &z1));
    CHECK(Pick_Tile(sx, sy, -160, -80, &x2, &y2, &z2));
    CHECK(x1 == x2 && y1 == y2 && z1 == z2);
}

/*
 * Round trip: whatever Pick_Tile returns for a point must be a surface whose
 * top diamond actually contains that point. The hover wireframe is drawn from
 * the returned tile, so if this fails the ring lands somewhere else.
 */
static void TestRoundTrip()
{
    static const short cams[3][2] = {{0, 0}, {-160, -80}, {160, 80}};
    short c, sx, sy;

    ClearWorld();
    BuildColumn(24, 24, 3);
    BuildColumn(30, 30, 1);
    BuildColumn(18, 28, 2);

    for (c = 0; c < 3; c++) {
        short camX = cams[c][0];
        short camY = cams[c][1];

        for (sx = 200; sx <= 400; sx += 7) {
            for (sy = 120; sy <= 320; sy += 5) {
                short x, y, z, px, py;
                long dx, dy;

                if (!Pick_Tile(sx, sy, camX, camY, &x, &y, &z)) {
                    continue;
                }
                Iso_Project(x, y, z, camX, camY, &px, &py);
                dx = (long)sx - px;
                dy = (long)sy - py;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;

                /* Inside the diamond, one pixel of slack for the edge test. */
                if (dx * (kTileH / 2) + dy * (kTileW / 2) >
                    (long)(kTileW / 2) * (kTileH / 2) + (kTileW / 2)) {
                    fprintf(stderr,
                            "pick(%d,%d) cam(%d,%d) -> (%d,%d,%d) projects to "
                            "(%d,%d): point is outside its diamond\n",
                            sx, sy, camX, camY, x, y, z, px, py);
                    sFailures++;
                    return;
                }
            }
        }
    }
}

/*
 * The side faces are what let a click build against a wall instead of piling on
 * top. Nothing exercised them: Pick_Tile only reports the column, so a wrong
 * face or a wrong neighbour shipped silently and looked like "clicking a wall
 * does nothing".
 */
static void TestFaces(void)
{
    short sx, sy, x, y, z, face;

    ClearWorld();
    BuildColumn(20, 20, 3);

    Iso_Project(20, 20, 2, 0, 0, &sx, &sy);

    /* Centre of the top face. */
    CHECK(Pick_TileFace(sx, sy - 2, 0, 0, &x, &y, &z, &face));
    CHECK(face == kPickFaceTop);
    CHECK(x == 20 && y == 20);

    /* Eight pixels right and twelve down is inside the right face, which points
     * at the column at (x+1, y). */
    CHECK(Pick_TileFace((short)(sx + 8), (short)(sy + 12), 0, 0, &x, &y, &z, &face));
    CHECK(face == kPickFaceRight);
    CHECK(x == 20 && y == 20);
    CHECK(z == 3);

    /* The mirror on the left, pointing at (x, y+1). */
    CHECK(Pick_TileFace((short)(sx - 8), (short)(sy + 12), 0, 0, &x, &y, &z, &face));
    CHECK(face == kPickFaceLeft);
    CHECK(x == 20 && y == 20);
}

int main(void)
{
    TestFlatColumns();
    TestRaisedBeatsFlat();
    TestFortyPixelsAboveAPlainColumn();
    TestRejectsEmptySpace();
    TestTallColumnDoesNotShadowNearerColumn();
    TestDeterminism();
    TestRoundTrip();
    TestFaces();

    if (sFailures == 0)
        printf("all pick tests passed\n");
    return sFailures == 0 ? 0 : 1;
}
