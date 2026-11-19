/*
 * bake_quickdraw.c - target-only blit for baked face sprites.
 *
 * A baked face is an 8-bit index buffer, not a BitMap: the indices point into
 * the 64-entry palette owned by art.c.  QuickDraw can only copy colours it
 * understands, so the blit pins that palette into a tiny offscreen PixMap and
 * lets CopyDeepMask map the indices to the destination depth:
 *
 *   - one 32x16, 8-bit GWorld is created on the first blit and reused for
 *     every face; no allocation, locking or colour-table rebuild per sprite;
 *   - the sprite is copied into the GWorld's pixels verbatim, honouring the
 *     PixMap rowBytes;
 *   - the byte-per-pixel coverage mask is packed into a 1-bit BitMap, which
 *     CopyDeepMask uses to leave the background untouched (that is what
 *     carves the diamond and parallelogram corners out of the bounding box);
 *   - CopyDeepMask performs the depth conversion: indices survive exactly on
 *     an 8-bit screen and QuickDraw dithers the palette to black and white
 *     on a 1-bit screen.
 *
 * If the machine has no Color QuickDraw (NewGWorld fails: a 1-bit-only Mac
 * such as the Plus/SE), or the palette handle cannot be allocated, the blit
 * is a silent no-op.  The legacy pattern renderer remains the drawing path on
 * those machines; this file never halts the game for lack of a GWorld.
 *
 * API note: NewGWorld/LockPixels/CopyDeepMask are the pre-Carbon QuickDraw
 * calls (InterfaceLib 7.1).  No Carbon accessors are used, because the 68k
 * Retro68 build links against InterfaceLib, not CarbonAccessors.
 */

#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <Memory.h>

#include "art.h"
#include "bake.h"

static GWorldPtr    sGWorld    = NULL;
static PixMapHandle sPixMap    = NULL;
static Ptr          sBase      = NULL;
static long         sRowBytes  = 0;
static CTabHandle   sCTab      = NULL;
static Boolean      sTried     = false;
static Boolean      sReady     = false;

/* The largest face is the top: 32x16.  The GWorld is sized once for that. */
static const Rect kWorldBounds = {0, 0, 16, 32}; /* bottom, left, top, right */

/* A 32-pixel row needs 4 bytes; 16 rows need 64 bytes of mask. */
static unsigned char sMaskBits[64];

/* Build the PixMap colour table from art.c's palette.  Art_RGBColor already
 * expands the packed 0x0RGB nibbles to QuickDraw's byte-repeated 16-bit
 * components. */
static void MakeColorTable(void)
{
    short i;
    ColorSpec *cs;

    sCTab = (CTabHandle)NewHandleClear((long)(sizeof(ColorTable) +
                                              (kArtPaletteEntries - 1) * sizeof(ColorSpec)));
    if (sCTab == NULL) {
        return;
    }

    (*sCTab)->ctSeed  = GetCTSeed();
    (*sCTab)->ctFlags = 0;
    (*sCTab)->ctSize  = kArtPaletteEntries - 1;
    cs = &(*sCTab)->ctTable[0];

    for (i = 0; i < kArtPaletteEntries; i++) {
        unsigned short r, g, b;

        Art_RGBColor(i, &r, &g, &b);
        cs[i].value     = i;
        cs[i].rgb.red   = r;
        cs[i].rgb.green = g;
        cs[i].rgb.blue  = b;
    }
}

static Boolean EnsureGWorld(void)
{
    if (sTried) {
        return sReady;
    }
    sTried = true;

    MakeColorTable();
    if (sCTab == NULL) {
        return false;
    }

    if (NewGWorld(&sGWorld, 8, &kWorldBounds, sCTab, NULL, 0) != noErr) {
        sGWorld = NULL;
        return false;
    }

    sPixMap = GetGWorldPixMap(sGWorld);
    if (sPixMap == NULL || !LockPixels(sPixMap)) {
        DisposeGWorld(sGWorld);
        sGWorld = NULL;
        sPixMap = NULL;
        return false;
    }

    sBase     = (*sPixMap)->baseAddr;
    sRowBytes = (*sPixMap)->rowBytes & 0x3FFF;
    sReady    = true;
    return true;
}

/* Return the BitMap QuickDraw wants for CopyDeepMask's destination.  On a
 * colour screen a window port is a CGrafPort and the pixels live in its
 * PixMap; a 1-bit screen uses the plain GrafPort BitMap.  The depth test is
 * the GDevice's, exactly as the renderer's DetectColor does, so this stays a
 * pre-Carbon InterfaceLib-only code path. */
static const BitMap *PortBits(GrafPtr port)
{
#if TARGET_API_MAC_CARBON
    return GetPortBitMapForCopyBits((CGrafPtr)port);
#else
    GDHandle gd = GetGDevice();

    if (gd != NULL && (*gd)->gdPMap != NULL &&
        (*(*gd)->gdPMap)->pixelSize > 1) {
        return (const BitMap *)(*((CGrafPtr)port)->portPixMap);
    }
    return &((GrafPtr)port)->portBits;
#endif
}

void Bake_Blit(GrafPtr port, short left, short top,
               const unsigned char *pixels, const unsigned char *mask,
               short width, short height)
{
    short x, y;
    short i;
    Rect srcRect;
    Rect dstRect;
    Rect maskRect;
    BitMap maskBits;
    GrafPtr savedPort;

    if (port == NULL || pixels == NULL || mask == NULL) {
        return;
    }
    if (width <= 0 || height <= 0 ||
        width > kBakeWidth || height > kBakeHeight) {
        return;
    }
    if (!EnsureGWorld()) {
        return;
    }

    /* Stage the sprite.  The row stride is the GWorld's, not necessarily
     * width: QuickDraw may pad rows for alignment. */
    for (y = 0; y < height; y++) {
        const unsigned char *src = pixels + (long)y * width;
        Ptr dst = sBase + (long)y * sRowBytes;

        for (x = 0; x < width; x++) {
            dst[x] = (char)src[x];
        }
    }

    /* Pack the coverage mask: 1-bit, MSB leftmost, zeroed first so stale bits
     * from the previous sprite cannot show through.  The stride stays 4 bytes
     * per row (32 pixels), so a 16-pixel side just uses the low two bytes. */
    for (i = 0; i < (short)sizeof(sMaskBits); i++) {
        sMaskBits[i] = 0;
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if (mask[(long)y * width + x] != 0) {
                sMaskBits[(long)y * 4 + (x >> 3)] |=
                    (unsigned char)(0x80 >> (x & 7));
            }
        }
    }

    maskBits.baseAddr = (Ptr)sMaskBits;
    maskBits.rowBytes = 4;
    maskBits.bounds   = kWorldBounds;

    srcRect.left   = 0;
    srcRect.top    = 0;
    srcRect.right  = width;
    srcRect.bottom = height;

    dstRect.left   = left;
    dstRect.top    = top;
    dstRect.right  = (short)(left + width);
    dstRect.bottom = (short)(top + height);

    maskRect = srcRect;

    /* CopyDeepMask maps through the GWorld's colour table and uses the
     * current port's GDevice, so make the caller's port current for the
     * duration and restore whatever was there before. */
    GetPort(&savedPort);
    if (port != savedPort) {
        SetPort(port);
    }

    CopyDeepMask((const BitMap *)(*sPixMap), &maskBits, PortBits(port),
                 &srcRect, &maskRect, &dstRect, srcCopy, NULL);

    if (port != savedPort) {
        SetPort(savedPort);
    }
}
