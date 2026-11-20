#ifndef BAKE_H
#define BAKE_H

/*
 * bake.h - turn a 16x16 indexed texture into an isometric face sprite.
 *
 * Each face sprite is the face's bounding box as the renderer lays it out;
 * the polygon itself lives in the mask, so the blit never paints a corner
 * that is not part of the face.
 *
 *   face 0, top   : 32x16, a 2:1 diamond with 256 inked pixels.  A 16x16
 *                   texture is sheared onto it one texel per pixel.
 *   face 1, left  : 16x16, a parallelogram 8 rows thick with 128 inked
 *                   pixels.  The texture is sampled 1:1; texels outside the
 *                   sloped band are not drawn.
 *   face 2, right : 16x16, the mirrored parallelogram, texture sampled
 *                   mirrored in x so one authored side wraps the corner.
 *
 * Buffers are caller-owned, row-major, and sized from Bake_FaceWidth() and
 * Bake_FaceHeight(): 512 bytes for the top, 256 for either side.  Every bake
 * clears the whole buffer first, so no stale pixel or mask bit can survive a
 * previous texture.  Nothing here touches QuickDraw state, so baking is safe
 * inside a paint pass and is exercised on the host.
 */

#include <Types.h>

#define kBakeWidth       32
#define kBakeHeight      16
#define kBakePixels      (kBakeWidth * kBakeHeight)

/* A side face is 16 wide and, per column, a full block tall: the vertical edges
 * of the cube are 16 pixels, so the parallelogram sweeps from row 0 on one side
 * to row 8 on the other, 16 rows deep. Its bounding box is therefore 16x24, not
 * 16x16 -- a 16x16 box is a half-height face, which is what made every block
 * read as a squat slab and every side texture look sheared the wrong way. */
#define kBakeSideWidth   16
#define kBakeSideHeight  24
#define kBakeSidePixels  (kBakeSideWidth * kBakeSideHeight)

/* Face indices, matching iso.h / the renderer. */
enum {
    kBakeFaceTop   = 0,
    kBakeFaceLeft  = 1,
    kBakeFaceRight = 2
};

/* Sprite dimensions for a face; unknown faces are treated as the top face. */
short Bake_FaceWidth(short face);
short Bake_FaceHeight(short face);

/*
 * Bake one face of `tex` (256 bytes, row-major 16x16, palette indices) into
 * out and mask.  The texture index is copied verbatim: this is the unlit
 * face, used when the caller already has shaded art.
 */
void Bake_Face(const unsigned char *tex, short face,
               unsigned char *out, unsigned char *mask);

/*
 * Same geometry, but every texel is passed through Art_ShadeIndex(index,
 * factor) before it is stored.  factor is 0..256; the whole face receives one
 * uniform light level, which is what gives Minecraft its flat per-face
 * shading.  Because Art_ShadeIndex only moves along the palette ramp, the
 * result stays pixel art: a darker entry, never a blend.
 */
void Bake_FaceShaded(const unsigned char *tex, short face, short factor,
                     unsigned char *out, unsigned char *mask);

/*
 * Target-only blit.  Draws a baked sprite with its upper-left corner at
 * (left, top) using the palette shared with art.c, skipping pixels whose
 * mask byte is 0.  width/height come from Bake_FaceWidth()/Bake_FaceHeight();
 * the QuickDraw implementation lives in bake_quickdraw.c and is hidden when
 * the Toolbox is not available, so host tests link only bake.c.
 */
#if !defined(MACCRAFT_HOST_TEST) && defined(__MACTYPES__)
#include <Quickdraw.h>

#endif

#endif /* BAKE_H */
