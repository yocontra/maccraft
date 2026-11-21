/*
 * scene.c - the starting world.
 *
 * A 96x96 island with a mountain in the north-east, a lake in the south-west,
 * a forest band across the middle and a village on the flats near the centre.
 * Everything is generated from a deterministic hash, so the same seed gives the
 * same map on every machine and no random state has to be saved.
 *
 * The world store is the module-level grid declared by world.h, so the builder
 * needs no handle: it clears that store and writes through the range-checked
 * World_Set accessor.
 */

#include "scene.h"
#include "blocks.h"
#include "world.h"

#define kSeaLevel 1

/* Footprint: land inside this ellipse, sea outside it. */
#define kIslandCX 48
#define kIslandCY 48
#define kIslandRX 44
#define kIslandRY 42

/* Mountain in the north-east, lake in the south-west. */
#define kMountCX 72
#define kMountCY 26
#define kMountR  26
#define kLakeCX  30
#define kLakeCY  70
#define kLakeR   14

/* Village: a 3x3 plot on the flats west of centre. */
#define kVillageCX 38
#define kVillageCY 48
#define kVillageR  11

static const signed char kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

/* Deterministic hash, then smoothed into a height term. Two octaves is enough
 * for rolling ground at this scale; a third only adds noise per tile. */
static unsigned short Hash2(short x, short y, short seed)
{
    unsigned long h = (unsigned long)(unsigned short)x * 374761393UL +
                      (unsigned long)(unsigned short)y * 668265263UL +
                      (unsigned long)(unsigned short)seed * 2246822519UL;

    h ^= h >> 13;
    h *= 1274126177UL;
    h ^= h >> 16;
    return (unsigned short)(h & 0xFFFF);
}

/* Bilinear value noise in 0..255, `cell` tiles per lattice step. */
static short Noise(short x, short y, short seed, short cell)
{
    short gx = (short)(x / cell);
    short gy = (short)(y / cell);
    short fx = (short)(x % cell);
    short fy = (short)(y % cell);
    long top = ((long)(Hash2(gx, gy, seed) & 0xFF) * (cell - fx) +
                (long)(Hash2((short)(gx + 1), gy, seed) & 0xFF) * fx) / cell;
    long bottom = ((long)(Hash2(gx, (short)(gy + 1), seed) & 0xFF) * (cell - fx) +
                   (long)(Hash2((short)(gx + 1), (short)(gy + 1), seed) & 0xFF) * fx) / cell;

    return (short)((top * (cell - fy) + bottom * fy) / cell);
}

/* 1 inside the island ellipse, 0 outside: a soft edge rather than a hard
 * rectangle, so the coast does not look drawn with a ruler. */
static Boolean IsIsland(short x, short y)
{
    long dx = (long)x - kIslandCX;
    long dy = (long)y - kIslandCY;

    return (Boolean)((dx * dx * 100) / ((long)kIslandRX * kIslandRX) +
                     (dy * dy * 100) / ((long)kIslandRY * kIslandRY) <= 100);
}

/* 0..99 from the two noise terms: the biome and roughness driver. */
static short Roughness(short x, short y)
{
    short a = Noise(x, y, 11, 13);
    short b = Noise(x, y, 29, 5);

    return (short)((a * 2 + b) / 3);
}

static short MountHeight(short x, short y)
{
    long dx = (long)x - kMountCX;
    long dy = (long)y - kMountCY;
    long d2 = dx * dx + dy * dy;

    if (d2 >= (long)kMountR * kMountR)
        return 0;

    /* Cone, stepped by the hash so the slopes are not smooth rings. */
    return (short)(1 + (long)(kMountR - (short)(d2 / kMountR)) * 4 / kMountR +
                   (Hash2(x, y, 7) & 1));
}

static Boolean InLake(short x, short y)
{
    long dx = (long)x - kLakeCX;
    long dy = (long)y - kLakeCY;

    /* Clipped to the island: a lake that spills past the coast leaves water
     * hanging in the sky beside the beach. */
    if (!IsIsland(x, y))
        return false;
    return (Boolean)(dx * dx + dy * dy <= (long)kLakeR * kLakeR);
}

/* Terrain top for a column, or -1 for open water. */
static short TerrainHeight(short x, short y)
{
    short h;
    short rough;

    if (!IsIsland(x, y))
        return -1;
    if (InLake(x, y))
        return -1;

    rough = Roughness(x, y);
    h = (short)(2 + (rough > 150 ? 1 : 0) + (rough > 210 ? 1 : 0));
    h = (short)(h + MountHeight(x, y));

    /* Flatten the village plot: a village on a slope reads as rubble. */
    {
        long dx = (long)x - kVillageCX;
        long dy = (long)y - kVillageCY;

        if (dx * dx + dy * dy <= (long)kVillageR * kVillageR)
            h = 3;
    }

    if (h > kWorldZ - 2)
        h = (short)(kWorldZ - 2);
    return h;
}

