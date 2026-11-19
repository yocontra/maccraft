#ifndef SKY_H
#define SKY_H

#include <Types.h>

/*
 * sky.h - the backdrop behind the world: a vertical gradient, a horizon haze
 * and a few slowly drifting clouds.  Everything is integer maths, so any of
 * it can run on a 68020 without an FPU.
 *
 * The gradient is addressed as vertical bands: band 0 is the top of the view
 * and band bandCount-1 is the horizon.  A band colour is returned packed as
 * 0x0RGB because the 16-bit return value cannot hold the 24 bits of a real
 * 0xRRGGBB; each nibble is one colour gun at Mac-legal granularity.  Expand a
 * nibble n with Sky_BandColor: it yields n*0x1111, i.e. the 8-bit value n*0x11
 * repeated in both bytes.  QuickDraw quantises every gun to eight bits, so values
 * such as 0x1234 would silently change; these never do.
 *
 * The 1-bit display cannot show the gradient at all, so Sky_BandDensity turns
 * it into an ink ramp: the top of the sky is the densest dither and the
 * horizon the sparsest, which reads as aerial perspective in black and white.
 */

/* Cloud drift is periodic in this many pixels: the cloud field repeats every
 * kSkyCloudPeriod pixels, and Sky_CloudShift wraps at that period, so a cloud
 * leaving one side is exactly replaced by the next one in the field and
 * nothing jumps.  One pixel of drift per kSkyCloudTicksPerPixel ticks (about
 * 1.5 s at 60 Hz) is slow enough to read as weather rather than jitter. */
#define kSkyCloudPeriod 128
#define kSkyCloudTicksPerPixel 90

/* Packed 0x0RGB colour of a band.  Out-of-range bands clamp to the nearest
 * end; bandCount <= 1 yields the top colour. */
unsigned short Sky_BandRGB(short band, short bandCount);

/* The same band colour as three RGBColor components (8-bit value repeated in
 * both bytes of each word).  Kept separate from Sky_BandRGB so the drawing and
 * the host tests share one conversion. */
void Sky_BandColor(short band, short bandCount,
                   unsigned short *red, unsigned short *green,
                   unsigned short *blue);

/* Ink density 0..64 for a 1-bit screen: high at the dark top of the sky,
 * falling towards the horizon.  Quantised into coarse steps so the ramp is a
 * dither ladder rather than a set of near-identical patterns. */
short Sky_BandDensity(short band, short bandCount);

/* Horizontal cloud drift in pixels, 0..kSkyCloudPeriod-1. */
short Sky_CloudShift(long ticks);

/* The cloud layout. Exposed so a colour renderer can place the slabs itself:
 * each rectangle is x pixels from the start of the tiled period, y pixels below
 * the top of the sky, and has a shaded underside one pixel below its body. */
short Sky_CloudRectCount(void);
void Sky_CloudRect(short index, short *x, short *y, short *width,
                   short *height, short *undersideY);

/* Width of one tiled cloud period, in pixels. */
short Sky_CloudPeriod(void);

/*
 * Sky_Draw needs QuickDraw: GrafPtr, Rect and Pattern do not exist in the
 * host tests' minimal <Types.h> shim.  The shim marks itself with
 * MACCRAFT_HOST_TYPES_H, so the drawing entry point is omitted on the host
 * (where only the pure above functions are tested) and present on the target.
 */
#ifndef MACCRAFT_HOST_TYPES_H
#include <Quickdraw.h>

/* Paint the whole backdrop into r: banded gradient, horizon haze and drifting
 * clouds.  port may be NULL to draw into the current port. */
void Sky_Draw(GrafPtr port, Rect r, Boolean hasColor, long ticks);
#endif

#endif /* SKY_H */
