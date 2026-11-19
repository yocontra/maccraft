/* sprite_sheet.c - baked, shaded, indexed block faces written straight into the
 * screen's pixels.
 *
 * Why direct writes: on this guest every CopyBits/CopyMask/CopyDeepMask route
 * from an offscreen bitmap drew nothing or drew white, while a self-copy of the
 * screen's own bits landed. The device PixMap is addressable once locked, so the
 * baked band bytes are written into it a row at a time.
 *
 * Two quantities have to be right, and both are now measured rather than
 * assumed (see the harness log: disp_left/top, portbits_left/top, first_sx/sy):
 *
 *   - the window's origin. A window port's portRect starts at 0,0 and
 *     LocalToGlobal is a no-op for it, but portBits.bounds holds the *negated*
 *     window position (-102,-40 for a window at (102,40)). So local to global is
 *     `local - bounds.left`.
 *   - the screen's row stride. A PixMap's rowBytes carries a flag in the high
 *     half, so the stride is `rowBytes & 0x3FFF`.
 */

#include <Memory.h>
#include <Palettes.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>

#include "sprite_sheet.h"
#include "../game/art.h"
#include "../game/bake.h"
#include "../game/dither.h"

#define kSpriteKinds 14
#define kSpriteBands (kSpriteKinds * 3 * kSpriteShades)
/* A band is as tall as the tallest face: the top is 16 rows, a side 24. */
#define kSpriteBandH 24
#define kSpriteRows  (kSpriteBands * kSpriteBandH)
#define kMaskRowBytes 4

static unsigned char sSheet[kSpriteRows * kSpriteW];
static unsigned char sMaskSheet[kSpriteRows * kMaskRowBytes];

/* The inked run of every sprite row, computed once at bake time. Every face row
 * is a single contiguous run, so the draw loop needs no mask scan at all: it
 * reads two bytes and copies. Scanning the mask per row per face was a third of
 * the frame's work. */
static unsigned char sRunFirst[kSpriteBands * kSpriteBandH];
static unsigned char sRunLast[kSpriteBands * kSpriteBandH];
static unsigned char sDevice[kArtPaletteEntries];

/* The art palette as 8-bit RGB triplets. The guest runs at 32-bit direct colour
 * (measured: pixelSize 32, rowBytes 4096 for a 1024-wide screen), so a face is
 * written as real pixels and no palette mapping is involved at all. */
static unsigned char sRGB[kArtPaletteEntries][3];

/* Shading a face means moving the whole texture one ramp step down, not passing
 * every texel through Art_ShadeIndex: that maps each texel to a fixed step for
 * the given light level, which collapses a texture to one flat colour. This
 * table is the correct operation -- hue preserved, step reduced by the band's
 * shade and floored at the darkest step of the same hue. */
static unsigned char sShadeMap[kSpriteShades][kArtPaletteEntries];

/* The same palette as whole 0x00RRGGBB words, which is one store per pixel at
 * 32-bit depth instead of four byte stores and a shift. */
static unsigned long sPixel32[kArtPaletteEntries];
static Rect sView;
static short sOriginX = 0;
static short sOriginY = 0;
static short sDepth = 8;
static Boolean sReady = false;

static unsigned char sPixels[kBakePixels];
static unsigned char sPixelMask[kBakePixels];

static short BandOf(BlockID id, short face, short shade)
{
    short kind = (short)id - 1;

    if (kind < 0) {
        kind = 0;
    }
    if (kind >= kSpriteKinds) {
        kind = kSpriteKinds - 1;
    }
    if (face < 0 || face > 2) {
        face = 0;
    }
    if (shade < 0 || shade >= kSpriteShades) {
        shade = 0;
    }
    return (short)((kind * 3 + face) * kSpriteShades + shade);
}

short Sprite_ShadeStep(short factor)
{
    if (factor >= 231) {
        return 0;
    }
    if (factor >= 180) {
        return 1;
    }
    if (factor >= 128) {
        return 2;
    }
    return 3;
}

