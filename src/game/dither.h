#ifndef DITHER_H
#define DITHER_H

#include <Types.h>

/*
 * dither.h - 4x4 ordered dithering for the four-step palette.
 *
 * The palette offers four tones per material, which is far too coarse to show
 * Minecraft's shadows: a face can only be one of four ramp steps, so shading
 * visibly bands.  Ordered dithering buys intermediate tones instead of extra
 * palette entries: for a shade that lies between two ramp steps, paint one
 * ramp step darker on a fixed proportion of the pixels, spread by the Bayer
 * threshold matrix.  At the size of these faces the result reads as a smooth
 * gradient, and dark seams around corners come out even rather than blocky.
 *
 * Everything is integer and deterministic.  The threshold pattern depends
 * only on (x, y), so a baked sprite is byte-identical on every run and every
 * machine; nothing here needs QuickDraw or a random source.
 */

/* 0 = this pixel keeps the step chosen by the caller, 1 = it takes the next
   ramp step darker.

   level is the share of pixels to darken, in sixteenths: 0 darkens nothing,
   16 darkens everything, and level/16 is the fraction that returns true over
   any area whose dimensions are multiples of four.  Levels outside 0..16 are
   clamped.  The pattern is the standard 4x4 Bayer matrix, so the dark pixels
   are spread evenly instead of clustering. */
Boolean Dither_On(short x, short y, short level);

/* Map a 0..256 light factor to that 0..16 dither level, monotonically:
   factor 256 (full light) yields 0 and factor 0 (no light) yields 16.

   span is how many of the four ramp steps the dither band may cover, 1..3
   (values outside that clamp).  It scales how quickly the level saturates:
   span 1 spends the whole 0..16 over the full factor sweep, span 2 reaches
   16 once the factor falls to 128, and span 3 once it falls to 170.
   The caller pairs this with the base ramp step it selected from the same
   factor, so a span of 3 lets the dither absorb three steps' worth of
   darkening before the base step has to move. */
short Dither_Level(short factor, short span);

#endif /* DITHER_H */
