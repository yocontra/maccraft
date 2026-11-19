/*
 * test_bake.c - host tests for the isometric face baker.
 *
 * The tests do not trust the baker's loops: they rebuild the silhouette from
 * the renderer's quads (edge walk for the side bands, half-plane diamond for
 * the top), then check every masked pixel against it.  A separate oracle maps
 * every texture coordinate to its expected pixel, which catches overlapping
 * writes, missing writes and row bleeding.
 */

#define MACCRAFT_HOST_TEST 1

#include <stdio.h>
#include <string.h>

#include "art.h"
#include "bake.h"
#include "blocks.h"

static int sFailures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            sFailures++;                                                     \
        }                                                                    \
    } while (0)

/* ---------------------------------------------------------------- */
/* Independent geometry oracle                                      */
/* ---------------------------------------------------------------- */

/* Top face: the sheet is rotated 45 degrees and squashed 2:1. */
static void OracleTop(short c, short r, short *x, short *y)
{
    *x = (short)(16 + c - r);
    *y = (short)((c + r + 1) / 2);
}

/*
 * Side bands.  The left quad is (0,0) (16,8) (16,16) (0,8), so at column x
 * the top edge is y = x/2 and the bottom edge is y = x/2 + 8.  Membership is
 * decided by the pixel's top-left corner:
 *      2y >= x        (below the top edge)
 *      2y <= x + 15   (above the bottom edge)
 * The right quad is (0,8) (16,0) (16,8) (0,16):
 *      2y >= 16 - x
 *      2y <= 31 - x
 * Both are eight inked rows in every column.  These are written here with
 * integer comparisons, not the baker's divisions.
 */
static unsigned char OracleSideInside(short x, short y, short face)
{
    short twice = (short)(2 * y);

    if (face == kBakeFaceLeft) {
        return (unsigned char)(twice >= x && twice <= x + 15);
    }
    return (unsigned char)(twice >= 16 - x && twice <= 31 - x);
}

static unsigned char OracleSideTexel(const unsigned char *tex, short face,
                                     short x, short y)
{
    short sx = (face == kBakeFaceRight) ? (short)(15 - x) : x;

    return tex[y * 16 + sx];
}

/* ---------------------------------------------------------------- */
/* Shape and coverage                                               */
/* ---------------------------------------------------------------- */

static void TestTopGeometry(const unsigned char *tex)
{
    unsigned char out[kBakePixels];
    unsigned char mask[kBakePixels];
    unsigned char seen[kBakePixels];
    short c, r, x, y, i;
    short count = 0;
    short topX = -1, topY = 99, bottomY = -1, leftX = 99, rightX = -1;

    CHECK(Bake_FaceWidth(kBakeFaceTop) == 32);
    CHECK(Bake_FaceHeight(kBakeFaceTop) == 16);

    memset(out, 0xAA, sizeof out);
    memset(mask, 0xAA, sizeof mask);

    Bake_Face(tex, kBakeFaceTop, out, mask);

    for (i = 0; i < kBakePixels; i++) {
        CHECK(mask[i] == 0 || mask[i] == 1);
        if (mask[i]) {
            count++;
        }
    }
    CHECK(count == 256);

    /* 256 texels map onto 256 distinct pixels, each holding its texel. */
    memset(seen, 0, sizeof seen);
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            OracleTop(c, r, &x, &y);
            CHECK(x >= 0 && x < kBakeWidth);
            CHECK(y >= 0 && y < kBakeHeight);
            CHECK(mask[y * kBakeWidth + x] == 1);
            CHECK(seen[y * kBakeWidth + x] == 0);
            seen[y * kBakeWidth + x] = 1;
            CHECK(out[y * kBakeWidth + x] == tex[r * 16 + c]);

            if (y < topY) { topY = y; topX = x; }
            if (y > bottomY) { bottomY = y; }
            if (x < leftX) { leftX = x; }
            if (x > rightX) { rightX = x; }
        }
    }
    CHECK(topX == 16 && topY == 0);
    CHECK(bottomY == 15);
    CHECK(leftX == 1);
    CHECK(rightX == 31);

    /* Independent silhouette: every inked pixel lies inside the 2:1 diamond
     * with corners (16,0), (32,8), (16,16), (0,8):
     *   top-left    x >= 16 - 2y       top-right    x <= 16 + 2y
     *   bottom-left x >= 2y - 16       bottom-right x <= 48 - 2y
     */
    for (y = 0; y < kBakeHeight; y++) {
        short rowCount = 0;
        short first = -1, last = -1;

        for (x = 0; x < kBakeWidth; x++) {
            if (!mask[y * kBakeWidth + x]) {
                continue;
            }
            rowCount++;
            if (first < 0) {
                first = x;
            }
            last = x;
            CHECK(x >= 16 - 2 * y);
            CHECK(x <= 16 + 2 * y);
            CHECK(x >= 2 * y - 16);
            CHECK(x <= 48 - 2 * y);
        }
        if (rowCount > 0) {
            CHECK(last - first + 1 == rowCount); /* contiguous run */
        }
    }
}

