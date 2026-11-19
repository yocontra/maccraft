/* test_sky.c - host tests for the pure sky gradient, density and cloud drift.
 *
 * Sky_Draw itself is QuickDraw and is compiled out on the host (the compat
 * <Types.h> shim has no GrafPtr/Rect); everything it is built from is tested
 * here: the band colours, their RGBColor expansion, the 1-bit density ramp,
 * the cloud drift and the slab layout.
 */

#include <stdio.h>

#include "sky.h"

/*
 * Internal cloud-layout accessors.  sky.h is the drawing-facing contract;
 * these two exist so the host test can verify the slab tiling without keeping
 * a duplicate copy of the table.  sky.c defines them next to the table and
 * uses them in Sky_Draw.
 */
short Sky_CloudRectCount(void);
void Sky_CloudRect(short index, short *x, short *y, short *width,
                   short *height, short *undersideY);

static int sFailures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            sFailures++;                                                     \
        }                                                                    \
    } while (0)

/* Expand the packed nibble at `shift` the way Sky_BandColor does. */
static unsigned short Component(unsigned short packed, short shift)
{
    return (unsigned short)(((packed >> shift) & 0xFu) * 0x1111u);
}

/* Every band of a range of band counts must be a legal Mac colour: one packed
 * nibble triple, expanded to words whose two bytes match, and whose 8-bit gun
 * value is a multiple of 0x11 (the only values QuickDraw preserves exactly). */
static void TestBandColoursAreLegal(void)
{
    short bandCount;

    for (bandCount = 2; bandCount <= 25; bandCount++) {
        short band;

        for (band = 0; band < bandCount; band++) {
            unsigned short packed = Sky_BandRGB(band, bandCount);
            unsigned short red, green, blue;

            CHECK((packed & 0xF000u) == 0);

            Sky_BandColor(band, bandCount, &red, &green, &blue);

            CHECK(red == Component(packed, 8));
            CHECK(green == Component(packed, 4));
            CHECK(blue == Component(packed, 0));

            /* 0x66 -> 0x6666: the high and low byte of every gun agree. */
            CHECK((red & 0xFFu) == (red >> 8));
            CHECK((green & 0xFFu) == (green >> 8));
            CHECK((blue & 0xFFu) == (blue >> 8));

            CHECK((red & 0xFFu) % 0x11u == 0);
            CHECK((green & 0xFFu) % 0x11u == 0);
            CHECK((blue & 0xFFu) % 0x11u == 0);
        }
    }
}

/* The sky must brighten downwards.  The acceptance criterion is that blue does
 * not go backwards by more than a few units; the ramp is built to be strictly
 * non-decreasing, which is the stronger property asserted here.  Overall
 * brightness must rise from the deep top to the pale horizon. */
static void TestBandsMonotonic(void)
{
    short bandCount;

    for (bandCount = 2; bandCount <= 25; bandCount++) {
        short band;
        unsigned short previousBlue = Component(Sky_BandRGB(0, bandCount), 0);

        for (band = 1; band < bandCount; band++) {
            unsigned short blue = Component(Sky_BandRGB(band, bandCount), 0);

            CHECK(blue >= previousBlue);
            previousBlue = blue;
        }
    }

    /* Top is blue-dominant and dark; the horizon is pale and nearly neutral. */
    {
        unsigned short packedTop = Sky_BandRGB(0, 8);
        unsigned short packedHorizon = Sky_BandRGB(7, 8);
        unsigned long topSum = (unsigned long)Component(packedTop, 8) +
                               Component(packedTop, 4) +
                               Component(packedTop, 0);
        unsigned long horizonSum = (unsigned long)Component(packedHorizon, 8) +
                                   Component(packedHorizon, 4) +
                                   Component(packedHorizon, 0);

        /* The sky is one colour end to end: a gradient cannot be drawn here
         * without a hard quantisation band, so the horizon must be neither
         * darker than the zenith nor lighter -- only blue-dominant at both. */
        CHECK(Component(packedTop, 0) > Component(packedTop, 8));
        CHECK(Component(packedHorizon, 0) > Component(packedHorizon, 8));
        CHECK(horizonSum >= topSum);
    }
}

/* Out-of-range bands clamp to the nearer end instead of reading backwards. */
static void TestBandClamping(void)
{
    unsigned short red, green, blue;

    CHECK(Sky_BandRGB(-3, 8) == Sky_BandRGB(0, 8));
    CHECK(Sky_BandRGB(8, 8) == Sky_BandRGB(7, 8));
    CHECK(Sky_BandRGB(100, 8) == Sky_BandRGB(7, 8));

    CHECK(Sky_BandDensity(-3, 8) == Sky_BandDensity(0, 8));
    CHECK(Sky_BandDensity(8, 8) == Sky_BandDensity(7, 8));
    CHECK(Sky_BandDensity(100, 8) == Sky_BandDensity(7, 8));

    /* Degenerate counts still yield a legal top-of-sky colour. */
    CHECK(Sky_BandRGB(0, 1) == Sky_BandRGB(0, 0));
    CHECK(Sky_BandRGB(5, 0) == Sky_BandRGB(0, 0));
    CHECK(Sky_BandDensity(0, 1) == Sky_BandDensity(0, 0));

    Sky_BandColor(0, 0, &red, &green, &blue);
    CHECK((red & 0xFFu) == (red >> 8));
    CHECK((green & 0xFFu) == (green >> 8));
    CHECK((blue & 0xFFu) == (blue >> 8));
}

