/*
 * scene.c - the hand-authored island the game opens on.
 *
 * The old World_Reset plateau was a 12x12 slab of one block type, which reads
 * as a single green diamond from the fixed isometric camera.  This scene is
 * composed instead: a finite elliptical island with a raised grass plateau,
 * dirt-cut edges, a cobblestone ridge with a stone crown and snow caps, a
 * beach-rimmed lagoon, one small hut and three trees.
 *
 * The world is not filled wall to wall.  Only the island footprint - the
 * coast ellipse and the lagoon cut into it - carries blocks; everything
 * outside it stays empty, so the sky is the backdrop rather than a dark
 * bedrock plate.
 *
 * Everything is deterministic integer geometry - no rand(), no clock, no
 * floating point - and every write goes through World_Set, so the world's
 * bounds are enforced by the one module that owns them.
 *
 * Height convention: z = 0 is bedrock under the island, the lagoon surface is
 * the single water layer at z = 1, and the sand shelf that meets it is one
 * block higher at z = 2.
 */

#include "scene.h"
#include "world.h"

/* Block ids agreed for the whole effort (0 = empty). */
enum {
    kBlockEmpty      = 0,
    kBlockGrass      = 1,
    kBlockDirt       = 2,
    kBlockStone      = 3,
    kBlockCobble     = 4,
    kBlockSand       = 5,
    kBlockGravel     = 6,
    kBlockPlanks     = 7,
    kBlockLog        = 8,
    kBlockLeaves     = 9,
    kBlockBrick      = 10,
    kBlockSnow       = 11,
    kBlockBedrock    = 12,
    kBlockWater      = 13,
    kBlockGlass      = 14
};

/* Water is one block deep: bedrock at z = 0, water at z = 1. */
#define kSeaLevel 1

/* Island silhouette: an ellipse with a deterministic coastal wobble.  The
 * radii keep the coast well inside the world border; the cells beyond it are
 * left empty so the island reads against the sky. */
#define kIslandCX 24
#define kIslandCY 24
#define kIslandRX 20
#define kIslandRY 17
#define kIslandRX2 (kIslandRX * kIslandRX)
#define kIslandRY2 (kIslandRY * kIslandRY)

/* Raised grass heart of the island, slightly north-west of the centre. */
#define kPlateauCX 22
#define kPlateauCY 23

/* Stone massif on the east side. */
#define kRidgeCX 35
#define kRidgeCY 19

/* Hut footprint on the plateau, one cell of overhang on every roof side. */
#define kHutX0 23
#define kHutY0 22
#define kHutX1 26
#define kHutY1 24
#define kHutFloorZ 3 /* plateau top; the footprint is flat by construction */
#define kHutWallTopZ 5
#define kHutRoofZ 6

static const signed char kDirs[4][2] = {
    {0, -1},
    {1, 0},
    {0, 1},
    {-1, 0}
};

static int AbsInt(int v)
{
    return v < 0 ? -v : v;
}

static Boolean IsIsland(short x, short y)
{
    int dx = (int)x - kIslandCX;
    int dy = (int)y - kIslandCY;
    int ax = AbsInt(dx);
    int ay = AbsInt(dy);
    int wobble = (((int)x * 5 + (int)y * 3) % 7) - 3;
    int lhs = ax * ax * kIslandRY2 + ay * ay * kIslandRX2;
    int rhs = kIslandRX2 * kIslandRY2 + wobble * 3200;

    return (Boolean)(lhs <= rhs);
}

static Boolean InPlateau(short x, short y)
{
    int dx = AbsInt((int)x - kPlateauCX);
    int dy = AbsInt((int)y - kPlateauCY);
    int wobble = (((int)x * 3 + (int)y * 5) % 5) - 2;

    /* 10 x 8 ellipse: a rounded mound rather than a rectangular terrace.
     * The small wobble keeps its rim from reading as a drawn oval. */
    return (Boolean)(dx * dx * 64 + dy * dy * 100 <= 6400 + wobble * 160);
}

/* 0 when the cell is not part of the ridge, otherwise its surface height.
 * The ridge tops out two blocks above the plateau (z = 5): high enough to
 * break the silhouette, low enough not to mask the rest of the scene. */
static short RidgeHeight(short x, short y)
{
    int dx = AbsInt((int)x - kRidgeCX);
    int dy = AbsInt((int)y - kRidgeCY);
    int wobble = (((int)x * 7 + (int)y * 5) % 5) - 2;
    int d = dx * 2 + dy * 3 + wobble;

    if (d <= 4)
        return 5;
    if (d <= 9)
        return 4;
    if (d <= 14)
        return 3;
    return 0;
}

static Boolean IsSnowCap(short x, short y)
{
    return (Boolean)((x == kRidgeCX && y == kRidgeCY) ||
                     (x == kRidgeCX && y == kRidgeCY + 1));
}

/* The two dark stone blocks left after the snow caps: stone belongs on the
 * silhouette, while the faces the camera sees are cobblestone. */