static void TestSideGeometry(const unsigned char *tex, short face)
{
    unsigned char out[kBakeSidePixels];
    unsigned char mask[kBakeSidePixels];
    short x, y, i;
    short count = 0;
    short firstY[16], lastY[16];

    CHECK(Bake_FaceWidth(face) == 16);
    CHECK(Bake_FaceHeight(face) == 16);

    memset(out, 0xAA, sizeof out);
    memset(mask, 0xAA, sizeof mask);

    Bake_Face(tex, face, out, mask);

    for (i = 0; i < kBakeSidePixels; i++) {
        CHECK(mask[i] == 0 || mask[i] == 1);
        if (mask[i]) {
            count++;
        }
    }
    CHECK(count == 128); /* half of the 16x16 bounding box */

    /* Exactly the eight rows between the two sloped edges in each column,
     * and nothing outside them. */
    for (x = 0; x < 16; x++) {
        short n = 0;

        firstY[x] = -1;
        lastY[x] = -1;
        for (y = 0; y < 16; y++) {
            unsigned char expected = OracleSideInside(x, y, face);
            unsigned char inked = mask[y * 16 + x];

            CHECK(inked == expected);
            if (inked) {
                if (firstY[x] < 0) {
                    firstY[x] = y;
                }
                lastY[x] = y;
                n++;
            }
        }
        CHECK(n == 8);
        CHECK(lastY[x] - firstY[x] + 1 == 8);
    }

    /* Texel values survive 1:1 inside the band; outside the band the buffer
     * stays cleared. */
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            if (OracleSideInside(x, y, face)) {
                CHECK(out[y * 16 + x] == OracleSideTexel(tex, face, x, y));
            }
            else {
                CHECK(out[y * 16 + x] == 0);
            }
        }
    }

    /* The four extreme points of the band, and their outside neighbours. */
    if (face == kBakeFaceLeft) {
        CHECK(mask[0 * 16 + 0] == 1);   /* (0,0) */
        CHECK(mask[7 * 16 + 0] == 1);   /* (0,7) */
        CHECK(mask[8 * 16 + 15] == 1);  /* (15,8) */
        CHECK(mask[15 * 16 + 15] == 1); /* (15,15) */
        CHECK(mask[8 * 16 + 0] == 0);   /* above the band at x=0 */
    }
    else {
        CHECK(mask[8 * 16 + 0] == 1);   /* (0,8) */
        CHECK(mask[15 * 16 + 0] == 1);  /* (0,15) */
        CHECK(mask[1 * 16 + 15] == 1);  /* (15,1) */
        CHECK(mask[8 * 16 + 15] == 1);  /* (15,8) */
        CHECK(mask[0 * 16 + 15] == 0);  /* above the band at x=15 */
    }
}