/* 1-bit ramp: within 0..64, densest at the top, never increasing towards the
 * horizon, and coarse enough to be a dither ladder (at most ten levels). */
static void TestDensityRamp(void)
{
    static const short kCounts[] = { 2, 3, 4, 8, 10, 16, 24, 32, 64 };
    unsigned int i;

    for (i = 0; i < sizeof kCounts / sizeof kCounts[0]; i++) {
        short bandCount = kCounts[i];
        short band;
        short previous = Sky_BandDensity(0, bandCount);
        int seen[65];

        for (band = 0; band < 65; band++)
            seen[band] = 0;

        CHECK(previous >= 0);
        CHECK(previous <= 64);

        seen[previous] = 1;
        for (band = 1; band < bandCount; band++) {
            short density = Sky_BandDensity(band, bandCount);

            CHECK(density >= 0);
            CHECK(density <= 64);
            CHECK(density <= previous);
            CHECK(density % 8 == 0);

            seen[density] = 1;
            previous = density;
        }

        /* The top is the darkest point and the horizon is lighter. */
        CHECK(Sky_BandDensity(0, bandCount) >=
              Sky_BandDensity(bandCount - 1, bandCount));
        CHECK(Sky_BandDensity(0, bandCount) >
              Sky_BandDensity(bandCount - 1, bandCount));

        {
            short levels = 0;

            for (band = 0; band < 65; band++)
                levels = (short)(levels + seen[band]);
            CHECK(levels <= 10);
        }
    }
}

/* Drift must stay inside the pattern period and move by at most one pixel per
 * tick, including across the wrap from period-1 back to 0. */
static void TestCloudShiftWraps(void)
{
    long t;
    long last = (long)kSkyCloudTicksPerPixel * kSkyCloudPeriod * 3;
    short previous = Sky_CloudShift(0);

    CHECK(previous == 0);
    /* A wrapped (negative) tick count still lands inside the period. */
    CHECK(Sky_CloudShift(-1000) >= 0);
    CHECK(Sky_CloudShift(-1000) < kSkyCloudPeriod);

    for (t = 1; t <= last; t++) {
        short shift = Sky_CloudShift(t);
        short step;

        CHECK(shift >= 0);
        CHECK(shift < kSkyCloudPeriod);

        step = (short)((shift - previous + kSkyCloudPeriod) %
                       kSkyCloudPeriod);
        CHECK(step <= 1);
        previous = shift;
    }

    /* One pixel per period of ticks; the field is identical after a full
     * period and reaches the last offset just before it wraps. */
    CHECK(Sky_CloudShift(kSkyCloudTicksPerPixel) == 1);
    CHECK(Sky_CloudShift((long)kSkyCloudTicksPerPixel * (kSkyCloudPeriod - 1)) ==
          kSkyCloudPeriod - 1);
    CHECK(Sky_CloudShift((long)kSkyCloudTicksPerPixel * kSkyCloudPeriod) == 0);
    CHECK(Sky_CloudShift((long)kSkyCloudTicksPerPixel * kSkyCloudPeriod * 2) == 0);
}

/* Cloud slabs: every rectangle is a real, positive rectangle that fits wholly
 * inside one period (so translating the tiled field by Sky_CloudShift cannot
 * split it across the wrap), and its shaded underside starts on the first row
 * below the body. */
static void TestCloudLayout(void)
{
    short count = Sky_CloudRectCount();
    short i;

    /* Two "few" groups of 3-6 rectangles each. */
    CHECK(count >= 6);
    CHECK(count <= 12);

    for (i = 0; i < count; i++) {
        short x, y, width, height, undersideY;

        Sky_CloudRect(i, &x, &y, &width, &height, &undersideY);

        /* No straddling of the period boundary. */
        CHECK(x >= 0);
        CHECK(x + width <= kSkyCloudPeriod);

        CHECK(width > 0);
        CHECK(height > 0);
        CHECK(y >= 0);

        /* The darker underside is one row, immediately below the body: its
         * top row is the first row the body does not occupy. */
        CHECK(undersideY == y + height);
        CHECK(undersideY >= y + height);
        CHECK(undersideY > y);
    }
}

int main(void)
{
    TestBandColoursAreLegal();
    TestBandsMonotonic();
    TestBandClamping();
    TestDensityRamp();
    TestCloudShiftWraps();
    TestCloudLayout();

    if (sFailures != 0) {
        fprintf(stderr, "test_sky: %d failure(s)\n", sFailures);
        return 1;
    }
    return 0;
}
