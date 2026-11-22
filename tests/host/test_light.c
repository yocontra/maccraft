/* test_light.c - host tests for the integer lighting model. */

#include <stdio.h>
#include <stdlib.h>

#include "light.h"
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

static void TestFaceOrder(void)
{
    short top = Light_FaceFactor(kLightFaceTop);
    short left = Light_FaceFactor(kLightFaceLeft);
    short right = Light_FaceFactor(kLightFaceRight);
    short faces[3];
    short i;

    CHECK(top > left);
    CHECK(left > right);
    CHECK(right >= 0);
    CHECK(top <= kLightMax);

    faces[0] = top;
    faces[1] = left;
    faces[2] = right;
    for (i = 0; i < 3; i++) {
        CHECK(faces[i] >= 0);
        CHECK(faces[i] <= kLightMax);
    }

    /* An unclassified face must never be brighter than the darkest one. */
    CHECK(Light_FaceFactor((short)-1) == right);
    CHECK(Light_FaceFactor((short)99) == right);
}

static void ResetEmptyWorld(void)
{
    short x, y, z;

    World_Reset();
    for (x = 0; x < kWorldX; x++)
        for (y = 0; y < kWorldY; y++)
            for (z = 0; z < kWorldZ; z++)
                World_Set(x, y, z, 0);
}

static void TestOcclusionExposed(void)
{
    short face;

    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 0, 1));

    for (face = 0; face < 3; face++) {
        short occ = Light_Occlusion(24, 24, 0, face);

        CHECK(occ >= 0);
        CHECK(occ <= kLightMax);
        CHECK(occ == kLightMax);
    }

    /* Outside the world there are no neighbours either, so an edge block is
       fully exposed rather than accidentally dark. */
    CHECK(Light_Occlusion((short)-1, (short)-1, 0, kLightFaceTop) == kLightMax);
}

static void TestOcclusionSteps(void)
{
    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));

    /* Top face of (24,24,1) samples five cells one level up.  Add them one
       at a time and watch the quantised shade fall. */
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceTop) == 256);

    CHECK(World_Set(25, 24, 2, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceTop) == 236);

    CHECK(World_Set(23, 24, 2, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceTop) == 216);

    CHECK(World_Set(24, 25, 2, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceTop) == 196);

    /* Saturation: more occluders cannot darken the face further. */
    CHECK(World_Set(24, 23, 2, 1));
    CHECK(World_Set(24, 24, 2, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceTop) == 196);
}

static void TestOcclusionSides(void)
{
    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));

    /* Left face has outward normal +y: a block at (24,25,1) sits in front. */
    CHECK(World_Set(24, 25, 1, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceLeft) < kLightMax);
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceRight) == kLightMax);

    /* Right face has outward normal +x. */
    CHECK(World_Set(25, 24, 1, 1));
    CHECK(Light_Occlusion(24, 24, 1, kLightFaceRight) < kLightMax);
}

static void TestOcclusionEnclosed(void)
{
    short enclosedTop, exposedTop, face;

    /* The reset plateau is solid from z=0 to z=1 across 18..29, so (24,24,0)
       is buried on six sides while (24,24,1) is open to the sky. */
    World_Reset();

    enclosedTop = Light_Occlusion(24, 24, 0, kLightFaceTop);
    exposedTop = Light_Occlusion(24, 24, 1, kLightFaceTop);

    CHECK(enclosedTop >= 0);
    CHECK(enclosedTop <= kLightMax);
    CHECK(enclosedTop < exposedTop);
    CHECK(exposedTop == kLightMax);

    for (face = 0; face < 3; face++) {
        short enclosed = Light_Occlusion(24, 24, 0, face);

        CHECK(enclosed >= 0);
        CHECK(enclosed <= kLightMax);
        CHECK(enclosed < Light_Occlusion(24, 24, 1, face));
    }
}

