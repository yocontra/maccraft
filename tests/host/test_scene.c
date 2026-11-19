/* test_scene.c - host tests for the hand-authored island scene.
 *
 * These assert the shape the scene is supposed to have, not the coordinates
 * it happens to use: materials present, one-depth water with a sand shoreline,
 * real elevation, trees above trunks, a built structure, and determinism.
 */

#include <stdio.h>
#include <string.h>

#include "scene.h"
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

/* Block ids agreed for the whole effort (0 = empty). */
enum {
    kBlockEmpty   = 0,
    kBlockGrass   = 1,
    kBlockDirt    = 2,
    kBlockStone   = 3,
    kBlockCobble  = 4,
    kBlockSand    = 5,
    kBlockGravel  = 6,
    kBlockPlanks  = 7,
    kBlockLog     = 8,
    kBlockLeaves  = 9,
    kBlockBrick   = 10,
    kBlockSnow    = 11,
    kBlockBedrock = 12,
    kBlockWater   = 13,
    kBlockGlass   = 14
};

#define kSeaLevel 1

static BlockID sFirst[kWorldX][kWorldY][kWorldZ];
static BlockID sSecond[kWorldX][kWorldY][kWorldZ];
static BlockID sThird[kWorldX][kWorldY][kWorldZ];

static const short kTestDirs[4][2] = {
    {0, -1},
    {1, 0},
    {0, 1},
    {-1, 0}
};

static void Snapshot(BlockID out[kWorldX][kWorldY][kWorldZ])
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                out[x][y][z] = World_Get(x, y, z);
}

static Boolean IsBeachMaterial(BlockID id)
{
    return (Boolean)(id == kBlockSand || id == kBlockGravel);
}

/* A material-rich scene, and no garbage ids leaking out of the builder. */
static void TestMaterialsAndBounds(void)
{
    int counts[256];
    int distinct = 0;
    short x, y, z;
    int i;

    Scene_BuildIsland();
    memset(counts, 0, sizeof counts);

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                counts[World_Get(x, y, z)]++;

    for (i = 0; i < 256; i++) {
        if (counts[i] == 0)
            continue;
        distinct++;
        /* Only the agreed 0..14 palette may appear. */
        CHECK(i <= kBlockGlass);
    }

    /* Acceptance minimum is four distinct materials including water; the
     * scene deliberately uses most of the palette, so name the load-bearing
     * ones rather than pinning an exact count. */
    CHECK(distinct >= 4);
    CHECK(counts[kBlockWater] > 0);
    CHECK(counts[kBlockGrass] > 0);
    CHECK(counts[kBlockSand] > 0);
    CHECK(counts[kBlockStone] > 0);
    CHECK(counts[kBlockLeaves] > 0);
    CHECK(counts[kBlockPlanks] > 0);
    CHECK(counts[kBlockBedrock] > 0);

    /* The accessors clamp out-of-range reads; the builder must not have
     * relied on that for its shape.  The island is inside open water, so no
     * border column rises above the sea. */
    CHECK(World_Get(-1, 0, 0) == kBlockEmpty);
    CHECK(World_Get(kWorldX, 0, 0) == kBlockEmpty);
    CHECK(World_Get(0, -1, 0) == kBlockEmpty);
    CHECK(World_Get(0, kWorldY, 0) == kBlockEmpty);
    CHECK(World_Get(0, 0, -1) == kBlockEmpty);
    CHECK(World_Get(0, 0, kWorldZ) == kBlockEmpty);

    for (x = 0; x < kWorldX; x++) {
        CHECK(World_TopZ(x, 0) <= kSeaLevel);
        CHECK(World_TopZ(x, kWorldY - 1) <= kSeaLevel);
    }
    for (y = 0; y < kWorldY; y++) {
        CHECK(World_TopZ(0, y) <= kSeaLevel);
        CHECK(World_TopZ(kWorldX - 1, y) <= kSeaLevel);
    }
}

/* Water is a single layer sitting on bedrock, and every in-world land cell
 * that touches it is beach.  That is what makes it a shoreline instead of a
 * blue patch of floor. */
static void TestWaterAndShore(void)
{
    short x, y, z, i, nx, ny, top;
    int water = 0;
    int shoreEdges = 0;

    Scene_BuildIsland();

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            for (z = 0; z < kWorldZ; z++) {
                if (World_Get(x, y, z) != kBlockWater)
                    continue;
                water++;
                CHECK(z == kSeaLevel);
            }
            if (World_Get(x, y, kSeaLevel) == kBlockWater) {
                /* One block deep, not a flooded column. */
                CHECK(World_Get(x, y, kSeaLevel + 1) == kBlockEmpty);
                CHECK(World_Get(x, y, 0) == kBlockBedrock);
            }
        }
    }

    CHECK(water > 300);

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (World_Get(x, y, kSeaLevel) != kBlockWater)
                continue;

            for (i = 0; i < 4; i++) {
                nx = (short)(x + kTestDirs[i][0]);
                ny = (short)(y + kTestDirs[i][1]);
                if (nx < 0 || nx >= kWorldX || ny < 0 || ny >= kWorldY)
                    continue;
                if (World_Get(nx, ny, kSeaLevel) == kBlockWater)
                    continue;

                /* This is the shoreline: land at the water's edge.  The
                 * shelf itself must be beach, even where a tree canopy
                 * overhangs it. */
                shoreEdges++;
                top = World_TopZ(nx, ny);
                CHECK(top >= kSeaLevel);
                CHECK(IsBeachMaterial(World_Get(nx, ny, kSeaLevel)));
            }
        }
    }

    CHECK(shoreEdges > 100);
}