static Boolean IsStoneCrown(short x, short y)
{
    return (Boolean)((x == kRidgeCX - 1 && y == kRidgeCY) ||
                     (x == kRidgeCX + 1 && y == kRidgeCY));
}

/* Land surface height, or -1 where the coast ellipse does not reach (the
 * lagoon is carved out afterwards).  The ridge wins over the plateau so the
 * massif grows out of the grass instead of being flattened. */
static short TerrainHeight(short x, short y)
{
    short ridge;

    if (!IsIsland(x, y))
        return -1;

    ridge = RidgeHeight(x, y);
    if (ridge > 0)
        return ridge;

    if (InPlateau(x, y))
        return 3;

    return 2;
}

/* A small interior lagoon in the south-west lowland, with a second lobe so
 * its shoreline is not a circle.  It stays clear of the coast: water at the
 * edge of a floating island would read as a hole rather than a pond. */
static Boolean InLagoon(short x, short y)
{
    int dx = (int)x - 16;
    int dy = (int)y - 33;

    if (dx * dx + dy * dy <= 9)
        return true;

    dx = (int)x - 20;
    dy = (int)y - 30;
    if (dx * dx + dy * dy <= 4)
        return true;

    return false;
}

/* Footprint of the scene: the coast ellipse plus the lagoon cut into it.
 * Nothing is written outside this set, so there is no world-sized bedrock
 * plate for the renderer to draw. */
static Boolean IsSceneCell(short x, short y)
{
    if (x < 0 || x >= kWorldX || y < 0 || y >= kWorldY)
        return false;
    return (Boolean)(IsIsland(x, y) || InLagoon(x, y));
}

static void ClearWorld(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, kBlockEmpty);
}

static void FillLandColumn(short x, short y, short top)
{
    short z;

    for (z = 1; z < top; z++)
        World_Set(x, y, z, kBlockDirt);
    World_Set(x, y, top, kBlockGrass);
}

static void BuildTerrain(void)
{
    short x, y, top;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!IsSceneCell(x, y))
                continue;
            top = TerrainHeight(x, y);
            if (top < 0)
                World_Set(x, y, kSeaLevel, kBlockWater);
            else
                FillLandColumn(x, y, top);
        }
    }
}

static void CarveLagoon(void)
{
    short x, y, z;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!InLagoon(x, y))
                continue;
            for (z = kSeaLevel; z < kWorldZ; z++)
                World_Set(x, y, z, kBlockEmpty);
            World_Set(x, y, kSeaLevel, kBlockWater);
        }
    }
}

static Boolean TouchesWater(short x, short y)
{
    short i, nx, ny;

    for (i = 0; i < 4; i++) {
        nx = (short)(x + kDirs[i][0]);
        ny = (short)(y + kDirs[i][1]);
        if (nx < 0 || nx >= kWorldX || ny < 0 || ny >= kWorldY)
            continue;
        if (World_Get(nx, ny, kSeaLevel) == kBlockWater)
            return true;
    }
    return false;
}

/* Deterministic gravel flecks in the sand.  Two co-prime strides keep the
 * patches from lining up into a visible lattice. */
static Boolean IsGravelPatch(short x, short y)
{
    return (Boolean)((((int)x * 5 + (int)y * 11) % 17) == 0 ||
                     (((int)x * 7 + (int)y * 3) % 23) == 0);
}

/* Flatten a land column to a sand shelf one level above the water.  The
 * shelf is what makes the lagoon read as a shore with a visible step rather
 * than a hole cut into the ground. */
static void MakeShelf(short x, short y)
{
    short z;

    for (z = kSeaLevel + 2; z < kWorldZ; z++)
        World_Set(x, y, z, kBlockEmpty);
    World_Set(x, y, kSeaLevel, kBlockSand);
    World_Set(x, y, kSeaLevel + 1,
              IsGravelPatch(x, y) ? kBlockGravel : kBlockSand);
}

/* Every land cell that touches the lagoon gets a sand shelf. */
static void BuildBeaches(void)
{
    short x, y;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!IsIsland(x, y))
                continue;
            if (World_Get(x, y, kSeaLevel) == kBlockWater)
                continue;
            if (!TouchesWater(x, y))
                continue;
            MakeShelf(x, y);
        }
    }
}

static Boolean TouchesOutside(short x, short y)
{
    short i, nx, ny;

    for (i = 0; i < 4; i++) {
        nx = (short)(x + kDirs[i][0]);
        ny = (short)(y + kDirs[i][1]);
        if (!IsSceneCell(nx, ny))
            return true;
    }
    return false;
}

/* Sand around the outer coast, so the island edge is a beach rim rather than
 * a cut-off slab of grass. */
static void BuildCoast(void)
{
    short x, y;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            if (!IsIsland(x, y))
                continue;
            if (World_Get(x, y, kSeaLevel) == kBlockWater)
                continue;
            if (!TouchesOutside(x, y))
                continue;
            MakeShelf(x, y);
        }
    }
}

