/*
 * sky.c - integer-only sky gradient, horizon haze and drifting clouds.
 *
 * The colour gradient is a straight interpolation in 4-bit gun levels between
 * a deep blue top and a pale, slightly warm horizon.  Interpolating in nibbles
 * rather than 8-bit components keeps every value Mac-legal: a gun becomes
 * nibble*0x1111, so the high and low byte always match.  The blue gun is
 * deliberately monotonic from top to horizon, which keeps the haze reading as
 * distance rather than as a dark band at the horizon.
 *
 * A 1-bit screen cannot show colour, so the same vertical position is mapped
 * to an ink density instead: densest at the top, sparsest at the horizon, and
 * quantised to 8/64 steps so the gradient appears as a ladder of dithers
 * instead of a set of patterns that look identical.  The density ramp is
 * monotonic by construction because it is a rounded linear ramp that is then
 * floored to the quantum, and both operations preserve order.
 *
 * Drawing is one FillRect per band, a horizon strip and two passes over a
 * handful of cloud rectangles (bodies and undersides).  Sky_Draw runs only
 * when the display is invalidated, not every tick, so that cost is paid on
 * pans and resizes rather than on the frame loop.  The cloud field repeats
 * every kSkyCloudPeriod pixels and shifts by Sky_CloudShift, so its wrap is
 * seamless.  One stack Pattern is reused for every mono fill; nothing is
 * allocated.
 */

#include "sky.h"

/* Deep blue at the top, pale haze at the horizon.  The horizon's red and green
 * are close to full because that is what makes a blue sky look washed out and
 * warm near the ground; blue still rises from 7 to 12 so it never darkens. */
enum {
    /* Minecraft's sky is a single mid blue that only pales near the horizon.
     * The original top level (1,3,7) is a deep navy that quantises to black on a
     * 256-colour display, so the top of the sky read as a black band. */
    kSkyTopRed = 4,
    kSkyTopGreen = 7,
    kSkyTopBlue = 12,
    kSkyHorizonRed = 13,
    kSkyHorizonGreen = 12,
    kSkyHorizonBlue = 12
};

/* Bands in Sky_Draw.  Ten bands over a ~590 pixel view meant ~59 pixels of
 * flat colour per step, which reads as stripes; 24 brings a step down to
 * roughly 25 pixels and is the most a 25 MHz machine should paint per
 * invalidate.  The 1-bit densities still quantise to at most seven levels, so
 * mono screens gain resolution without turning the dither into a solid grey. */
enum { kSkyBands = 24 };

/* 1-bit ramp: 75% ink at the top, none at the horizon. */
enum { kSkyTopDensity = 48, kSkyDensityQuantum = 8 };

/* Clamp a band index to the gradient.  A single band is both top and horizon,
 * so it reports the top colour/density; negative bandCount is treated the same
 * way rather than indexing backwards. */
static short ClampBand(short band, short bandCount)
{
    if (bandCount <= 1)
        return 0;
    if (band < 0)
        return 0;
    if (band >= bandCount)
        return bandCount - 1;
    return band;
}

/* Linear interpolation in nibble levels, rounded to nearest.  steps is the
 * number of band intervals (bandCount - 1); zero means a degenerate gradient. */
static short LerpLevel(short from, short to, short band, short steps)
{
    long value;

    if (steps <= 0)
        return from;

    value = (long)from * steps + ((long)to - (long)from) * band;
    if (value >= 0)
        value = (value + steps / 2) / steps;
    else
        value = -((-value + steps / 2) / steps);
    return (short)value;
}

unsigned short Sky_BandRGB(short band, short bandCount)
{
    short level = ClampBand(band, bandCount);
    short steps = (short)(bandCount - 1);
    short red, green, blue;

    red = LerpLevel(kSkyTopRed, kSkyHorizonRed, level, steps);
    green = LerpLevel(kSkyTopGreen, kSkyHorizonGreen, level, steps);
    blue = LerpLevel(kSkyTopBlue, kSkyHorizonBlue, level, steps);

    return (unsigned short)((red << 8) | (green << 4) | blue);
}

void Sky_BandColor(short band, short bandCount,
                   unsigned short *red, unsigned short *green,
                   unsigned short *blue)
{
    unsigned short packed = Sky_BandRGB(band, bandCount);

    /* A nibble repeated in both bytes is exactly the value QuickDraw keeps
     * after quantising a gun to eight bits. */
    *red = (unsigned short)(((packed >> 8) & 0xFu) * 0x1111u);
    *green = (unsigned short)(((packed >> 4) & 0xFu) * 0x1111u);
    *blue = (unsigned short)((packed & 0xFu) * 0x1111u);
}