/* An indexed GDevice keeps a Palette in pmTable: a dense list of ColorInfo, each
 * with its RGB. Reading it as a ColorTable is what made every colour white. */
#define kDeviceEntries 256

static struct {
    short count;
    RGBColor rgb[kDeviceEntries];
} sDeviceColors;

static void ReadDeviceColors(void)
{
    GDHandle gd = GetMainDevice();
    PaletteHandle pal;
    short i;

    sDeviceColors.count = 0;
    if (gd == NULL || (*gd)->gdPMap == NULL || (*(*gd)->gdPMap)->pmTable == NULL) {
        return;
    }
    pal = (PaletteHandle)(*(*gd)->gdPMap)->pmTable;
    if (pal == NULL || *pal == NULL) {
        return;
    }
    sDeviceColors.count = (*pal)->pmEntries;
    if (sDeviceColors.count > kDeviceEntries) {
        sDeviceColors.count = kDeviceEntries;
    }
    for (i = 0; i < sDeviceColors.count; i++) {
        sDeviceColors.rgb[i] = (*pal)->pmInfo[i].ciRGB;
    }
}

static void BuildRGBTable(void)
{
    int i;

    for (i = 0; i < kArtPaletteEntries; i++) {
        unsigned short r, g, b;

        Art_RGBColor(i, &r, &g, &b);
        sRGB[i][0] = (unsigned char)(r >> 8);
        sRGB[i][1] = (unsigned char)(g >> 8);
        sRGB[i][2] = (unsigned char)(b >> 8);
        sPixel32[i] = ((unsigned long)sRGB[i][0] << 16) |
                      ((unsigned long)sRGB[i][1] << 8) |
                      (unsigned long)sRGB[i][2];
    }
}

static void BuildDeviceMap(void)
{
    int i;
    short entry;

    ReadDeviceColors();

    for (i = 0; i < kArtPaletteEntries; i++) {
        unsigned short r, g, b;
        long bestDistance = -1;
        unsigned char best = (unsigned char)i;

        Art_RGBColor(i, &r, &g, &b);

        for (entry = 0; entry < sDeviceColors.count; entry++) {
            long dr = (long)sDeviceColors.rgb[entry].red - (long)r;
            long dg = (long)sDeviceColors.rgb[entry].green - (long)g;
            long db = (long)sDeviceColors.rgb[entry].blue - (long)b;
            long distance = dr * dr + dg * dg + db * db;

            if (bestDistance < 0 || distance < bestDistance) {
                bestDistance = distance;
                best = (unsigned char)entry;
            }
        }
        sDevice[i] = best;
    }
}

static void BuildShadeMap(void)
{
    short shade;
    short i;

    for (shade = 0; shade < kSpriteShades; shade++) {
        for (i = 0; i < kArtPaletteEntries; i++) {
            short hue = (short)(i & ~3);
            short step = (short)(i & 3);
            short out = (short)(step - shade);

            if (out < 0) {
                out = 0;
            }
            sShadeMap[shade][i] = (unsigned char)(hue + out);
        }
    }
}

/* Minecraft's smooth lighting darkens a side face towards the ground it meets.
 * The palette only has four ramp steps, so the ramp is quantised: the top third
 * of a side keeps the face's shade, the middle goes one step darker and the
 * bottom one step darker again. That reads as a contact shadow and stops blocks
 * looking like they float. Tops stay uniform, as the light hits them square on. */
static short RowShade(short face, short row, short height, short shade)
{
    short extra;

    if (face == 0 || height <= 0) {
        return shade;
    }
    extra = (short)((row * 3) / height);
    if (extra > 2) {
        extra = 2;
    }
    shade = (short)(shade + extra);
    if (shade >= kSpriteShades) {
        shade = kSpriteShades - 1;
    }
    return shade;
}

