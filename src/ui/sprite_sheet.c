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

#define kSpriteKinds 14
#define kSpriteBands (kSpriteKinds * 3 * kSpriteShades)
#define kSpriteRows  (kSpriteBands * kSpriteH)
#define kMaskRowBytes 4

static unsigned char sSheet[kSpriteRows * kSpriteW];
static unsigned char sMaskSheet[kSpriteRows * kMaskRowBytes];

/* The inked run of every sprite row, computed once at bake time. Every face row
 * is a single contiguous run, so the draw loop needs no mask scan at all: it
 * reads two bytes and copies. Scanning the mask per row per face was a third of
 * the frame's work. */
static unsigned char sRunFirst[kSpriteBands * kSpriteH];
static unsigned char sRunLast[kSpriteBands * kSpriteH];
static unsigned char sDevice[kArtPaletteEntries];

/* The art palette as 8-bit RGB triplets. The guest runs at 32-bit direct colour
 * (measured: pixelSize 32, rowBytes 4096 for a 1024-wide screen), so a face is
 * written as real pixels and no palette mapping is involved at all. */
static unsigned char sRGB[kArtPaletteEntries][3];

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

static short FactorOf(short shade)
{
    switch (shade) {
    case 1:
        return 205;
    case 2:
        return 154;
    case 3:
        return 103;
    default:
        return 256;
    }
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

static void PutBand(short band, short width, short height)
{
    short row, col;
    Ptr colourBase = (Ptr)sSheet + (long)band * kSpriteH * kSpriteW;
    Ptr maskBase = (Ptr)sMaskSheet + (long)band * kSpriteH * kMaskRowBytes;

    for (row = 0; row < height; row++) {
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

                dst[col] = sDevice[index < kArtPaletteEntries ? index : 0];
                if (cov[col]) {
                    mrow[col >> 3] |= (unsigned char)(0x80 >> (col & 7));
                    if (first < 0) {
                        first = col;
                    }
                    last = col;
                }
            }
            sRunFirst[band * kSpriteH + row] = (unsigned char)(first < 0 ? 0 : first);
            sRunLast[band * kSpriteH + row] = (unsigned char)(last < 0 ? 0 : last);
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
    if ((*(*gd)->gdPMap)->pixelSize <= 1) {
        return false;
    }

    BuildRGBTable();
    BuildDeviceMap();

    for (id = 1; id <= kSpriteKinds; id++) {
        for (face = 0; face < 3; face++) {
            for (shade = 0; shade < kSpriteShades; shade++) {
                short band = BandOf((BlockID)id, face, shade);

                Bake_FaceShaded(Art_Texture((BlockID)id, face), face, FactorOf(shade),
                                sPixels, sPixelMask);
                PutBand(band, Bake_FaceWidth(face), Bake_FaceHeight(face));
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
}

void Sprite_EndFrame(void)
{
    GDHandle gd = GetMainDevice();

    if (sScreenBase != NULL && gd != NULL && (*gd)->gdPMap != NULL) {
        UnlockPixels((*gd)->gdPMap);
    }
    sScreenBase = NULL;
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

    bandStart = (short)(band * kSpriteH);
    bandPixels = (Ptr)sSheet + (long)bandStart * kSpriteW;
    /* One multiply for the first row, then the pointer walks: the old code did a
     * multiply per row per face, which is 32,000 multiplies a frame. */
    rowBase = sScreenBase + (long)(originY + firstRow) * sScreenRowBytes;

    /* The depth branch is hoisted out of the row loop: one test per face instead
     * of one per row. */
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