short Sky_BandDensity(short band, short bandCount)
{
    short level = ClampBand(band, bandCount);
    short steps = (short)(bandCount - 1);
    long density;

    if (steps <= 0)
        return kSkyTopDensity;

    density = ((long)kSkyTopDensity * (steps - level) + steps / 2) / steps;
    density = (density / kSkyDensityQuantum) * kSkyDensityQuantum;

    /* The ramp cannot leave this range, but a caller-supplied bandCount large
     * enough to hit integer limits must still produce a usable pattern. */
    if (density < 0)
        density = 0;
    if (density > 64)
        density = 64;
    return (short)density;
}

short Sky_CloudShift(long ticks)
{
    /* TickCount is an unsigned 32-bit count; a caller that stores it in a long
     * sees it go negative after 2^31 ticks (about 414 days of uptime).  Read it
     * back as unsigned so a long-running machine keeps drifting instead of
     * freezing the clouds at zero. */
    unsigned long t = (unsigned long)ticks;

    return (short)((t / kSkyCloudTicksPerPixel) % kSkyCloudPeriod);
}

/*
 * Cloud geometry.  A Minecraft cloud seen edge-on is a flat slab with hard
 * corners, not a soft blob, so each cloud is a cluster of rectangles in
 * roughly the same row band: widths differ, neighbours adjoin or overlap, and
 * the bottom edges step by a pixel or two so the mass has a ragged outline.
 * Two groups of four and three rectangles sit in one period.  Every rectangle
 * lies wholly inside [0, kSkyCloudPeriod), so tiling the layout at period
 * boundaries and translating the field by Sky_CloudShift never splits a
 * rectangle across the wrap.  y is a fixed offset from the top of the sky:
 * clouds belong near the top and must not scale down with the view.
 */
typedef struct {
    short x;
    short y;
    short width;
    short height;
} CloudRect;

/* Declared here rather than in sky.h: the header stays the drawing contract,
 * while the host test needs the flat layout to verify the tiling. */
short Sky_CloudRectCount(void);
void Sky_CloudRect(short index, short *x, short *y, short *width,
                   short *height, short *undersideY);

static const CloudRect kCloudRects[] = {
    /* Group 1: four adjoining pieces with a stepped right end. */
    { 10, 40, 22, 6 },
    { 28, 42, 18, 5 },
    { 42, 40, 20, 6 },
    { 58, 44, 12, 4 },
    /* Group 2: three pieces, lower and further right, so the sky is not a
     * single repeated stamp. */
    { 76, 86, 18, 5 },
    { 90, 86, 16, 5 },
    { 102, 88, 12, 4 }
};

short Sky_CloudRectCount(void)
{
    return (short)(sizeof kCloudRects / sizeof kCloudRects[0]);
}

void Sky_CloudRect(short index, short *x, short *y, short *width,
                   short *height, short *undersideY)
{
    const CloudRect *rect;
    short count = Sky_CloudRectCount();

    /* Out-of-range indices clamp like the band accessors do; the drawing loop
     * only ever passes valid ones. */
    if (index < 0 || index >= count)
        index = 0;
    rect = &kCloudRects[index];

    *x = rect->x;
    *y = rect->y;
    *width = rect->width;
    *height = rect->height;
    *undersideY = (short)(rect->y + rect->height);
}

#ifndef MACCRAFT_HOST_TYPES_H

/* Solid ink for colour fills (pattern all ones => foreground colour). */
static Pattern kSolidPattern = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* No ink at all; with patCopy this paints the background white.  The 1-bit
 * horizon strip and the cloud slab bodies both use it. */
static Pattern kClearPattern = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const RGBColor kHorizonColor = {0xFFFF, 0xFFFF, 0xEEEE};

/* Flat white slab and a slightly blue-grey shaded underside, all byte-repeated
 * Mac-legal values. */
static const RGBColor kCloudColor = {0xFFFF, 0xFFFF, 0xF4F4};
static const RGBColor kCloudUnderColor = {0xC8C8, 0xC8C8, 0xD8D8};

/* 1-bit clouds: a white body and a 37.5% underside dither, dark enough to read
 * as thickness against both the dense top and the nearly clean horizon. */
enum { kSkyCloudUnderDensity = 24 };

/* Build an 8x8 dither with exactly `density` of its 64 bits set, spread as
 * evenly as the accumulator allows.  A Pattern is eight bytes, MSB = leftmost
 * pixel, so bit i lands in byte i/8 at mask 0x80 >> (i%8). */
static void PatternForDensity(short density, Pattern *pat)
{
    short i;
    short acc = 0;

    for (i = 0; i < 8; i++)
        pat->pat[i] = 0;

    if (density <= 0)
        return;
    if (density >= 64) {
        for (i = 0; i < 8; i++)
            pat->pat[i] = 0xFF;
        return;
    }

    for (i = 0; i < 64; i++) {
        acc = (short)(acc + density);
        if (acc >= 64) {
            acc = (short)(acc - 64);
            pat->pat[i >> 3] |= (unsigned char)(0x80 >> (i & 7));
        }
    }
}

/* Intersect a candidate rect with the sky rect; false when nothing remains.
 * QuickDraw clips to the port, not to r, so an unclamped cloud could smear
 * over the world below the sky. */