static void PutBand(short band, short width, short height, short shade, short face)
{
    short row, col;
    Ptr colourBase = (Ptr)sSheet + (long)band * kSpriteBandH * kSpriteW;
    Ptr maskBase = (Ptr)sMaskSheet + (long)band * kSpriteBandH * kMaskRowBytes;

    for (row = 0; row < height; row++) {
        /* Continuous shade in sixteenths: the band's own step, darker towards
         * the base of a side face. The fractional part is spent by ordered
         * dithering between two ramp steps, which is how four palette steps
         * produce a smooth gradient instead of three visible bands. */
        short shadeX16 = (short)(shade * 16 + (face != 0 ? (row * 48) / height : 0));
        short step = (short)(shadeX16 / 16);
        short fraction = (short)(shadeX16 % 16);

        Ptr src = sPixels + (long)row * width;
        Ptr cov = sPixelMask + (long)row * width;
        Ptr dst = colourBase + (long)row * kSpriteW;
        Ptr mrow = maskBase + (long)row * kMaskRowBytes;

        for (col = 0; col < kMaskRowBytes; col++) {
            mrow[col] = 0;
        }
        {
            short first = -1;
            short last = -1;

            for (col = 0; col < width; col++) {
                unsigned char index = src[col];

                /* Shade AND contact gradient: the shade band picks the ramp
                 * step and RowShade darkens it towards the base of a side face.
                 * Without this every band was identical and the whole lighting
                 * model never reached the screen. */
                short useStep = step;

                if (fraction > 0 && Dither_On(col, row, fraction)) {
                    useStep = (short)(step + 1);
                }
                if (useStep >= kSpriteShades) {
                    useStep = kSpriteShades - 1;
                }
                index = sShadeMap[useStep][index < kArtPaletteEntries ? index : 0];
                dst[col] = sDevice[index];
                if (cov[col]) {
                    mrow[col >> 3] |= (unsigned char)(0x80 >> (col & 7));
                    if (first < 0) {
                        first = col;
                    }
                    last = col;
                }
            }
            sRunFirst[band * kSpriteBandH + row] = (unsigned char)(first < 0 ? 0 : first);
            sRunLast[band * kSpriteBandH + row] = (unsigned char)(last < 0 ? 0 : last);
        }
    }
}

Boolean Sprite_Init(void)
{
    GDHandle gd = GetMainDevice();
    short id, face, shade;

    if (sReady) {
        return true;
    }
    if (gd == NULL || (*gd)->gdPMap == NULL) {
        return false;
    }
    /* Only the 32-bit path is implemented. On any other depth the direct writer
     * would step four bytes per pixel through a narrower buffer, so the whole
     * sprite layer stands down and the QuickDraw renderer takes over. */
    if ((*(*gd)->gdPMap)->pixelSize != 32) {
        return false;
    }

    BuildRGBTable();
    BuildShadeMap();
    BuildDeviceMap();

    for (id = 1; id <= kSpriteKinds; id++) {
        for (face = 0; face < 3; face++) {
            /* Bake the texture once, unshaded; the four shade bands are that
             * same pixel art with its ramp step shifted, so the texture survives
             * and the face still darkens as a whole. */
            Bake_Face(Art_Texture((BlockID)id, face), face, sPixels, sPixelMask);
            for (shade = 0; shade < kSpriteShades; shade++) {
                PutBand(BandOf((BlockID)id, face, shade), Bake_FaceWidth(face),
                        Bake_FaceHeight(face), shade, face);
            }
        }
    }

    sDepth = (*gd)->gdPMap != NULL ? (*(*gd)->gdPMap)->pixelSize : 8;
    sReady = true;
    return true;
}

Boolean Sprite_Ready(void)
{
    return sReady;
}

/* Cached screen state for the duration of a frame. Locking the device PixMap
 * once per frame instead of once per face, and caching baseAddr/stride, removes
 * three quarters of the drawing overhead on this machine. */
static Ptr sScreenBase = NULL;
static short sScreenRowBytes = 0;
static short sScreenWidth = 1024;
static short sScreenHeight = 768;

