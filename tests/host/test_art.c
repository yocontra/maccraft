/*
 * test_art.c - host-side checks for the palette and the block textures.
 *
 * These tests pin the properties the isometric baker relies on, not the
 * exact pixel art: every palette entry must survive Mac 8-bit colour, a
 * shade step must stay inside its hue's ramp, every texture must carry real
 * tonal detail, and all 14 block tops must be distinguishable byte-wise.
 *
 * Built and run by tests/host/run.sh with the same ASan/UBSan flags as the
 * other host tests.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "art.h"

static int sFailures;

static void checkf(int ok, const char *fmt, ...)
{
    va_list ap;

    if (ok) {
        return;
    }
    sFailures++;
    printf("FAIL: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static const char *kBlockNames[15] = {
    "empty", "grass", "dirt", "stone", "cobblestone", "sand", "gravel",
    "planks", "log", "leaves", "brick", "snow", "bedrock", "water", "glass"
};

/* Count the distinct palette indices in a 256-byte texture, note whether the
   darkest ink (index 0) appears, and reject any out-of-palette byte. */
static int distinct_indices(const unsigned char *tex, int *hasInk, int *bad)
{
    unsigned char seen[kArtPaletteEntries];
    int i;
    int count = 0;

    memset(seen, 0, sizeof seen);
    *hasInk = 0;
    *bad = 0;

    for (i = 0; i < kArtTextureBytes; i++) {
        unsigned value = tex[i];

        if (value >= kArtPaletteEntries) {
            *bad = 1;
            continue;
        }
        if (value == 0) {
            *hasInk = 1;
        }
        if (!seen[value]) {
            seen[value] = 1;
            count++;
        }
    }
    return count;
}

static unsigned max3(unsigned a, unsigned b, unsigned c)
{
    unsigned m = a;

    if (b > m) {
        m = b;
    }
    if (c > m) {
        m = c;
    }
    return m;
}

static unsigned min3(unsigned a, unsigned b, unsigned c)
{
    unsigned m = a;

    if (b < m) {
        m = b;
    }
    if (c < m) {
        m = c;
    }
    return m;
}

/* ---------------------------------------------------------------- */
/* Palette                                                           */
/* ---------------------------------------------------------------- */

