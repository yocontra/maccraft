#ifndef TEXTURE_H
#define TEXTURE_H

#include <Types.h>
#include <Quickdraw.h>

#include "world.h"

/*
 * texture.h - per-face two-tone textures for the isometric blocks.
 *
 * Every face is drawn in two passes: a base fill, then a grain pattern inset
 * by one pixel so it never smears over the 1 px outline.  The base pass tells
 * the display type apart: on a colour screen the renderer sets
 * Texture_BaseColor() and fills the face with an all-ones pattern (RGBForeColor
 * + FillPoly), so the face is a solid colour; on a 1-bit screen it fills with
 * Texture_BasePattern() instead, whose 1-bits are black, so the pattern itself
 * is the shade (12.5% top, 25% right, 50% left - top lightest, left darkest).
 * The grain pass is the same on both: set Texture_GrainColor() and fill the
 * inset quad with Texture_GrainPattern().  On a colour screen the grain is a
 * second shade of the base colour; on a 1-bit screen the grain ink is black and
 * the mono pattern is coarser, so it darkens the base dither into a visible
 * fibre/strata/pebble texture.  Nothing here touches QuickDraw state: these are
 * pure lookups into static data.
 */

/* Face indices: 0 = top, 1 = left, 2 = right. */
enum { kFaceTop = 0, kFaceLeft = 1, kFaceRight = 2 };

/* Base fill for a face. On a colour display the renderer fills with this
   colour (all-ones pattern + RGBForeColor); on a 1-bit display it uses
   BasePattern() instead. */
const RGBColor *Texture_BaseColor(BlockID id, short face);
Pattern        *Texture_BasePattern(BlockID id, short face);

/* Texture grain: a 8x8 pattern drawn over the base with a contrasting colour.
   Two variants because a 1-bit screen needs a coarser, darker grain than a
   colour screen, where the second colour is a shade of the base. */
Pattern        *Texture_GrainPattern(BlockID id, short face, Boolean hasColor);
const RGBColor *Texture_GrainColor(BlockID id, short face, Boolean hasColor);

/* How many pixels to inset the grain area from the face edges, so the grain
   does not smear over the 1px outline. Always 1. */
short Texture_GrainInset(BlockID id, short face);

/* Scene background, behind the world: a light blue-grey on colour displays,
   a 25% dither on 1-bit displays. */
const RGBColor *Texture_SkyColor(void);   /* light blue-grey */
Pattern        *Texture_SkyPattern(void); /* 25% pattern for 1-bit screens */

#endif /* TEXTURE_H */