static void TestShade(void)
{
    LightColor white;
    LightColor black;
    LightColor mid;
    LightColor maxed;

    white.red = 0xFFFF;
    white.green = 0xFFFF;
    white.blue = 0xFFFF;

    maxed = Light_Shade(white, kLightMax);
    CHECK(maxed.red == 0xFFFF);
    CHECK(maxed.green == 0xFFFF);
    CHECK(maxed.blue == 0xFFFF);

    black = Light_Shade(white, 0);
    CHECK(black.red == 0);
    CHECK(black.green == 0);
    CHECK(black.blue == 0);

    mid = Light_Shade(white, 128);
    CHECK(mid.red == 0x8000);
    CHECK(mid.green == 0x8000);
    CHECK(mid.blue == 0x8000);

    /* Over-range factors clamp to full brightness instead of overflowing. */
    maxed = Light_Shade(white, 300);
    CHECK(maxed.red == 0xFFFF);
    CHECK(maxed.green == 0xFFFF);
    CHECK(maxed.blue == 0xFFFF);
    maxed = Light_Shade(white, (short)2000);
    CHECK(maxed.red == 0xFFFF);

    /* Negative factors are treated as no light. */
    black = Light_Shade(white, (short)-7);
    CHECK(black.red == 0);
    CHECK(black.green == 0);
    CHECK(black.blue == 0);

    /* A canonical Mac component scales without an off-by-one. */
    mid.red = 0xCCCC;
    mid.green = 0;
    mid.blue = 0x3333;
    mid = Light_Shade(mid, 128);
    CHECK(mid.red == 0x6666);
    CHECK(mid.green == 0);
    CHECK(mid.blue == 0x199A);
}

static void TestCombine(void)
{
    short a, b;

    CHECK(Light_Combine(kLightMax, kLightMax) == kLightMax);
    CHECK(Light_Combine(kLightMax, 0) == 0);
    CHECK(Light_Combine(0, kLightMax) == 0);
    CHECK(Light_Combine(128, 128) == 64);
    CHECK(Light_Combine(256, 128) == 128);

    /* Over-range inputs clamp, never wrap. */
    CHECK(Light_Combine(400, 400) == kLightMax);
    CHECK(Light_Combine((short)-5, kLightMax) == 0);

    /* Monotonic in each argument and always inside 0..256. */
    for (a = 0; a <= kLightMax; a += 16) {
        short previous = 0;

        for (b = 0; b <= kLightMax; b += 16) {
            short combined = Light_Combine(a, b);

            CHECK(combined >= 0);
            CHECK(combined <= kLightMax);
            CHECK(combined >= previous);
            previous = combined;
        }
    }
}

static void TestFaceCornersOpen(void)
{
    short corners[kLightCorners];
    short face, i;

    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));

    for (face = 0; face < 3; face++) {
        Light_FaceCorners(24, 24, 1, face, corners);
        for (i = 0; i < kLightCorners; i++) {
            CHECK(corners[i] == kLightMax);
        }
    }

    /* Outside the world nothing occludes either edge block. */
    Light_FaceCorners((short)-1, (short)-1, 0, kLightFaceTop, corners);
    for (i = 0; i < kLightCorners; i++)
        CHECK(corners[i] == kLightMax);
}

static void TestFaceCornersTop(void)
{
    short corners[kLightCorners];

    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));

    /* (23,24,2) touches the top face's top corner and its left corner. */
    CHECK(World_Set(23, 24, 2, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceTop, corners);
    CHECK(corners[0] == 128);
    CHECK(corners[1] == 256);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);

    /* A second neighbour boxes the top corner in completely and leaves the
       right corner half lit. */
    CHECK(World_Set(24, 23, 2, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceTop, corners);
    CHECK(corners[0] == 0);
    CHECK(corners[1] == 128);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);

    /* A solid cell directly above the block would hide the whole face; it is
       not part of the corner samples and must not change them. */
    CHECK(World_Set(24, 24, 2, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceTop, corners);
    CHECK(corners[0] == 0);
    CHECK(corners[1] == 128);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);
}

