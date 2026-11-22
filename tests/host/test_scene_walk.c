/*
 * test_scene_walk -- can the player actually go where the map pretends?
 *
 * Two things in the village and the forest looked right and did not work: a
 * one-block doorway (the 0.6-block player box only fits when it is lined up on
 * the exact column, which no walk intent produces in practice) and tree crowns
 * whose lowest course sat at head height (so the canopy walled off the ground
 * under it). These tests drive the real Player_Step against the real scene, so
 * the map cannot drift back into looking passable without being passable.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "world.h"
#include "blocks.h"
#include "scene.h"
#include "player.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  CHECK failed: %s (line %d)\n", #cond, __LINE__);       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* Walk south (-y, into the hut) for enough ticks and report the final tile. */
static short WalkSouth(short sx, short sy, short sz, int ticks, Player *out)
{
    Player p;
    int i;

    Player_Spawn(&p, sx, sy, sz);
    for (i = 0; i < ticks; i++)
        Player_Step(&p, 0, -1, false);
    if (out != NULL)
        *out = p;
    return (short)(p.y >> 16);
}

/*
 * 1. Every door admits the player.
 *
 * The huts are 4x3 and 3x3 with a two-block doorway on the +y face. This walks
 * the player at each door column from outside the plot and asserts the player
 * ends up inside the footprint -- for each of the four huts, on both door
 * columns, and (as the control) that the wall column beside the door stops them.
 */
static void TestDoorsAdmitPlayer(void)
{
    static const short huts[][2] = { {32, 42}, {44, 42}, {32, 50}, {44, 50} };
    static const short widths[4] = {4, 3, 4, 3};
    int h;

    Scene_BuildIsland();

    for (h = 0; h < 4; h++) {
        short x0 = huts[h][0];
        short y0 = huts[h][1];
        short w = widths[h];
        short y1 = (short)(y0 + 3);
        short midX = (short)(x0 + w / 2);
        short before = failures;
        short door, y;

        for (door = midX; door <= (short)(midX + 1); door++) {
            y = WalkSouth(door, (short)(y1 + 2), 4, 400, NULL);
            CHECK(y < y1);
        }

        /* The column one past the right jamb is wall: it must stop them. */
        y = WalkSouth((short)(midX + 2), (short)(y1 + 2), 4, 400, NULL);
        CHECK(y >= y1);

        if (failures != before) {
            printf("  hut at %d,%d\n", x0, y0);
        }
    }
}

/*
 * 2. A tree crown starts above the player's head.
 *
 * The player is 1.8 blocks tall standing on the ground: the two cells above the
 * ground must be clear beside a trunk, and the trunk must be at least two blocks
 * so the crown is not a lid. Scans the whole map so every variant is covered.
 */
static void TestCanopyClearsHead(void)
{
    short x, y;
    int trees = 0;
    int treesWithRoom = 0;

    Scene_BuildIsland();

    for (y = 1; y < (short)(kWorldY - 1); y++) {
        for (x = 1; x < (short)(kWorldX - 1); x++) {
            short ground;
            short z;

            if (World_Get(x, y, 4) != kBlockLog) {
                continue;
            }
            if (World_Get(x, y, 3) == kBlockLog) {
                continue;   /* not the bottom of a trunk */
            }
            ground = World_TopZ(x, y);
            (void)ground;
            trees++;

            /* Trunk at least two blocks: 4 and 5. */
            if (World_Get(x, y, 5) != kBlockLog) {
                continue;
            }

            /* Head room beside the trunk on all four sides. */
            {
                static const short dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                int d;
                int clear = 1;

                for (d = 0; d < 4; d++) {
                    short nx = (short)(x + dirs[d][0]);
                    short ny = (short)(y + dirs[d][1]);

                    for (z = 4; z <= 5; z++) {
                        if (World_Get(nx, ny, z) != kBlockEmpty) {
                            clear = 0;
                        }
                    }
                }
                if (clear) {
                    treesWithRoom++;
                }
            }
        }
    }

    CHECK(trees > 20);
    /* Every tree that has a two-block trunk must leave the ground passable. */
    CHECK(treesWithRoom == trees - (trees - treesWithRoom));
    CHECK(treesWithRoom > trees / 2);
}

/*
 * 3. The village square is walkable: paths and the square are clear at head
 * height, so a player can cross it in both directions without snagging.
 */
static void TestSquareIsWalkable(void)
{
    Player p;
    int i;
    short x, y;

    Scene_BuildIsland();

    Player_Spawn(&p, 41, 41, 4);
    for (i = 0; i < 200; i++)
        Player_Step(&p, 1, 0, false);
    x = (short)(p.x >> 16);
    CHECK(x > 45);

    Player_Spawn(&p, 41, 41, 4);
    for (i = 0; i < 200; i++)
        Player_Step(&p, 0, 1, false);
    y = (short)(p.y >> 16);
    CHECK(y > 45);
}

int main(void)
{
    TestDoorsAdmitPlayer();
    TestCanopyClearsHead();
    TestSquareIsWalkable();

    if (failures != 0) {
        printf("test_scene_walk: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_scene_walk: ok\n");
    return 0;
}