void Sprite_BeginFrame(void)
{
    GDHandle gd = GetMainDevice();

    sScreenBase = NULL;
    if (gd == NULL || (*gd)->gdPMap == NULL) {
        return;
    }
    if (!LockPixels((*gd)->gdPMap)) {
        return;
    }
    sScreenBase = (*(*gd)->gdPMap)->baseAddr;
    sScreenRowBytes = (short)((*(*gd)->gdPMap)->rowBytes & 0x3FFF);
    sScreenHeight = (short)((*gd)->gdRect.bottom - (*gd)->gdRect.top);
    sScreenWidth = (short)(sScreenRowBytes / 4);
    if (sScreenWidth > (short)((*gd)->gdRect.right - (*gd)->gdRect.left)) {
        sScreenWidth = (short)((*gd)->gdRect.right - (*gd)->gdRect.left);
    }
}

void Sprite_EndFrame(void)
{
    GDHandle gd = GetMainDevice();

    if (sScreenBase != NULL && gd != NULL && (*gd)->gdPMap != NULL) {
        UnlockPixels((*gd)->gdPMap);
    }
    sScreenBase = NULL;
}

/* A filled rectangle written through the same pixel path as the sprites. The
 * HUD uses this because QuickDraw's fills inside the play area do not survive:
 * the world is written straight to screen memory, and anything QuickDraw paints
 * afterwards is at the mercy of the update clip. */
void Sprite_FillRect(const Rect *r, unsigned long rgb)
{
    short x, y;
    short viewLeft, viewRight, viewTop, viewBottom;

    if (!sReady || sScreenBase == NULL || r == NULL) {
        return;
    }
    viewLeft = (short)(sView.left + sOriginX);
    viewRight = (short)(sView.right + sOriginX);
    viewTop = (short)(sView.top + sOriginY);
    viewBottom = (short)(sView.bottom + sOriginY);

    for (y = (short)(r->top + sOriginY); y < (short)(r->bottom + sOriginY); y++) {
        unsigned long *line;

        if (y < viewTop || y >= viewBottom || y < 0 || y >= sScreenHeight) {
            continue;
        }
        line = (unsigned long *)(sScreenBase + (long)y * sScreenRowBytes);
        for (x = (short)(r->left + sOriginX); x < (short)(r->right + sOriginX); x++) {
            if (x < viewLeft || x >= viewRight || x < 0 || x >= sScreenWidth) {
                continue;
            }
            line[x] = rgb;
        }
    }
}

/* Move the pixels inside r by (dx, dy), leaving the vacated band untouched.
 *
 * This is what makes a camera pan cheap: the previous frame is shifted and only
 * the exposed strip is redrawn, instead of recomposing two thousand faces. Rows
 * are copied in the direction that cannot overwrite source data, and the copy
 * within a row uses memmove semantics for the same reason. */
void Sprite_ScrollRect(const Rect *r, short dx, short dy)
{
    short y, yStart, yEnd, yStep;
    short x0, x1;
    short width;

    if (!sReady || sScreenBase == NULL || r == NULL || (dx == 0 && dy == 0)) {
        return;
    }
    if (dx <= -sScreenWidth || dx >= sScreenWidth || dy <= -sScreenHeight || dy >= sScreenHeight) {
        return;
    }

    x0 = (short)(r->left + sOriginX);
    x1 = (short)(r->right + sOriginX);
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > sScreenWidth) {
        x1 = sScreenWidth;
    }
    width = (short)(x1 - x0);
    if (width <= 0) {
        return;
    }

    /* Vertical order: walking down would overwrite rows that are still to be
     * read when dy is positive, so positive deltas walk upwards. */
    if (dy > 0) {
        yStart = (short)(r->bottom + sOriginY - 1);
        yEnd = (short)(r->top + sOriginY - 1);
        yStep = -1;
    }
    else {
        yStart = (short)(r->top + sOriginY);
        yEnd = (short)(r->bottom + sOriginY);
        yStep = 1;
    }

    for (y = yStart; y != yEnd; y = (short)(y + yStep)) {
        short srcY = (short)(y - dy);
        Ptr dst;
        Ptr src;
        short copyX0;
        short copyWidth;

        if (y < 0 || y >= sScreenHeight || srcY < 0 || srcY >= sScreenHeight) {
            continue;
        }
        dst = sScreenBase + (long)y * sScreenRowBytes;
        src = sScreenBase + (long)srcY * sScreenRowBytes;

        if (dx > 0) {
            copyX0 = (short)(x0 + dx);
            copyWidth = (short)(width - dx);
        }
        else {
            copyX0 = x0;
            copyWidth = (short)(width + dx);
        }
        if (copyWidth <= 0) {
            continue;
        }

        /* memmove by hand: the regions can overlap when dx is small. */
        if (dx > 0) {
            short i;

            for (i = copyWidth - 1; i >= 0; i--) {
                ((unsigned long *)dst)[copyX0 + i] = ((unsigned long *)src)[copyX0 + i - dx];
            }
        }
        else {
            short i;

            for (i = 0; i < copyWidth; i++) {
                ((unsigned long *)dst)[copyX0 + i] = ((unsigned long *)src)[copyX0 + i - dx];
            }
        }
    }
}

