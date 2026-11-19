/*
 * bake.c - isometric face baker.
 *
 * Geometry comes straight from the quads DrawBlock paints; a baked sprite is
 * the face's bounding box with the polygon rasterised into the mask.
 *
 * Top face (kBakeWidth x kBakeHeight = 32x16)
 * -------------------------------------------
 * DrawBlock draws (sx, sy-8) (sx+16, sy) (sx, sy+8) (sx-16, sy): a 2:1
 * diamond, 256 pixels inside.  A 16x16 texture has 256 texels, so the sheet
 * is rotated 45 degrees and squashed 2:1 without resampling:
 *
 *      x = 16 + c - r
 *      y = (c + r + 1) / 2
 *
 * The c-r term spans 31 columns (1..31) and the rounding of (c+r)/2 spreads
 * the 16 texture rows over the 16 sprite rows.  The parity of c-r and of c+r
 * agree, so exactly one texel with c+r = 2y-1 or c+r = 2y writes each pixel.
 * The silhouette is the diamond above; its extreme pixels are (16,0),
 * (31,8), (16,15) and (1,8).  The ideal polygon corner is (0,8), but a
 * 16-texel-wide shear can only span 31 columns, so column 0 is transparent
 * for this one face.  That half-pixel sliver disappears under the face shade
 * steps and keeps the texel count exactly 256.
 *
 * Side faces (kBakeSideWidth x kBakeSideHeight = 16x16, 128 pixels)
 * ----------------------------------------------------------------
 * The left quad is (0,0) (16,8) (16,16) (0,8) in sprite-local coordinates
 * (left = sx-16, top = sy): a parallelogram only eight rows thick at any
 * column, exactly half of the 16x16 box.  The right quad is (0,8) (16,0)
 * (16,8) (0,16).  Measuring a pixel by its top-left corner, membership is
 *
 *      left :  x     <= 2y <= x + 15
 *      right:  16 - x <= 2y <= 31 - x
 *
 * which is eight inked rows in every column and therefore 16*8 = 128 set
 * mask bytes - half of the box, as the quads demand.  Because the texture is
 * 16x16 and the sprite is also 16x16, every inked pixel samples the texel
 * with the same coordinates: no scaling, no blending.  The sloped edges are
 * purely a mask concern; texels outside the parallelogram are simply not
 * drawn, which is what makes the face read as a slanted board.
 *
 * The right face samples the side texture mirrored in x, so one authored
 * side wraps around the block corner instead of repeating identically.
 */

#include "bake.h"
#include "art.h"

#define kTexSide 16

/* Clear exactly the face's buffer before writing: masked-off pixels must be
 * zero and the mask must never retain a bit from a previous bake. */
static void ClearFace(short face, unsigned char *out, unsigned char *mask)
{
    short n = (short)(Bake_FaceWidth(face) * Bake_FaceHeight(face));
    short i;

    for (i = 0; i < n; i++) {
        out[i] = 0;
        mask[i] = 0;
    }
}

short Bake_FaceWidth(short face)
{
    return (face == kBakeFaceLeft || face == kBakeFaceRight)
               ? kBakeSideWidth
               : kBakeWidth;
}

short Bake_FaceHeight(short face)
{
    return (face == kBakeFaceLeft || face == kBakeFaceRight)
               ? kBakeSideHeight
               : kBakeHeight;
}

/* Is (x, y) inside the side parallelogram?  Derived from the quad corners;
 * see the file comment.  The top face uses the shear mapping instead. */


static void BakeTop(const unsigned char *tex, short factor, Boolean shade,
                    unsigned char *out, unsigned char *mask)
{
    short c, r;

    for (r = 0; r < kTexSide; r++) {
        for (c = 0; c < kTexSide; c++) {
            short x = (short)(kBakeWidth / 2 + c - r);
            short y = (short)((c + r + 1) >> 1);
            unsigned char v = tex[r * kTexSide + c];

            if (shade) {
                v = Art_ShadeIndex(v, factor);
            }
            out[y * kBakeWidth + x] = v;
            mask[y * kBakeWidth + x] = 1;
        }
    }
}

static void BakeSide(const unsigned char *tex, short face, short factor,
                     Boolean shade, unsigned char *out, unsigned char *mask)
{
    short x, column;

    /* Column x of the face starts at row `top` and is 16 texels deep: the
     * texture maps 1:1 onto the face, and the shear is in the geometry, not in
     * the sampling. Left face rises to the right, right face falls. */
    for (x = 0; x < kBakeSideWidth; x++) {
        short top = (face == kBakeFaceRight) ? (short)((kBakeSideWidth - 1 - x) / 2)
                                             : (short)(x / 2);
        short sx = (face == kBakeFaceRight) ? (short)(kBakeSideWidth - 1 - x) : x;

        for (column = 0; column < kTexSide; column++) {
            short y = (short)(top + column);
            unsigned char v = tex[column * kTexSide + sx];

            if (y >= kBakeSideHeight) {
                continue;
            }
            if (shade) {
                v = Art_ShadeIndex(v, factor);
            }
            out[y * kBakeSideWidth + x] = v;
            mask[y * kBakeSideWidth + x] = 1;
        }
    }
}

static void BakeInto(const unsigned char *tex, short face, short factor,
                     Boolean shade, unsigned char *out, unsigned char *mask)
{
    ClearFace(face, out, mask);

    if (face == kBakeFaceLeft || face == kBakeFaceRight) {
        BakeSide(tex, face, factor, shade, out, mask);
    }
    else {
        BakeTop(tex, factor, shade, out, mask);
    }
}

void Bake_Face(const unsigned char *tex, short face,
               unsigned char *out, unsigned char *mask)
{
    BakeInto(tex, face, 256, false, out, mask);
}

void Bake_FaceShaded(const unsigned char *tex, short face, short factor,
                     unsigned char *out, unsigned char *mask)
{
    BakeInto(tex, face, factor, true, out, mask);
}