static Boolean HasLowerNeighbour(short x, short y, short top)
{
    short i, nx, ny;

    for (i = 0; i < 4; i++) {
        nx = (short)(x + kDirs[i][0]);
        ny = (short)(y + kDirs[i][1]);
        if (nx < 0 || nx >= kWorldX || ny < 0 || ny >= kWorldY)
            continue;
        if (World_TopZ(nx, ny) < top)
            return true;
    }
    return false;
}

/* Where a grass terrace is cut by a lower neighbour, the exposed top block
 * becomes dirt: that brown rim is what draws the edge of the plateau. */
static void EdgeGrassWithDirt(void)
{
    short x, y, top;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            top = World_TopZ(x, y);
            if (top < kSeaLevel + 1)
                continue;
            if (World_Get(x, y, top) != kBlockGrass)
                continue;
            if (HasLowerNeighbour(x, y, top))
                World_Set(x, y, top, kBlockDirt);
        }
    }
}

/* Raise the ridge in cobblestone, with snow on its two summit cells and
 * stone only on the two crown cells that shape the silhouette.  Filling the
 * visible side faces with cobble keeps the massif from rendering as a black
 * wedge.  Columns lowered to a coast shelf are skipped: a shore is sand. */
static void BuildRidge(void)
{
    short x, y, z, h;

    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            h = RidgeHeight(x, y);
            if (h <= 0 || World_TopZ(x, y) != h)
                continue;

            for (z = 1; z < h; z++)
                World_Set(x, y, z, kBlockCobble);

            if (IsSnowCap(x, y))
                World_Set(x, y, h, kBlockSnow);
            else if (IsStoneCrown(x, y))
                World_Set(x, y, h, kBlockStone);
            else
                World_Set(x, y, h, kBlockCobble);
        }
    }
}

/* A four-by-three plank hut with log corner posts, a glass window, a
 * doorway facing the camera, a plank roof with a ridge beam, and a brick
 * chimney poking through the roof. */
static void BuildHut(void)
{
    short x, y;

    for (x = kHutX0; x <= kHutX1; x++)
        for (y = kHutY0; y <= kHutY1; y++)
            World_Set(x, y, kHutFloorZ, kBlockPlanks);

    for (x = kHutX0; x <= kHutX1; x++) {
        for (y = kHutY0; y <= kHutY1; y++) {
            Boolean onXEdge = (Boolean)(x == kHutX0 || x == kHutX1);
            Boolean onYEdge = (Boolean)(y == kHutY0 || y == kHutY1);
            short z;

            if (!onXEdge && !onYEdge)
                continue;

            for (z = kHutFloorZ + 1; z <= kHutWallTopZ; z++) {
                if (onXEdge && onYEdge)
                    World_Set(x, y, z, kBlockLog);
                else
                    World_Set(x, y, z, kBlockPlanks);
            }
        }
    }

    /* Front doorway (the +y face is the one the camera sees). */
    World_Set(24, kHutY1, 4, kBlockEmpty);
    World_Set(24, kHutY1, 5, kBlockEmpty);

    /* Window in the +x wall, at eye height on a raised hut. */
    World_Set(kHutX1, 23, 5, kBlockGlass);

    /* Roof: one cell of overhang on all four sides, plus a ridge beam. */
    for (x = kHutX0 - 1; x <= kHutX1 + 1; x++)
        for (y = kHutY0 - 1; y <= kHutY1 + 1; y++)
            World_Set(x, y, kHutRoofZ, kBlockPlanks);

    for (x = kHutX0; x <= kHutX1; x++)
        World_Set(x, 23, kHutRoofZ + 1, kBlockPlanks);

    /* Chimney through the back roof slope, one block proud of the ridge. */
    World_Set(25, kHutY0, kHutRoofZ, kBlockBrick);
    World_Set(25, kHutY0, kHutRoofZ + 1, kBlockBrick);
}

static void MirrorOffset(short variant, int *dx, int *dy)
{
    if (variant & 1)
        *dx = -*dx;
    if (variant & 2)
        *dy = -*dy;
}

static void PutLeaf(short x, short y, short z)
{
    if (World_Get(x, y, z) == kBlockEmpty)
        World_Set(x, y, z, kBlockLeaves);
}

/* Two log blocks and three leaf layers: a wide, ragged skirt at the trunk
 * top, a broad middle, and a sparse cap.  Mirroring the offsets by variant
 * keeps the three trees from reading as copies of one stamp. */
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
    World_Set(x, y, ground + 2, kBlockLog);
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

void Scene_BuildIsland(void)
{
    short x, y;

    ClearWorld();

    /* Bedrock only under the island footprint.  Writing it everywhere would
     * cover the whole 48x48 world in a near-black base plate; cells outside
     * the coast stay empty so the sky is the background. */
    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            if (IsSceneCell(x, y))
                World_Set(x, y, 0, kBlockBedrock);

    BuildTerrain();
    CarveLagoon();
    BuildBeaches();
    BuildCoast();
    EdgeGrassWithDirt();
    BuildRidge();
    BuildHut();

    PlantTree(17, 22, 0);
    PlantTree(24, 30, 3);
    PlantTree(29, 28, 1);
}