static Boolean IsSceneCell(short x, short y)
{
    if (x < 0 || x >= kWorldX || y < 0 || y >= kWorldY)
        return false;
    return (Boolean)(IsIsland(x, y) || InLake(x, y));
}

static void ClearWorld(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, kBlockEmpty);
}

static Boolean TouchesWater(short x, short y)
{
    short i;

    for (i = 0; i < 4; i++) {
        short nx = (short)(x + kDirs[i][0]);
        short ny = (short)(y + kDirs[i][1]);

        if (nx < 0 || nx >= kWorldX || ny < 0 || ny >= kWorldY)
            continue;
        if (TerrainHeight(nx, ny) < 0)
            return true;
    }
    return false;
}

static void FillLandColumn(short x, short y, short top)
{
    short z;

    for (z = 1; z < top; z++) {
        /* Stone carries the mountain, dirt the lowlands. */
        World_Set(x, y, z, (BlockID)(top >= 4 ? kBlockStone : kBlockDirt));
    }
    if (top >= 5) {
        World_Set(x, y, (short)(top - 1), kBlockStone);
        World_Set(x, y, top, kBlockSnow);
    }
    else if (TouchesWater(x, y)) {
        /* Gravel bars among the sand: the same pale shore, broken up. */
        if ((Hash2(x, y, 17) & 7) == 0) {
            World_Set(x, y, top, kBlockGravel);
        }
        else {
            World_Set(x, y, top, kBlockSand);
        }
    }
    else if (top == 4 && (Hash2(x, y, 23) & 3) == 0) {
        /* Slopes of the mountain are bare dirt where the grass gives out. */
        World_Set(x, y, top, kBlockDirt);
    }
    else {
        World_Set(x, y, top, kBlockGrass);
    }
}

static void BuildTerrain(void)
{
    short x, y, top;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!IsSceneCell(x, y))
                continue;
            top = TerrainHeight(x, y);
            if (top < 0) {
                /* Water sits on the bedrock plate, like every other column: the
                 * bed of the lake and the sea is rock, not sand. */
                World_Set(x, y, 0, kBlockBedrock);
                World_Set(x, y, kSeaLevel, kBlockWater);
            }
            else {
                World_Set(x, y, 0, kBlockBedrock);
                FillLandColumn(x, y, top);
            }
        }
    }
}

/* ---------------------------------------------------------------- village */

/* A hut shell on any flat plot: plank walls, log corners, glass window, plank
 * gable, brick chimney. `w` and `h` are the inner footprint. */
static void BuildHutAt(short x0, short y0, short w, short h, short floorZ)
{
    short x, y, z;
    short x1 = (short)(x0 + w);
    short y1 = (short)(y0 + h);

    for (x = x0; x <= x1; x++) {
        for (y = y0; y <= y1; y++) {
            Boolean onX = (Boolean)(x == x0 || x == x1);
            Boolean onY = (Boolean)(y == y0 || y == y1);

            if (!onX && !onY)
                continue;
            for (z = (short)(floorZ + 1); z <= (short)(floorZ + 2); z++) {
                World_Set(x, y, z, (BlockID)(onX && onY ? kBlockLog : kBlockPlanks));
            }
        }
    }

    /* Doorway on the +y face: that is the face the camera looks at. */
    World_Set((short)(x0 + w / 2), y1, (short)(floorZ + 1), kBlockEmpty);
    World_Set((short)(x0 + w / 2), y1, (short)(floorZ + 2), kBlockEmpty);

    /* Window on the +x wall. */
    World_Set(x1, (short)(y0 + h / 2), (short)(floorZ + 2), kBlockGlass);

    /* Gable roof over the footprint. */
    for (x = x0; x <= x1; x++) {
        World_Set(x, y0, (short)(floorZ + 3), kBlockPlanks);
        World_Set(x, y1, (short)(floorZ + 3), kBlockPlanks);
    }
    for (x = x0; x <= x1; x++) {
        World_Set(x, (short)((y0 + y1) / 2), (short)(floorZ + 4), kBlockPlanks);
    }

    /* Chimney at a back corner, two courses above the eaves. */
    World_Set(x0, y0, (short)(floorZ + 3), kBlockBrick);
    World_Set(x0, y0, (short)(floorZ + 4), kBlockBrick);
}

/* Plank path across the square, one block wide. */
static void BuildPath(short x0, short y0, short x1, short y1)
{
    short x, y;

    for (x = x0; x <= x1; x++)
        for (y = y0; y <= y1; y++)
            World_Set(x, y, 3, kBlockPlanks);
}