static Boolean ClampToSky(Rect *rect, const Rect *sky)
{
    if (rect->top < sky->top)
        rect->top = sky->top;
    if (rect->left < sky->left)
        rect->left = sky->left;
    if (rect->bottom > sky->bottom)
        rect->bottom = sky->bottom;
    if (rect->right > sky->right)
        rect->right = sky->right;

    return rect->left < rect->right && rect->top < rect->bottom;
}

void Sky_Draw(GrafPtr port, Rect r, Boolean hasColor, long ticks)
{
    GrafPtr savedPort;
    PenState savedPen;
    Pattern pat;
    Rect band;
    Rect line;
    Rect cloud;
    RGBColor color;
    unsigned short red, green, blue;
    const Pattern *pattern;
    short height;
    short i;
    short shift;
    short rectCount;
    short cx, cy, cw, ch, uy;
    long x;

    if (r.bottom <= r.top || r.right <= r.left)
        return;

    GetPort(&savedPort);
    if (port != 0)
        SetPort(port);

    GetPenState(&savedPen);
    PenMode(patCopy);
    height = (short)(r.bottom - r.top);

    /* Gradient: one band per fill.  On colour, the fill pattern is solid and
     * only the foreground colour changes; on 1-bit, only the pattern changes. */
    pattern = &kSolidPattern;
    for (i = 0; i < kSkyBands; i++) {
        band.top = (short)(r.top + (height * (long)i) / kSkyBands);
        band.bottom = (short)(r.top + (height * (long)(i + 1)) / kSkyBands);
        band.left = r.left;
        band.right = r.right;

        if (band.bottom <= band.top)
            continue;

        if (hasColor) {
            Sky_BandColor(i, kSkyBands, &red, &green, &blue);
            color.red = red;
            color.green = green;
            color.blue = blue;
            RGBForeColor(&color);
        } else {
            PatternForDensity(Sky_BandDensity(i, kSkyBands), &pat);
            pattern = &pat;
        }
        FillRect(&band, pattern);
    }

    /* Soft horizon: two pale rows at the bottom of the sky. */
    line.top = (short)(r.bottom - 2);
    if (line.top < r.top)
        line.top = r.top;
    line.bottom = r.bottom;
    line.left = r.left;
    line.right = r.right;

    if (line.bottom > line.top) {
        if (hasColor) {
            RGBForeColor(&kHorizonColor);
            FillRect(&line, &kSolidPattern);
        } else {
            FillRect(&line, &kClearPattern);
        }
    }

    /*
     * Clouds: flat slabs tiled every kSkyCloudPeriod pixels and translated by
     * Sky_CloudShift.  Because the shift wraps at the period and every slab
     * fits inside one period, the tiling is continuous across the wrap and no
     * rectangle is ever split.  Start one period left of the view so a mass
     * that crosses the left edge is still drawn.
     *
     * Undersides go down first: a body painted afterwards covers the part of
     * a neighbouring edge that falls inside the mass, leaving only the exposed
     * silhouette and its 1-pixel shaded base.  Bodies go second, all in one
     * colour, so the foreground colour changes twice per draw rather than
     * twice per slab.
     */
    shift = Sky_CloudShift(ticks);
    rectCount = Sky_CloudRectCount();

    if (hasColor) {
        RGBForeColor(&kCloudUnderColor);
        pattern = &kSolidPattern;
    } else {
        PatternForDensity(kSkyCloudUnderDensity, &pat);
        pattern = &pat;
    }

    for (x = (long)r.left - kSkyCloudPeriod; x < (long)r.right;
         x += kSkyCloudPeriod) {
        for (i = 0; i < rectCount; i++) {
            Sky_CloudRect(i, &cx, &cy, &cw, &ch, &uy);

            cloud.left = (short)(x + cx + shift);
            cloud.top = (short)(r.top + uy);
            cloud.right = (short)(cloud.left + cw);
            cloud.bottom = (short)(cloud.top + 1);

            if (ClampToSky(&cloud, &r))
                FillRect(&cloud, pattern);
        }
    }

    if (hasColor) {
        RGBForeColor(&kCloudColor);
        pattern = &kSolidPattern;
    } else {
        pattern = &kClearPattern;
    }

    for (x = (long)r.left - kSkyCloudPeriod; x < (long)r.right;
         x += kSkyCloudPeriod) {
        for (i = 0; i < rectCount; i++) {
            Sky_CloudRect(i, &cx, &cy, &cw, &ch, &uy);

            cloud.left = (short)(x + cx + shift);
            cloud.top = (short)(r.top + cy);
            cloud.right = (short)(cloud.left + cw);
            cloud.bottom = (short)(cloud.top + ch);

            if (ClampToSky(&cloud, &r))
                FillRect(&cloud, pattern);
        }
    }

    SetPenState(&savedPen);
    SetPort(savedPort);
}

#endif /* MACCRAFT_HOST_TYPES_H */