static void TestFaceCornersSides(void)
{
    short corners[kLightCorners];

    /* Left face, outward normal +y. */
    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));
    CHECK(World_Set(23, 25, 1, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceLeft, corners);
    CHECK(corners[0] == 128);
    CHECK(corners[1] == 256);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);

    CHECK(World_Set(24, 25, 2, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceLeft, corners);
    CHECK(corners[0] == 0);
    CHECK(corners[1] == 128);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);

    /* Right face, outward normal +x. */
    ResetEmptyWorld();
    CHECK(World_Set(24, 24, 1, 1));
    CHECK(World_Set(25, 24, 2, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceRight, corners);
    CHECK(corners[0] == 128);
    CHECK(corners[1] == 128);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 256);

    CHECK(World_Set(25, 23, 1, 1));
    Light_FaceCorners(24, 24, 1, kLightFaceRight, corners);
    CHECK(corners[0] == 0);
    CHECK(corners[1] == 128);
    CHECK(corners[2] == 256);
    CHECK(corners[3] == 128);
}

static void TestSampleCorners(void)
{
    short all[kLightCorners] = { 256, 256, 256, 256 };
    short varied[kLightCorners] = { 0, 64, 128, 256 };
    short dark[kLightCorners] = { 0, 256, 256, 256 };
    short dim[kLightCorners] = { 128, 256, 256, 256 };
    short mid, u, previous;

    /* Four full corners stay full everywhere, including dead centre. */
    CHECK(Light_SampleCorners(all, 0, 0) == 256);
    CHECK(Light_SampleCorners(all, 128, 128) == 256);
    CHECK(Light_SampleCorners(all, 255, 255) == 256);
    CHECK(Light_SampleCorners(all, 17, 231) == 256);

    /* The four samples are exact at their own corners. */
    CHECK(Light_SampleCorners(varied, 0, 0) == 0);
    CHECK(Light_SampleCorners(varied, 255, 0) == 64);
    CHECK(Light_SampleCorners(varied, 255, 255) == 128);
    CHECK(Light_SampleCorners(varied, 0, 255) == 256);

    mid = Light_SampleCorners(varied, 128, 128);
    CHECK(mid > 0);
    CHECK(mid < 256);

    /* Sampling towards the one dark corner only ever gets darker. */
    previous = 0;
    for (u = 0; u <= 255; u += 3) {
        short sample = Light_SampleCorners(dark, u, 0);

        CHECK(sample >= previous);
        CHECK(sample >= 0);
        CHECK(sample <= 256);
        previous = sample;
    }
    previous = 0;
    for (u = 0; u <= 255; u += 3) {
        short sample = Light_SampleCorners(dark, 0, u);

        CHECK(sample >= previous);
        previous = sample;
    }

    /* Lowering one corner can never brighten a sample. */
    CHECK(Light_SampleCorners(dim, 128, 128) <
          Light_SampleCorners(all, 128, 128));

    /* Out-of-range coordinates and corner values clamp. */
    CHECK(Light_SampleCorners(varied, (short)-10, 0) ==
          Light_SampleCorners(varied, 0, 0));
    CHECK(Light_SampleCorners(varied, (short)300, 0) ==
          Light_SampleCorners(varied, 255, 0));
    CHECK(Light_SampleCorners(all, (short)-1, (short)300) == 256);
}

int main(void)
{
    TestFaceOrder();
    TestOcclusionExposed();
    TestOcclusionSteps();
    TestOcclusionSides();
    TestOcclusionEnclosed();
    TestShade();
    TestCombine();
    TestFaceCornersOpen();
    TestFaceCornersTop();
    TestFaceCornersSides();
    TestSampleCorners();

    if (sFailures != 0) {
        fprintf(stderr, "test_light: %d failure(s)\n", sFailures);
        return EXIT_FAILURE;
    }
    printf("test_light: ok\n");
    return EXIT_SUCCESS;
}