static void BuildVillage(void)
{
    /* Four huts around a square, joined by plank paths. The plot is flat at
     * z=3 by construction (TerrainHeight), so nothing here needs to level. */
    BuildHutAt(32, 42, 4, 3, 3);
    BuildHutAt(44, 42, 3, 3, 3);
    BuildHutAt(32, 50, 4, 3, 3);
    BuildHutAt(44, 50, 3, 3, 3);

    BuildPath(36, 46, 44, 47);   /* across the square, west to east */
    BuildPath(39, 42, 40, 50);   /* north to south */

    /* A well head in the middle of the square: stone ring, water inside. */
    World_Set(41, 47, 4, kBlockCobblestone);
    World_Set(42, 47, 4, kBlockCobblestone);
    World_Set(41, 48, 4, kBlockCobblestone);
    World_Set(42, 48, 4, kBlockCobblestone);
    World_Set(41, 48, 3, kBlockWater);
    World_Set(42, 48, 3, kBlockWater);
}

/* ----------------------------------------------------------------- forest */

static void MirrorOffset(short variant, int *dx, int *dy)
{
    if (variant & 1)
        *dx = -*dx;
    if (variant & 2)
        *dy = -*dy;
}

static void PutLeaf(short x, short y, short z)
{
    /* A canopy that overhangs the coast would leave leaves floating in the sky
     * over open water, which the containment test rightly calls a bug. */
    if (!IsSceneCell(x, y))
        return;
    if (World_Get(x, y, z) == kBlockEmpty)
        World_Set(x, y, z, kBlockLeaves);
}

/* One trunk block with the canopy above it, else the leaves wrap the trunk and
 * the tree reads as a green cube. Three leaf layers: ragged skirt, broad
 * middle, sparse cap. */
static void PlantTree(short x, short y, short variant)
{
    static const signed char skirt[][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}
    };
    static const signed char middle[][2] = {
        {-2, 0}, {-1, -1}, {0, -1}, {1, -1}, {2, 0},
        {-1, 0}, {0, 0},   {1, 0},  {-1, 1}, {0, 1}, {1, 1}, {0, 2}
    };
    static const signed char cap[][2] = {
        {-1, 0}, {0, -1}, {1, 0}, {0, 0}
    };
    short ground, z0, i;
    int dx, dy;

    ground = World_TopZ(x, y);
    if (ground < kSeaLevel + 1 || ground + 4 >= kWorldZ)
        return;

    World_Set(x, y, ground + 1, kBlockLog);
    z0 = (short)(ground + 2);

    for (i = 0; i < (short)(sizeof skirt / sizeof skirt[0]); i++) {
        dx = skirt[i][0];
        dy = skirt[i][1];
        MirrorOffset(variant, &dx, &dy);
        PutLeaf((short)(x + dx), (short)(y + dy), z0);
    }
    for (i = 0; i < (short)(sizeof middle / sizeof middle[0]); i++) {
        dx = middle[i][0];
        dy = middle[i][1];
        MirrorOffset(variant, &dx, &dy);
        PutLeaf((short)(x + dx), (short)(y + dy), (short)(z0 + 1));
    }
    for (i = 0; i < (short)(sizeof cap / sizeof cap[0]); i++) {
        dx = cap[i][0];
        dy = cap[i][1];
        MirrorOffset(variant, &dx, &dy);
        PutLeaf((short)(x + dx), (short)(y + dy), (short)(z0 + 2));
    }
}

/* Forest where the roughness sits in a band: a belt across the middle of the
 * island plus clumps on the northern slopes. Trees keep two tiles of spacing
 * from each other and stay off the village and the paths. */
static void BuildForest(void)
{
    short x, y;

    for (y = 2; y < (short)(kWorldY - 2); y += 3) {
        for (x = 2; x < (short)(kWorldX - 2); x += 3) {
            short rough = Roughness(x, y);
            short h = TerrainHeight(x, y);
            short jx = (short)(Hash2(x, y, 3) % 3);
            short jy = (short)(Hash2(x, y, 5) % 3);
            short tx = (short)(x + jx);
            short ty = (short)(y + jy);

            if (h < 2 || h > 3)
                continue;
            if (rough < 120)
                continue;
            if (World_Get(tx, ty, h) != kBlockGrass)
                continue;
            /* Keep the village plot and its approach clear. */
            {
                long dx = (long)tx - kVillageCX;
                long dy = (long)ty - kVillageCY;

                if (dx * dx + dy * dy <= (long)(kVillageR + 3) * (kVillageR + 3))
                    continue;
            }
            if (World_TopZ(tx, ty) != h)
                continue;
            PlantTree(tx, ty, (short)(Hash2(tx, ty, 9) & 3));
        }
    }
}

void Scene_BuildIsland(void)
{
    ClearWorld();
    BuildTerrain();
    BuildVillage();
    BuildForest();
}

Boolean Scene_IsIslandCell(short x, short y)
{
    return IsIsland(x, y);
}