static void test_palette(void)
{
    int i;
    int hue;
    int step;

    for (i = 0; i < kArtPaletteEntries; i++) {
        unsigned short packed = Art_RGB(i);
        unsigned short cr;
        unsigned short cg;
        unsigned short cb;
        unsigned r = (unsigned)((packed >> 8) & 0x0F);
        unsigned g = (unsigned)((packed >> 4) & 0x0F);
        unsigned b = (unsigned)(packed & 0x0F);
        unsigned r8;
        unsigned g8;
        unsigned b8;
        unsigned long rgb24;

        checkf(packed <= 0x0FFF, "palette %d sets bits above 0x0FFF", i);

        Art_RGBColor(i, &cr, &cg, &cb);
        r8 = (unsigned)(cr >> 8);
        g8 = (unsigned)(cg >> 8);
        b8 = (unsigned)(cb >> 8);

        /* Byte-repeated 8-bit components: 0xNN with both nibbles equal. */
        checkf((cr & 0xFF) == r8,
               "palette %d red 0x%04X is not byte-repeated", i, (unsigned)cr);
        checkf((cg & 0xFF) == g8,
               "palette %d green 0x%04X is not byte-repeated", i, (unsigned)cg);
        checkf((cb & 0xFF) == b8,
               "palette %d blue 0x%04X is not byte-repeated", i, (unsigned)cb);

        /* Every component is a multiple of 0x11, i.e. 0xNN. */
        checkf((r8 % 0x11) == 0 && (g8 % 0x11) == 0 && (b8 % 0x11) == 0,
               "palette %d is not a 0xNNNNNN colour", i);

        /* The nibble form must be the lossless encoding of those bytes. */
        checkf(r8 == r * 0x11 && g8 == g * 0x11 && b8 == b * 0x11,
               "palette %d nibble form does not match its components", i);

        rgb24 = ((unsigned long)r8 << 16)
              | ((unsigned long)g8 << 8)
              | (unsigned long)b8;
        checkf((rgb24 >> 24) == 0,
               "palette %d does not fit in 24 bits", i);
    }

    /* Index 0 is the darkest ink the textures use for holes and mortar; the
       ink rule in test_ink() relies on that being true globally. */
    {
        unsigned darkest = 0xFFFFFFFFu;
        int darkestIndex = -1;

        for (i = 0; i < kArtPaletteEntries; i++) {
            unsigned short packed = Art_RGB(i);
            unsigned sum = (((packed >> 8) & 0x0F)
                          + ((packed >> 4) & 0x0F)
                          + (packed & 0x0F)) * 0x11;

            if (sum < darkest) {
                darkest = sum;
                darkestIndex = i;
            }
        }
        checkf(darkestIndex == 0,
               "palette index 0 is not the darkest ink (index %d is)",
               darkestIndex);
    }

    /* Each hue must be a ramp: step 3 is brighter than step 2, and so on. */
    for (hue = 0; hue < kArtHues; hue++) {
        unsigned previous = 0;

        for (step = 0; step < kArtSteps; step++) {
            unsigned short packed = Art_RGB(hue * kArtSteps + step);
            unsigned sum = (((packed >> 8) & 0x0F)
                          + ((packed >> 4) & 0x0F)
                          + (packed & 0x0F)) * 0x11;

            if (step > 0) {
                checkf(sum > previous,
                       "hue %d step %d is not brighter than step %d",
                       hue, step, step - 1);
            }
            previous = sum;
        }
    }

    /* Out-of-range indices clamp instead of reading past the table. */
    checkf(Art_RGB(-1) == Art_RGB(0), "negative palette index did not clamp");
    checkf(Art_RGB(kArtPaletteEntries) == Art_RGB(kArtPaletteEntries - 1),
           "palette index 64 did not clamp");
    checkf(Art_RGB(9999) == Art_RGB(kArtPaletteEntries - 1),
           "large palette index did not clamp");

    /* NULL component pointers are legal: the renderer may only want one. */
    Art_RGBColor(0, NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- */
/* Face shading                                                      */
/* ---------------------------------------------------------------- */

static void test_shading(void)
{
    int i;
    int factor;

    for (i = 0; i < kArtPaletteEntries; i++) {
        unsigned previousStep = 0;

        for (factor = 0; factor <= 256; factor++) {
            unsigned char shaded =
                Art_ShadeIndex((unsigned char)i, (short)factor);
            unsigned char step = (unsigned char)(shaded & 3);

            checkf(shaded < kArtPaletteEntries,
                   "shading entry %d at factor %d left the palette",
                   i, factor);
            checkf((shaded >> 2) == (i >> 2),
                   "shading entry %d at factor %d changed hue to %d",
                   i, factor, shaded >> 2);
            if (factor > 0) {
                checkf(step >= previousStep,
                       "shading entry %d is not monotonic at factor %d",
                       i, factor);
            }
            previousStep = step;
        }

        checkf((Art_ShadeIndex((unsigned char)i, 0) & 3) == 0,
               "entry %d: factor 0 is not the darkest step", i);
        checkf((Art_ShadeIndex((unsigned char)i, 100) & 3) == 0,
               "entry %d: factor 100 is not the darkest step", i);
        checkf((Art_ShadeIndex((unsigned char)i, 154) & 3) == 1,
               "entry %d: factor 154 is not step 1", i);
        checkf((Art_ShadeIndex((unsigned char)i, 205) & 3) == 2,
               "entry %d: factor 205 is not step 2", i);
        checkf((Art_ShadeIndex((unsigned char)i, 256) & 3) == 3,
               "entry %d: factor 256 is not step 3", i);
        checkf((Art_ShadeIndex((unsigned char)i, -50) & 3) == 0,
               "entry %d: negative factor is not the darkest step", i);
        checkf((Art_ShadeIndex((unsigned char)i, 1000) & 3) == 3,
               "entry %d: oversize factor is not the brightest step", i);
    }
}

/* ---------------------------------------------------------------- */
/* Textures                                                          */
/* ---------------------------------------------------------------- */

static void test_textures(void)
{
    int id;
    int face;
    int other;

    for (id = 1; id <= 14; id++) {
        for (face = 0; face <= 2; face++) {
            const unsigned char *tex =
                Art_Texture((BlockID)id, (short)face);
            int hasInk;
            int bad;
            int count;

            checkf(tex != NULL, "%s face %d returned NULL",
                   kBlockNames[id], face);
            if (tex == NULL) {
                continue;
            }
            count = distinct_indices(tex, &hasInk, &bad);
            checkf(!bad, "%s face %d has an index outside the palette",
                   kBlockNames[id], face);
            checkf(count >= 3,
                   "%s face %d has only %d distinct indices",
                   kBlockNames[id], face, count);
            checkf(count > 1, "%s face %d is a constant fill",
                   kBlockNames[id], face);
        }
    }

    /* Every top must be byte-wise different from every other top. */
    for (id = 1; id <= 14; id++) {
        for (other = id + 1; other <= 14; other++) {
            checkf(memcmp(Art_Texture((BlockID)id, 0),
                          Art_Texture((BlockID)other, 0),
                          kArtTextureBytes) != 0,
                   "%s and %s tops are byte-identical",
                   kBlockNames[id], kBlockNames[other]);
        }
    }

    /* Blocks with a real top and a real side must differ on those faces. */
    checkf(memcmp(Art_Texture(1, 0), Art_Texture(1, 1),
                  kArtTextureBytes) != 0,
           "grass top is identical to grass side");
    checkf(memcmp(Art_Texture(8, 0), Art_Texture(8, 1),
                  kArtTextureBytes) != 0,
           "log top is identical to log side");
}

static void test_faces(void)
{
    int id;

    for (id = 1; id <= 14; id++) {
        /* The baker mirrors face 2, so both sides must be the same data. */
        checkf(Art_Texture((BlockID)id, 1) == Art_Texture((BlockID)id, 2),
               "%s faces 1 and 2 do not share the side texture",
               kBlockNames[id]);
        checkf(Art_Texture((BlockID)id, 7) == Art_Texture((BlockID)id, 1),
               "%s face 7 should fall back to the side texture",
               kBlockNames[id]);
    }

    /* Empty, unknown and future ids all draw as stone. */
    checkf(Art_Texture(0, 0) == Art_Texture(3, 0),
           "empty id does not draw as the stone top");
    checkf(Art_Texture(200, 0) == Art_Texture(3, 0),
           "id 200 does not draw as the stone top");
    checkf(Art_Texture(0, 1) == Art_Texture(3, 1),
           "empty id does not draw as the stone side");
    checkf(Art_Texture(200, 1) == Art_Texture(3, 1),
           "id 200 does not draw as the stone side");
    checkf(Art_Texture(15, 0) == Art_Texture(3, 0),
           "the first unused id does not draw as stone");
}

static void test_colours(void)
{
    const unsigned char *texture;
    int i;

    /* Stone must stay grey: every channel within one 8-bit step. */
    texture = Art_Texture(3, 0);
    for (i = 0; i < kArtTextureBytes; i++) {
        unsigned short r;
        unsigned short g;
        unsigned short b;

        Art_RGBColor(texture[i], &r, &g, &b);
        checkf(max3(r, g, b) - min3(r, g, b) <= 0x1111,
               "stone pixel %d is not grey (r=%04X g=%04X b=%04X)",
               i, (unsigned)r, (unsigned)g, (unsigned)b);
    }

    /* Grass must be green on every pixel of its top. */
    texture = Art_Texture(1, 0);
    for (i = 0; i < kArtTextureBytes; i++) {
        unsigned short r;
        unsigned short g;
        unsigned short b;

        Art_RGBColor(texture[i], &r, &g, &b);
        checkf(g > r && g > b,
               "grass pixel %d is not green (r=%04X g=%04X b=%04X)",
               i, (unsigned)r, (unsigned)g, (unsigned)b);
    }
}

static void test_ink(void)
{
    int id;

    /* Index 0 is the darkest ink.  Only mortar, pebble gaps and bedrock
       chips may use it; every other texture must be free of it. */
    for (id = 1; id <= 14; id++) {
        const unsigned char *texture = Art_Texture((BlockID)id, 0);
        int hasInk = 0;
        int i;

        for (i = 0; i < kArtTextureBytes; i++) {
            if (texture[i] == 0) {
                hasInk = 1;
                break;
            }
        }

        if (id == 4 || id == 6 || id == 12) { /* cobble, gravel, bedrock */
            checkf(hasInk,
                   "%s should use index 0 for its dark mortar or chips", 
                   kBlockNames[id]);
        } else {
            checkf(!hasInk,
                   "%s uses index 0 but has no dark ink", kBlockNames[id]);
        }
    }

    /* Leaves have holes, but the holes are the darkest green (index 8),
       because the format has no alpha and ink would read as holes in the
       middle of a solid block. */
    {
        const unsigned char *leaves = Art_Texture(9, 0);
        int hasDarkGreen = 0;
        int i;

        for (i = 0; i < kArtTextureBytes; i++) {
            if (leaves[i] == 8) {
                hasDarkGreen = 1;
                break;
            }
        }
        checkf(hasDarkGreen,
               "leaves have no darkest-green holes");
    }
}

int main(void)
{
    test_palette();
    test_shading();
    test_textures();
    test_faces();
    test_colours();
    test_ink();

    if (sFailures != 0) {
        printf("%d art check(s) failed\n", sFailures);
        return 1;
    }

    printf("test_art: ok (palette 64, textures 16, faces 3)\n");
    return 0;
}
