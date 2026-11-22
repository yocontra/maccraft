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
    /* Two steps per ring, not four: with a twelve-block world the old cone
     * built ten-block cliffs, which read as brick walls rather than a hill.
     * The peak now tops out around seven, which leaves room for the snow cap. */
    return (short)(1 + (long)(kMountR - (short)(d2 / kMountR)) * 2 / kMountR +
                   ((Hash2(x / 3, y / 3, 7) & 1) && (d2 % 5) == 0 ? 1 : 0));
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

    /* The lowland is one level. The noise steps the ground by a block every few
     * tiles, and every such step is a lone pillar or pit whose shaded sides
     * scatter across the grass as brown chevrons. A flat basin with real hills
     * only on the mountain reads far better and is what the reference does. */
    if (h > 0 && h <= 4) {
        h = 3;
    }

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
    if (top >= 6) {
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
    else if (top >= 5 && (Hash2(x, y, 23) & 3) == 0) {
        /* Bare dirt only where the ground climbs to the mountain, so the
         * lowland keeps its grass. It used to fire on the flat plain too, which
         * scattered brown squares with nothing above them among the green. */
        World_Set(x, y, top, kBlockDirt);
    }
    else {
        World_Set(x, y, top, kBlockGrass);
    }
}

/* Terrain height with isolated single columns removed.
 *
 * The noise thresholds are smooth, but where a hill crosses one, a lone column
 * ends up a block above or below every neighbour. In a cube renderer that is a
 * one-tile pillar or pit, and its shaded sides read as a scatter of brown
 * chevrons across flat grass. A column that is higher (or lower) than all four
 * neighbours by the same amount is flattened into them. */
static short SmoothedHeight(short x, short y)
{
    short h = TerrainHeight(x, y);
    short n[4];
    short i;
    short higher = 1;
    short lower = 1;

    if (h < 0) {
        return h;
    }
    n[0] = TerrainHeight((short)(x - 1), y);
    n[1] = TerrainHeight((short)(x + 1), y);
    n[2] = TerrainHeight(x, (short)(y - 1));
    n[3] = TerrainHeight(x, (short)(y + 1));

    for (i = 0; i < 4; i++) {
        if (n[i] >= h) {
            higher = 0;
        }
        if (n[i] <= h) {
            lower = 0;
        }
    }
    if (higher) {
        short m = n[0];
        for (i = 1; i < 4; i++) {
            if (n[i] > m) {
                m = n[i];
            }
        }
        return m;
    }
    if (lower) {
        short m = n[0];
        for (i = 1; i < 4; i++) {
            if (n[i] < m) {
                m = n[i];
            }
        }
        return (short)(m < 0 ? h : m);
    }
    return h;
}

static void BuildTerrain(void)
{
    short x, y, top;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!IsSceneCell(x, y))
                continue;
            top = SmoothedHeight(x, y);
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

/* A hut on a flat plot: cobble sill, three courses of plank wall with log
 * corners, a two-block doorway with log jambs and a stone step, windows on both
 * flanks, and a pitched roof that overhangs the walls by a block on every side.
 *
 * Two things were wrong with the first version. Its roof was two eaves and a
 * ridge with the middle courses missing, so every hut had a hole along the top;
 * and with eight blocks of sky a hut at ground level had exactly one course
 * left for the roof, which is why they read as flat plank lids. The world is
 * twelve high now and the roof is a stair of courses, one per row, highest in
 * the middle. */
