/* test_blocks.c - host tests for the block appearance tables. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
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

static Boolean SameColor(BlockColor a, BlockColor b)
{
    return (Boolean)(a.red == b.red && a.green == b.green && a.blue == b.blue);
}

static void TestStoneFallback(void)
{
    const BlockLook *stone = Block_Look(kBlockStone);

    CHECK(stone != NULL);
    CHECK(Block_Look(kBlockEmpty) == stone);
    CHECK(Block_Look(16) == stone);
    CHECK(Block_Look(200) == stone);
    CHECK(Block_Look(255) == stone);
}

static void TestDistinctTops(void)
{
    short a, b;

    for (a = kBlockGrass; a <= kBlockKinds; a++) {
        BlockColor one = Block_Look((BlockID)a)->top;

        /* A black top would mean a missing table entry, not a dark block. */
        CHECK(one.red != 0 || one.green != 0 || one.blue != 0);

        for (b = (short)(a + 1); b <= kBlockKinds; b++) {
            BlockColor two = Block_Look((BlockID)b)->top;

            if (SameColor(one, two)) {
                fprintf(stderr, "blocks %d and %d share a top colour\n", a, b);
                sFailures++;
            }
        }
    }
}

static Boolean IsCubeLevel(unsigned short value)
{
    /* The display quantises each gun to these levels; anything between them
       loses a step at draw time. */
    return (Boolean)(value == 0x0000 || value == 0x3333 || value == 0x6666 ||
                     value == 0x9999 || value == 0xCCCC || value == 0xFFFF);
}

static void TestSingleHue(void)
{
    short id;

    /* The light model shades a side; the palette only chooses the hue.  If a
       look darkened its own sides the two factors would multiply and the dark
       kinds would render black. */
    for (id = kBlockGrass; id <= kBlockKinds; id++) {
        const BlockLook *look = Block_Look((BlockID)id);

        CHECK(SameColor(look->top, look->left));
        CHECK(SameColor(look->top, look->right));
    }
}

static void TestCubeLevels(void)
{
    short id;

    for (id = kBlockGrass; id <= kBlockKinds; id++) {
        const BlockLook *look = Block_Look((BlockID)id);

        CHECK(IsCubeLevel(look->top.red));
        CHECK(IsCubeLevel(look->top.green));
        CHECK(IsCubeLevel(look->top.blue));
    }
}

static void TestVariationNeighbours(void)
{
    long x, y, z;
    long pairs = 0;
    long shared = 0;

    for (x = 0; x < 6; x++) {
        for (y = 0; y < 6; y++) {
            for (z = 0; z < 6; z++) {
                short v = Block_Variation(x, y, z);

                CHECK(v >= 0 && v <= 3);

                if (x + 1 < 6) {
                    pairs++;
                    if (Block_Variation(x + 1, y, z) == v)
                        shared++;
                }
                if (y + 1 < 6) {
                    pairs++;
                    if (Block_Variation(x, y + 1, z) == v)
                        shared++;
                }
                if (z + 1 < 6) {
                    pairs++;
                    if (Block_Variation(x, y, z + 1) == v)
                        shared++;
                }
            }
        }
    }

    CHECK(pairs > 0);
    CHECK(shared == 0);

    /* Negative world coordinates must not fall outside 0..3 either. */
    for (x = -3; x <= 2; x++) {
        short v = Block_Variation(x, -x, x + 1);

        CHECK(v >= 0 && v <= 3);
    }
}

static void TestTone(void)
{
    BlockColor base = { 0x8080, 0x8000, 0x4040 };
    BlockColor plus6 = Block_Tone(base, 0);
    BlockColor plus2 = Block_Tone(base, 1);
    BlockColor minus2 = Block_Tone(base, 2);
    BlockColor minus6 = Block_Tone(base, 3);
    BlockColor black = { 0x0000, 0x0000, 0x0000 };
    BlockColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    BlockColor nearWhite = { 0xFEFE, 0xFEFE, 0xFEFE };
    BlockColor dark;
    short v;

    /* Four distinct steps, two brighter and two darker than the base. */
    CHECK(plus6.red > base.red);
    CHECK(plus2.red > base.red);
    CHECK(plus2.red < plus6.red);
    CHECK(minus2.red < base.red);
    CHECK(minus6.red < minus2.red);

    /* The steps are uniform across channels: they only scale. */
    CHECK(plus6.green > base.green);
    CHECK(plus6.blue > base.blue);

    /* Clamped, never wrapped. */
    dark = Block_Tone(white, 0);
    CHECK(dark.red == 0xFFFF);
    CHECK(dark.green == 0xFFFF);
    CHECK(dark.blue == 0xFFFF);

    dark = Block_Tone(nearWhite, 0);
    CHECK(dark.red == 0xFFFF);
    CHECK(dark.green == 0xFFFF);
    CHECK(dark.blue == 0xFFFF);

    /* Black stays black; there is nothing to brighten. */
    for (v = 0; v < 4; v++) {
        dark = Block_Tone(black, v);
        CHECK(dark.red == 0);
        CHECK(dark.green == 0);
        CHECK(dark.blue == 0);
    }
}

static void TestGrainPresent(void)
{
    short id, face, v;

    for (id = kBlockGrass; id <= kBlockKinds; id++) {
        for (face = kBlockFaceTop; face <= kBlockFaceRight; face++) {
            for (v = 0; v < 4; v++) {
                unsigned char grain[8];
                short i;
                int any = 0;

                Block_Grain((BlockID)id, face, (short)v, grain);
                for (i = 0; i < 8; i++) {
                    if (grain[i] != 0)
                        any = 1;
                }
                CHECK(any);
            }
        }
    }
}

static void TestGrainFollowsVariation(void)
{
    short id;

    for (id = kBlockGrass; id <= kBlockKinds; id++) {
        unsigned char v0[8];
        unsigned char v1[8];

        Block_Grain((BlockID)id, kBlockFaceTop, 0, v0);
        Block_Grain((BlockID)id, kBlockFaceTop, 1, v1);
        CHECK(memcmp(v0, v1, sizeof v0) != 0);
    }
}

static void TestGrainFacesDiffer(void)
{
    short id;

    /* Top and side are different stencils for a kind; otherwise the top of a
       column and its flanks would carry the same texture. */
    for (id = kBlockGrass; id <= kBlockKinds; id++) {
        unsigned char top[8];
        unsigned char side[8];

        Block_Grain((BlockID)id, kBlockFaceTop, 0, top);
        Block_Grain((BlockID)id, kBlockFaceLeft, 0, side);
        CHECK(memcmp(top, side, sizeof top) != 0);
    }
}

int main(void)
{
    TestStoneFallback();
    TestDistinctTops();
    TestSingleHue();
    TestCubeLevels();
    TestVariationNeighbours();
    TestTone();
    TestGrainPresent();
    TestGrainFollowsVariation();
    TestGrainFacesDiffer();

    if (sFailures != 0) {
        fprintf(stderr, "%d check(s) failed\n", sFailures);
        return 1;
    }
    return 0;
}
