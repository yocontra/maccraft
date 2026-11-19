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
 * Drawing is kept to one FillRect per band, a horizon strip and a handful of
 * ovals - comfortably under the ~60 QuickDraw calls a 25 MHz machine can
 * afford per frame.  The cloud field repeats every kSkyCloudPeriod pixels and
 * shifts by Sky_CloudShift, so its wrap is seamless.  One stack Pattern is
 * reused for every mono fill; nothing is allocated.
 */

#include "sky.h"

/* Deep blue at the top, pale haze at the horizon.  The horizon's red and green
 * are close to full because that is what makes a blue sky look washed out and
 * warm near the ground; blue still rises from 7 to 12 so it never darkens. */
enum {
    kSkyTopRed = 1,
    kSkyTopGreen = 3,
    kSkyTopBlue = 7,
    kSkyHorizonRed = 13,
    kSkyHorizonGreen = 12,
    kSkyHorizonBlue = 12
};

/* Bands in Sky_Draw.  Ten gives a smooth colour ramp while keeping the fill
 * count low; more bands would only repeat 1-bit densities. */
enum { kSkyBands = 10 };

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

#ifndef MACCRAFT_HOST_TYPES_H

/* Solid ink for colour fills (pattern all ones => foreground colour). */
static Pattern kSolidPattern = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* No ink at all; with patCopy this paints the background white, which is what
 * the horizon strip uses on a 1-bit screen. */
static Pattern kClearPattern = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const RGBColor kHorizonColor = {0xFFFF, 0xFFFF, 0xEEEE};
static const RGBColor kCloudColor = {0xFFFF, 0xF8F8, 0xEEEE};

/* Sparse cloud ink for 1-bit screens: 25% reads as a soft grey against both the
 * dark top and the nearly white horizon. */
enum { kSkyCloudDensity = 16 };

/* Three clouds per period, at different heights and widths so the field does
 * not look like a repeated stamp.  Positions are inside kSkyCloudPeriod. */
enum { kSkyCloudCount = 3 };
static const short kCloudX[kSkyCloudCount] = { 10, 54, 96 };
static const short kCloudY[kSkyCloudCount] = { 4, 11, 7 };  /* /32 of height */
static const short kCloudW[kSkyCloudCount] = { 30, 20, 15 };
static const short kCloudH[kSkyCloudCount] = { 6, 5, 4 };

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
    Pattern fillPat;
    Rect band;
    Rect line;
    RGBColor color;
    unsigned short red, green, blue;
    short height;
    short i;
    short shift;
    long x;

    if (r.bottom <= r.top || r.right <= r.left)
        return;

    GetPort(&savedPort);
    if (port != 0)
        SetPort(port);

    GetPenState(&savedPen);
    PenMode(patCopy);
    height = (short)(r.bottom - r.top);

    /* Gradient: one band per fill.  On colour, the pen pattern is solid and
     * only the foreground colour changes; on 1-bit, only the pattern changes. */
    if (hasColor)
        PenPat(&kSolidPattern);
    fillPat = kSolidPattern;

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
            PenPat(&pat);
            fillPat = pat;
        }
        FillRect(&band, &fillPat);
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
        } else {
            PenPat(&kClearPattern);
            fillPat = kClearPattern;
        }
        FillRect(&line, &fillPat);
    }

    /* Clouds: the field repeats every kSkyCloudPeriod pixels; shift moves it,
     * and because the shift wraps at the same period the pattern is continuous
     * across the wrap.  Start one period left of the view so a cloud whose
     * body straddles the left edge is still drawn. */
    shift = Sky_CloudShift(ticks);

    if (hasColor) {
        RGBForeColor(&kCloudColor);
    } else {
        PatternForDensity(kSkyCloudDensity, &pat);
        PenPat(&pat);
    }

    for (x = (long)r.left - kSkyCloudPeriod; x < (long)r.right;
         x += kSkyCloudPeriod) {
        for (i = 0; i < kSkyCloudCount; i++) {
            Rect cloud;

            cloud.left = (short)(x + kCloudX[i] + shift);
            cloud.top = (short)(r.top + (height * (long)kCloudY[i]) / 32);
            cloud.right = (short)(cloud.left + kCloudW[i]);
            cloud.bottom = (short)(cloud.top + kCloudH[i]);

            if (ClampToSky(&cloud, &r))
                PaintOval(&cloud);
        }
    }

    SetPenState(&savedPen);
    SetPort(savedPort);
}

#endif /* MACCRAFT_HOST_TYPES_H */