static void BuildHutAt(short x0, short y0, short w, short h, short floorZ)
{
    short x, y, z;
    short x1 = (short)(x0 + w);
    short y1 = (short)(y0 + h);
    short midX = (short)(x0 + w / 2);
    short midY = (short)(y0 + h / 2);
    short top = (short)(floorZ + 4);        /* walls: floorZ+1 .. floorZ+4 */
    short roofBase = (short)(floorZ + 5);

    /* Stone sill under the walls, so the hut sits on the ground. */
    for (x = x0; x <= x1; x++) {
        for (y = y0; y <= y1; y++) {
            if (x == x0 || x == x1 || y == y0 || y == y1) {
                World_Set(x, y, floorZ, kBlockCobblestone);
            }
        }
    }

    /* Walls: planks, with a log at every corner. */
    for (x = x0; x <= x1; x++) {
        for (y = y0; y <= y1; y++) {
            Boolean onX = (Boolean)(x == x0 || x == x1);
            Boolean onY = (Boolean)(y == y0 || y == y1);

            if (!onX && !onY) {
                continue;
            }
            for (z = (short)(floorZ + 1); z <= top; z++) {
                World_Set(x, y, z, (BlockID)(onX && onY ? kBlockLog : kBlockPlanks));
            }
        }
    }

    /* Doorway on the +y face -- the face the camera looks at. Two blocks wide
     * and two tall: a one-wide door exists, but the player's box is 0.6 blocks
     * and the walk intent is coarse, so the only way through a one-wide gap is
     * to line up on the exact column, which nobody does. Framed in log with a
     * stone step outside. */
    World_Set(midX, y1, (short)(floorZ + 1), kBlockEmpty);
    World_Set(midX, y1, (short)(floorZ + 2), kBlockEmpty);
    World_Set((short)(midX + 1), y1, (short)(floorZ + 1), kBlockEmpty);
    World_Set((short)(midX + 1), y1, (short)(floorZ + 2), kBlockEmpty);

    if ((short)(midX - 1) > x0) {
        World_Set((short)(midX - 1), y1, (short)(floorZ + 1), kBlockLog);
        World_Set((short)(midX - 1), y1, (short)(floorZ + 2), kBlockLog);
    }
    if ((short)(midX + 2) < x1) {
        World_Set((short)(midX + 2), y1, (short)(floorZ + 1), kBlockLog);
        World_Set((short)(midX + 2), y1, (short)(floorZ + 2), kBlockLog);
    }
    if ((short)(y1 + 1) < kWorldY) {
        World_Set(midX, (short)(y1 + 1), floorZ, kBlockCobblestone);
        World_Set((short)(midX + 1), (short)(y1 + 1), floorZ, kBlockCobblestone);
    }

    /* Windows on both flanks, at eye height. */
    World_Set(x1, midY, (short)(floorZ + 3), kBlockGlass);
    World_Set(x0, midY, (short)(floorZ + 3), kBlockGlass);

    /* Pitched roof, one course per row, highest in the middle, ends filled down
     * to the wall top so the attic is closed. No eave overhang: from this
     * camera a one-block overhang hangs in front of the walls and the door, and
     * the hut reads as a plank ramp sitting on the grass. */
    for (y = y0; y <= y1; y++) {
        short rise = (short)(y - y0);
        short fall = (short)(y1 - y);
        short step = (short)(rise < fall ? rise : fall);
        short course = (short)(roofBase + step);

        if (course >= kWorldZ) {
            course = (short)(kWorldZ - 1);
        }
        for (x = x0; x <= x1; x++) {
            if (x < 0 || x >= kWorldX || y < 0 || y >= kWorldY) {
                continue;
            }
            World_Set(x, y, course, kBlockPlanks);
        }
        /* Gable ends: close the triangle between the wall top and the roof. */
        for (z = (short)(top + 1); z < course; z++) {
            if (x0 >= 0 && x0 < kWorldX) {
                World_Set(x0, y, z, kBlockPlanks);
            }
            if (x1 >= 0 && x1 < kWorldX) {
                World_Set(x1, y, z, kBlockPlanks);
            }
        }
    }

    /* Chimney on the ridge: brick, two courses above the roof. */
    {
        short ridge = (short)(roofBase + (h + 1) / 2);

        for (z = ridge; z < (short)(ridge + 2) && z < kWorldZ; z++) {
            World_Set((short)(x0 + 1), midY, z, kBlockBrick);
        }
    }
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

/* An oak: two or three blocks of trunk with the canopy around and above its
 * top. The first version put one log on the ground and a shell of leaves
 * straight on top of it, which reads as a green cube sitting in the grass --
 * there was no trunk to see. Height is clamped: the world is eight blocks deep
 * and a hilltop tree has less room than a valley one. */
static void PlantTree(short x, short y, short variant)
{
    /* Diamond canopy, radius 2, minus the corners: two full frames. */
    static const signed char wide[][2] = {
        {-1, 0}, {0, -1}, {1, 0}, {0, 1},
        {-2, 0}, {2, 0}, {0, -2}, {0, 2},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
        {-2, -1}, {-1, -2}, {1, -2}, {2, -1},
        {-2, 1}, {-1, 2}, {1, 2}, {2, 1}
    };
    /* Skirt: the eight neighbours, one course below the first wide frame. */
    static const signed char skirt[][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}
    };
    short ground, space, trunk, t0, i, z;

    ground = World_TopZ(x, y);
    if (ground < kSeaLevel + 1 || ground + 3 >= kWorldZ) {
        return;
    }

    /* Four blocks of trunk, canopy above that. The player is 1.8 tall, so a
     * crown that starts two blocks up walls the ground off: you could not walk
     * under a tree. The lowest leaf course is the fifth block above the ground. */
    space = (short)(kWorldZ - 1 - ground);
    if (space < 7) {
        return;
    }
    trunk = 4;

    for (z = (short)(ground + 1); z <= (short)(ground + trunk); z++) {
        World_Set(x, y, z, kBlockLog);
    }
    t0 = (short)(ground + trunk + 1);       /* first leaf course above the trunk */

    /* Crown entirely *above* the trunk: a ring at the first course over the
     * bark, a 3x3 above that, a plus on top. A ring wrapped around the trunk's
     * own top course was the reason no tree had a trunk to see: each leaf
     * block's side face is 24 pixels tall, so a ring at the bark's level hung
     * down over three quarters of it and the tree read as a green cube. */
    for (i = 0; i < 12; i++) {
        int mx = wide[i][0];
        int my = wide[i][1];
        short dx, dy;

        MirrorOffset(variant, &mx, &my);
        dx = (short)mx;
        dy = (short)my;
        PutLeaf((short)(x + dx), (short)(y + dy), t0);
    }
    World_Set(x, y, t0, kBlockLeaves);

    if ((short)(t0 + 1) < kWorldZ) {
        for (i = 0; i < 8; i++) {
            int mx = skirt[i][0];
            int my = skirt[i][1];
            short dx, dy;

            MirrorOffset(variant, &mx, &my);
            dx = (short)mx;
            dy = (short)my;
            PutLeaf((short)(x + dx), (short)(y + dy), (short)(t0 + 1));
        }
        World_Set(x, y, (short)(t0 + 1), kBlockLeaves);
    }

    if ((short)(t0 + 2) < kWorldZ) {
        PutLeaf((short)(x - 1), y, (short)(t0 + 2));
        PutLeaf((short)(x + 1), y, (short)(t0 + 2));
        PutLeaf(x, (short)(y - 1), (short)(t0 + 2));
        PutLeaf(x, (short)(y + 1), (short)(t0 + 2));
        World_Set(x, y, (short)(t0 + 2), kBlockLeaves);
    }
}

/* Forest where the roughness sits in a band: a belt across the middle of the
 * island plus clumps on the northern slopes. Trees keep two tiles of spacing
 * from each other and stay off the village and the paths. */
static void BuildForest(void)
{
    short x, y;

    for (y = 2; y < (short)(kWorldY - 2); y += 5) {
        for (x = 2; x < (short)(kWorldX - 2); x += 5) {
            short rough = Roughness(x, y);
            short h = TerrainHeight(x, y);
            short jx = (short)(Hash2(x, y, 3) % 4);
            short jy = (short)(Hash2(x, y, 5) % 4);
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