/* Elevation: sea at 1, lowland at 2, plateau at 3, ridge to 6, structures
 * and canopies above that.  The column count per height proves it is terrain,
 * not a recoloured slab. */
static void TestElevation(void)
{
    short x, y, z, top;
    int zSeen[kWorldZ];
    int distinctZ = 0;
    short minSolid = kWorldZ;
    short maxSolid = -1;
    short minTop = kWorldZ;
    short maxTop = -1;
    int grassAboveSea = 0;
    int plateauColumns = 0;
    int sand = 0;
    int gravel = 0;
    int snow = 0;
    int cobble = 0;
    int dirtTops = 0;

    Scene_BuildIsland();
    memset(zSeen, 0, sizeof zSeen);

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            top = World_TopZ(x, y);
            if (top < minTop)
                minTop = top;
            if (top > maxTop)
                maxTop = top;

            if (top >= 3 && World_Get(x, y, top) == kBlockGrass)
                plateauColumns++;
            if (top > kSeaLevel && World_Get(x, y, top) == kBlockDirt)
                dirtTops++;

            for (z = 0; z < kWorldZ; z++) {
                if (World_Get(x, y, z) == kBlockEmpty)
                    continue;
                zSeen[z] = 1;
                if (z < minSolid)
                    minSolid = z;
                if (z > maxSolid)
                    maxSolid = z;
                if (z > kSeaLevel && World_Get(x, y, z) == kBlockGrass)
                    grassAboveSea++;
                if (World_Get(x, y, z) == kBlockSand)
                    sand++;
                if (World_Get(x, y, z) == kBlockGravel)
                    gravel++;
                if (World_Get(x, y, z) == kBlockSnow)
                    snow++;
                if (World_Get(x, y, z) == kBlockCobble)
                    cobble++;
            }
        }
    }

    for (z = 0; z < kWorldZ; z++)
        if (zSeen[z])
            distinctZ++;

    CHECK(distinctZ >= 4);
    CHECK(minSolid == 0);
    CHECK(maxSolid > minSolid);
    CHECK(maxSolid >= 5);
    CHECK(maxTop - minTop >= 3);

    CHECK(grassAboveSea > 0);
    CHECK(plateauColumns > 80);
    /* Dirt must be visible as a rim on cut grass terraces, not only as the
     * buried fill under them. */
    CHECK(dirtTops > 10);
    CHECK(sand > 50);
    CHECK(gravel > 0);
    CHECK(snow >= 1);
    CHECK(cobble > 0);
}

static void TestTreesAndStructure(void)
{
    short x, y, z, dz;
    int logs = 0;
    int leaves = 0;
    int planks = 0;
    int bricks = 0;
    int glass = 0;
    int treeColumns = 0;
    int leafAboveTrunk = 0;
    int wideCanopy = 0;

    Scene_BuildIsland();

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            for (z = 0; z < kWorldZ; z++) {
                BlockID id = World_Get(x, y, z);

                if (id == kBlockLog) {
                    int hasLeafAbove = 0;

                    logs++;
                    for (dz = (short)(z + 1); dz < kWorldZ; dz++) {
                        if (World_Get(x, y, dz) == kBlockLeaves) {
                            hasLeafAbove = 1;
                            break;
                        }
                    }
                    if (hasLeafAbove) {
                        treeColumns++;
                        leafAboveTrunk++;
                    }
                }
                else if (id == kBlockLeaves) {
                    leaves++;
                    /* A canopy is wider than the trunk: some leaf cell must
                     * sit two tiles out from some log column. */
                    if (!wideCanopy) {
                        short nx, ny;

                        for (dz = 0; dz < kWorldZ && !wideCanopy; dz++) {
                            for (nx = (short)(x - 2); nx <= x + 2 && !wideCanopy; nx++) {
                                for (ny = (short)(y - 2); ny <= y + 2; ny++) {
                                    int onRim = (nx == x - 2 || nx == x + 2 ||
                                                 ny == y - 2 || ny == y + 2);
                                    if (onRim && World_Get(nx, ny, dz) == kBlockLog) {
                                        wideCanopy = 1;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else if (id == kBlockPlanks) {
                    planks++;
                }
                else if (id == kBlockBrick) {
                    bricks++;
                }
                else if (id == kBlockGlass) {
                    glass++;
                }
            }
        }
    }

    /* Two log cells per tree, all three trees carrying leaves above. */
    CHECK(logs >= 6);
    CHECK(treeColumns >= 3);
    CHECK(leafAboveTrunk >= 3);
    CHECK(leaves > 30);
    CHECK(wideCanopy);

    /* The hut is more than a single block: walls/roof, chimney and window. */
    CHECK(planks > 20);
    CHECK(bricks >= 2);
    CHECK(glass >= 1);
}

static void TestDeterminism(void)
{
    Scene_BuildIsland();
    Snapshot(sFirst);

    Scene_BuildIsland();
    Snapshot(sSecond);
    CHECK(memcmp(sFirst, sSecond, sizeof sFirst) == 0);

    /* Clear the scene state with a different world (the old plateau), then
     * rebuild: the builder must erase what was there, not layer on it. */
    World_Reset();
    Scene_BuildIsland();
    Snapshot(sThird);
    CHECK(memcmp(sFirst, sThird, sizeof sFirst) == 0);
}

int main(void)
{
    TestMaterialsAndBounds();
    TestWaterAndShore();
    TestElevation();
    TestTreesAndStructure();
    TestDeterminism();

    if (sFailures != 0) {
        fprintf(stderr, "test_scene: %d check(s) failed\n", sFailures);
        return 1;
    }

    printf("test_scene: ok\n");
    return 0;
}
