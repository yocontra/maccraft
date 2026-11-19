#ifndef ART_H
#define ART_H

#include <Types.h>

#include "world.h"

/*
 * art.h - the 64-colour palette and the 16x16 block textures.
 *
 * The palette is 16 material hues of four steps each:
 *
 *     index = hue * 4 + step        step 0 = darkest, step 3 = brightest
 *
 * Each hue is one material (grass green, stone grey, brick red, ...), so a
 * hue's four entries are that material's shade ramp.  Face shading therefore
 * never blends colours: Art_ShadeIndex() keeps the hue and only moves along
 * the ramp.  One shade step is one palette entry, which is what keeps the
 * isometric faces pixel-exact and dither-free on an 8-bit Mac.
 *
 * Packing.  The declared return type of Art_RGB is unsigned short, but a
 * 0xRRGGBB value needs 24 bits.  Every legal palette component is 0xNN (both
 * nibbles equal), so a colour is fully described by one nibble per gun:
 * Art_RGB returns packed 0x0RGB, e.g. 0x372 means red 0x33, green 0x77,
 * blue 0x22.  Art_RGBColor expands those nibbles to the 16-bit byte-repeated
 * form QuickDraw wants (n * 0x1111).  The expansion is lossless: n * 0x11 is
 * exactly the original 0xNN component.
 *
 * Textures are 16x16 row-major arrays of palette indices, 256 bytes each,
 * top row first.  Face 0 is the top; faces 1 and 2 share one side texture
 * because the isometric baker mirrors face 2.  Art_Texture never returns
 * NULL: empty, unknown and future block ids all draw as stone, so a corrupt
 * world cell still looks like something solid.
 */

#define kArtPaletteEntries 64
#define kArtHues 16
#define kArtSteps 4

#define kArtTextureSize 16
#define kArtTextureBytes (kArtTextureSize * kArtTextureSize)

/* Palette colour of a ramp entry, packed 0x0RGB (one nibble per gun).
   Out-of-range indices clamp to the ends of the table. */
unsigned short Art_RGB(int index);

/* The same colour expanded for a QuickDraw RGBColor: each component is
   n * 0x1111, i.e. the 8-bit value 0xNN repeated in both bytes.  Any output
   pointer may be NULL. */
void Art_RGBColor(int index, unsigned short *red, unsigned short *green,
                  unsigned short *blue);

/* Snap a lighting factor 0..256 to one of the four steps of the entry's hue.
   256 is step 3 (full sun), 205 is step 2, 154 is step 1, <=100 is step 0.
   The mapping is a fixed four-band quantiser, so it is monotonic in factor
   and stable: the same factor always picks the same step.  For a valid
   palette entry the result stays in that entry's four-value ramp; an
   out-of-range index is first clamped into the palette. */
unsigned char Art_ShadeIndex(unsigned char index, short factor);

/* 256-byte texture for a face.  face 0 = top, 1 = left side, 2 = right side
   (same data as 1; the baker mirrors it).  Never NULL. */
const unsigned char *Art_Texture(BlockID id, short face);

#endif /* ART_H */