static void TestGeometry(void)
{
    short id;

    for (id = 1; id <= kBlockKinds; id++) {
        short face;

        for (face = 0; face < 3; face++) {
            const unsigned char *tex = Art_Texture((BlockID)id, face);

            if (face == kBakeFaceTop) {
                TestTopGeometry(tex);
            }
            else {
                TestSideGeometry(tex, face);
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* No row bleeding / texel distinctness                             */
/* ---------------------------------------------------------------- */

static void TestDistinctNeighbours(void)
{
    unsigned char tex[256];
    unsigned char out[kBakePixels];
    unsigned char mask[kBakePixels];
    short face, c, r;

    /* Step-3 palette entries only, so an unshaded bake copies them exactly. */
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            tex[r * 16 + c] = (unsigned char)(4 * ((c + 3 * r) % 16) + 3);
        }
    }

    /* Top face: adjacent texture pixels must land on different screen
     * pixels and keep both values. */
    Bake_Face(tex, kBakeFaceTop, out, mask);
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            short x0, y0, x1, y1;

            OracleTop(c, r, &x0, &y0);
            if (c + 1 < 16) {
                OracleTop((short)(c + 1), r, &x1, &y1);
                CHECK(x0 != x1 || y0 != y1);
                if (tex[r * 16 + c] != tex[r * 16 + c + 1]) {
                    CHECK(out[y0 * 32 + x0] != out[y1 * 32 + x1]);
                }
            }
            if (r + 1 < 16) {
                OracleTop(c, (short)(r + 1), &x1, &y1);
                CHECK(x0 != x1 || y0 != y1);
                if (tex[r * 16 + c] != tex[(r + 1) * 16 + c]) {
                    CHECK(out[y0 * 32 + x0] != out[y1 * 32 + x1]);
                }
            }
        }
    }

    /* Sides: 1:1 mapping, so any two inked texels with different values must
     * stay different, including vertical neighbours and the mirrored side. */
    for (face = kBakeFaceLeft; face <= kBakeFaceRight; face++) {
        Bake_Face(tex, face, out, mask);
        for (r = 0; r < 16; r++) {
            for (c = 0; c < 16; c++) {
                if (!OracleSideInside(c, r, face)) {
                    continue;
                }
                if (c + 1 < 16 && OracleSideInside((short)(c + 1), r, face)) {
                    if (OracleSideTexel(tex, face, c, r) !=
                        OracleSideTexel(tex, face, (short)(c + 1), r)) {
                        CHECK(out[r * 16 + c] != out[r * 16 + c + 1]);
                    }
                }
                if (r + 1 < 16 && OracleSideInside(c, (short)(r + 1), face)) {
                    if (OracleSideTexel(tex, face, c, r) !=
                        OracleSideTexel(tex, face, c, (short)(r + 1))) {
                        CHECK(out[r * 16 + c] != out[(r + 1) * 16 + c]);
                    }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* Shading                                                          */
/* ---------------------------------------------------------------- */

static void TestShading(void)
{
    short id, face, i;
    int sawDarker = 0;

    for (id = 1; id <= kBlockKinds; id++) {
        for (face = 0; face < 3; face++) {
            const unsigned char *tex = Art_Texture((BlockID)id, face);
            short factors[5] = {0, 100, 154, 205, 256};
            unsigned char chain[5][kBakePixels];
            unsigned char mask[kBakePixels];
            unsigned char maskRef[kBakePixels];
            short n = (short)(Bake_FaceWidth(face) * Bake_FaceHeight(face));
            short f;

            for (f = 0; f < 5; f++) {
                Bake_FaceShaded(tex, face, factors[f], chain[f], mask);
                if (f == 0) {
                    memcpy(maskRef, mask, sizeof maskRef);
                }
                CHECK(memcmp(maskRef, mask, (size_t)n) == 0);
            }

            /* Palette entries are ramps of four (hue*4 + step), so the low
             * two bits are the brightness step.  A lower factor may never
             * land on a brighter step, and the hue must not move. */
            for (i = 0; i < n; i++) {
                if (!maskRef[i]) {
                    continue;
                }
                CHECK((chain[0][i] & 3) <= (chain[1][i] & 3));
                CHECK((chain[1][i] & 3) <= (chain[2][i] & 3));
                CHECK((chain[2][i] & 3) <= (chain[3][i] & 3));
                CHECK((chain[3][i] & 3) <= (chain[4][i] & 3));
                CHECK((chain[0][i] >> 2) == (chain[4][i] >> 2));
                if ((chain[2][i] & 3) < (chain[4][i] & 3)) {
                    sawDarker = 1;
                }
            }
        }
    }
    CHECK(sawDarker);
}

/* ---------------------------------------------------------------- */
/* Determinism                                                      */
/* ---------------------------------------------------------------- */

static void TestDeterminism(void)
{
    unsigned char a[kBakePixels], b[kBakePixels];
    unsigned char ma[kBakePixels], mb[kBakePixels];
    short id, face;

    for (id = 1; id <= kBlockKinds; id++) {
        for (face = 0; face < 3; face++) {
            const unsigned char *tex = Art_Texture((BlockID)id, face);
            short n = (short)(Bake_FaceWidth(face) * Bake_FaceHeight(face));

            memset(a, 0x00, sizeof a);
            memset(b, 0xFF, sizeof b);
            memset(ma, 0x00, sizeof ma);
            memset(mb, 0xFF, sizeof mb);

            Bake_FaceShaded(tex, face, 154, a, ma);
            Bake_FaceShaded(tex, face, 154, b, mb);

            CHECK(memcmp(a, b, (size_t)n) == 0);
            CHECK(memcmp(ma, mb, (size_t)n) == 0);
        }
    }
}

/* ---------------------------------------------------------------- */
/* Palette legality                                                 */
/* ---------------------------------------------------------------- */

static void TestPaletteRange(void)
{
    unsigned char out[kBakePixels];
    unsigned char mask[kBakePixels];
    short id, face, i;

    for (id = 1; id <= kBlockKinds; id++) {
        for (face = 0; face < 3; face++) {
            const unsigned char *tex = Art_Texture((BlockID)id, face);
            short n = (short)(Bake_FaceWidth(face) * Bake_FaceHeight(face));

            Bake_Face(tex, face, out, mask);
            for (i = 0; i < n; i++) {
                CHECK(out[i] < kArtPaletteEntries);
            }
        }
    }
}

int main(void)
{
    TestGeometry();
    TestDistinctNeighbours();
    TestShading();
    TestDeterminism();
    TestPaletteRange();

    if (sFailures != 0) {
        fprintf(stderr, "test_bake: %d failure(s)\n", sFailures);
        return 1;
    }
    return 0;
}