void Sprite_SetView(const Rect *view)
{
    if (view != NULL) {
        sView = *view;
    }
}

/* The window's global position. Callers pass the rectangle the window was
 * created at; it is simpler and safer than reading it back, since
 * portBits.bounds stores the negation of it. */
void Sprite_SetOrigin(short dx, short dy)
{
    sOriginX = dx;
    sOriginY = dy;
}

void Sprite_Dispose(void)
{
    sReady = false;
}

/* Harness probes: Sprite_Probe[0] != 0 makes Sprite_Draw return immediately, so
 * the cost of the copy can be separated from the cost of deciding to draw;
 * [1] counts calls. */
long Sprite_Probe[2];

void Sprite_Draw(short left, short top, BlockID id, short face, short shade)
{
    Sprite_DrawScaled(left, top, id, face, shade, 1);
}

/* One face, optionally magnified by an integer factor. Scale 2 is used for the
 * held block in the corner, which is the one place the game shows a block bigger
 * than the world draws it. */
void Sprite_DrawScaled(short left, short top, BlockID id, short face, short shade, short scale)
{
    short band;
    short width;
    short height;
    short row, col;
    short bandStart;
    short originX;
    short originY;
    short viewLeft, viewRight, viewTop, viewBottom;
    short firstRow, lastRow;
    Ptr bandPixels;
    Ptr rowBase;

    if (!sReady || id == 0 || sScreenBase == NULL) {
        return;
    }
    Sprite_Probe[1]++;
    if (Sprite_Probe[0] != 0) {
        return;
    }

    band = BandOf(id, face, shade);
    width = Bake_FaceWidth(face);
    height = Bake_FaceHeight(face);

    originX = (short)(left + sOriginX);
    originY = (short)(top + sOriginY);

    viewLeft = (short)(sView.left + sOriginX);
    viewRight = (short)(sView.right + sOriginX);
    viewTop = (short)(sView.top + sOriginY);
    viewBottom = (short)(sView.bottom + sOriginY);

    if (originX >= viewRight || (short)(originX + width) <= viewLeft) {
        return;
    }
    if (originY >= sScreenHeight || (short)(originX + width) <= 0) {
        return;
    }
    if (originX >= sScreenWidth) {
        return;
    }
    if (originY >= viewBottom || (short)(originY + height) <= viewTop) {
        return;
    }

    /* Only the rows inside the play area need visiting at all. */
    firstRow = (short)(viewTop - originY);
    if (firstRow < 0) {
        firstRow = 0;
    }
    lastRow = (short)(viewBottom - originY);
    if (lastRow > height) {
        lastRow = height;
    }
    if (firstRow >= lastRow) {
        return;
    }

    bandStart = (short)(band * kSpriteBandH);
    bandPixels = (Ptr)sSheet + (long)bandStart * kSpriteW;
    /* One multiply for the first row, then the pointer walks: the old code did a
     * multiply per row per face, which is 32,000 multiplies a frame. */
    rowBase = sScreenBase + (long)(originY + firstRow) * sScreenRowBytes;

    /* The depth branch is hoisted out of the row loop: one test per face instead
     * of one per row. */
    if (sDepth >= 32 && scale == 2) {
        /* Two-by-two magnification: each source pixel fills a 2x2 destination
         * square, written as two words per row into two consecutive rows. */
        for (row = firstRow; row < lastRow; row++) {
            short srcRow = (short)(bandStart + row);
            short x0 = (short)(originX + sRunFirst[srcRow] * 2);
            short dstRow = (short)(originY + row * 2);

            if (dstRow < 0 || (short)(dstRow + 1) >= sScreenHeight) {
                continue;
            }
            short x1 = (short)(originX + (sRunLast[srcRow] + 1) * 2);
            const unsigned char *p;
            unsigned long *line;
            unsigned long *line2;

            if (x0 < viewLeft) {
                x0 = viewLeft;
            }
            if (x1 > viewRight) {
                x1 = viewRight;
            }
            if (x0 >= x1) {
                continue;
            }
            p = bandPixels + (long)row * kSpriteW;
            line = (unsigned long *)(sScreenBase + (long)dstRow * sScreenRowBytes) + x0;
            line2 = (unsigned long *)((Ptr)line + sScreenRowBytes);
            for (col = x0; col < x1; col++, line++, line2++) {
                unsigned long word = sPixel32[p[(col - originX) >> 1]];

                *line = word;
                *line2 = word;
            }
        }
        return;
    }

    if (sDepth >= 32) {
        for (row = firstRow; row < lastRow; row++, rowBase += sScreenRowBytes) {
            short srcRow = (short)(bandStart + row);
            short x0 = (short)(originX + sRunFirst[srcRow]);
            short x1 = (short)(originX + sRunLast[srcRow] + 1);
            const unsigned char *p;
            unsigned long *line;

            if (x0 < viewLeft) {
                x0 = viewLeft;
            }
            if (x1 > viewRight) {
                x1 = viewRight;
            }
            if (x0 >= x1) {
                continue;
            }
            p = bandPixels + (long)row * kSpriteW + (x0 - originX);
            line = (unsigned long *)rowBase + x0;
            for (col = x0; col < x1; col++) {
                *line++ = sPixel32[*p++];
            }
        }
    }
    else if (sDepth == 16) {
        for (row = firstRow; row < lastRow; row++, rowBase += sScreenRowBytes) {
            short srcRow = (short)(bandStart + row);
            short x0 = (short)(originX + sRunFirst[srcRow]);
            short x1 = (short)(originX + sRunLast[srcRow] + 1);
            const unsigned char *p;
            Ptr out;

            if (x0 < viewLeft) {
                x0 = viewLeft;
            }
            if (x1 > viewRight) {
                x1 = viewRight;
            }
            if (x0 >= x1) {
                continue;
            }
            p = bandPixels + (long)row * kSpriteW + (x0 - originX);
            out = rowBase + (long)x0 * 2;
            for (col = x0; col < x1; col++) {
                unsigned char index = *p++;
                unsigned short word = (unsigned short)(((sRGB[index][0] >> 3) << 10) |
                                                       ((sRGB[index][1] >> 3) << 5) |
                                                       (sRGB[index][2] >> 3));

                *out++ = (char)(word >> 8);
                *out++ = (char)(word & 0xFF);
            }
        }
    }
    else {
        for (row = firstRow; row < lastRow; row++, rowBase += sScreenRowBytes) {
            short srcRow = (short)(bandStart + row);
            short x0 = (short)(originX + sRunFirst[srcRow]);
            short x1 = (short)(originX + sRunLast[srcRow] + 1);
            const unsigned char *p;
            Ptr out;

            if (x0 < viewLeft) {
                x0 = viewLeft;
            }
            if (x1 > viewRight) {
                x1 = viewRight;
            }
            if (x0 >= x1) {
                continue;
            }
            p = bandPixels + (long)row * kSpriteW + (x0 - originX);
            out = rowBase + x0;
            for (col = x0; col < x1; col++) {
                *out++ = sDevice[*p++];
            }
        }
    }
}
