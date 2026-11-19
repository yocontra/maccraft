/* avatar.c - the player's figure.
 *
 * A 16x24 pixel character, drawn as horizontal runs through the sprite layer's
 * pixel fills. Three frames: standing, and two walk frames whose legs alternate;
 * in the air the legs tuck. At this size a per-pixel draw would be 384 calls, but
 * the rows are mostly contiguous colour, so a run-based draw costs about seventy
 * fills a frame -- and it is the same primitive the HUD already proves works.
 *
 * The palette is deliberately small and reads at a glance: dark hair, skin, a
 * cyan shirt, blue trousers, brown shoes, the colours Minecraft's default
 * character uses.
 */

#include <Quickdraw.h>

#include "avatar.h"
#include "sprite_sheet.h"
#include "../game/iso.h"

#define kAvatarW 16
#define kAvatarH 24

/* Palette. 0 is transparent. */
enum {
    kClear = 0,
    kHair = 1,
    kSkin = 2,
    kEye = 3,
    kShirt = 4,
    kShirtDark = 5,
    kTrousers = 6,
    kShoes = 7
};

static const unsigned long kAvatarColour[] = {
    0x000000UL, /* unused */
    0x3A2A18UL, /* hair */
    0xE0B088UL, /* skin */
    0x202020UL, /* eye */
    0x2E9FB0UL, /* shirt */
    0x23798AUL, /* shirt shadow */
    0x2B3D8CUL, /* trousers */
    0x4A3A22UL  /* shoes */
};

/* Standing frame: hair, face, shirt with arms, trousers, shoes. */
static const unsigned char kFrameStand[kAvatarH][kAvatarW] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,2,2,2,2,2,2,2,1,1,1,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,1,0,0},
    {0,0,1,2,3,2,2,2,2,3,2,2,1,1,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,5,5,5,5,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,0,2,4,4,4,4,4,4,4,4,4,4,2,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,0,0,2,2,4,4,4,4,4,4,2,2,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,0,7,7,7,0,0,7,7,7,0,0,0,0},
    {0,0,0,7,7,7,7,0,0,7,7,7,7,0,0,0}
};

/* Walk frame A: left leg forward, right leg back. */
static const unsigned char kFrameWalkA[kAvatarH][kAvatarW] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,2,2,2,2,2,2,2,1,1,1,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,1,0,0},
    {0,0,1,2,3,2,2,2,2,3,2,2,1,1,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,5,5,5,5,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,0,2,4,4,4,4,4,4,4,4,4,4,2,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,0,0,2,2,4,4,4,4,4,4,2,2,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,6,6,6,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,6,6,6,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,0,6,6,6,0,0},
    {0,0,0,6,6,6,0,0,0,0,0,6,6,6,0,0},
    {0,0,0,7,7,7,0,0,0,0,0,7,7,7,0,0},
    {0,0,7,7,7,7,0,0,0,0,0,7,7,7,7,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

/* Walk frame B: mirrored legs. */
static const unsigned char kFrameWalkB[kAvatarH][kAvatarW] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,2,2,2,2,2,2,2,1,1,1,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,1,0,0},
    {0,0,1,2,3,2,2,2,2,3,2,2,1,1,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,2,2,4,4,4,5,5,5,5,4,4,4,2,2,0},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,0,2,4,4,4,4,4,4,4,4,4,4,2,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,0,0,2,2,4,4,4,4,4,4,2,2,0,0,0},
    {0,0,0,0,0,6,6,6,6,6,6,0,0,0,0,0},
    {0,0,0,0,6,6,6,0,0,6,6,6,0,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,6,6,6,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,6,6,6,0,0,0},
    {0,0,0,6,6,6,0,0,0,0,0,6,6,6,0,0},
    {0,0,0,7,7,7,0,0,0,0,0,7,7,7,0,0},
    {0,0,7,7,7,7,0,0,0,0,0,7,7,7,7,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

/* Airborne: legs together and tucked, arms slightly out. */
static const unsigned char kFrameAir[kAvatarH][kAvatarW] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,2,2,2,2,2,2,2,1,1,1,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,1,0,0},
    {0,0,1,2,3,2,2,2,2,3,2,2,1,1,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0},
    {0,2,2,2,4,4,4,4,4,4,4,4,2,2,2,0},
    {2,2,2,4,4,4,4,4,4,4,4,4,4,2,2,2},
    {2,2,2,4,4,4,4,4,4,4,4,4,4,2,2,2},
    {2,2,2,4,4,4,5,5,5,5,4,4,4,2,2,2},
    {0,2,2,4,4,4,4,4,4,4,4,4,4,2,2,0},
    {0,0,2,4,4,4,4,4,4,4,4,4,4,2,0,0},
    {0,0,2,2,4,4,4,4,4,4,4,4,2,2,0,0},
    {0,0,0,2,2,4,4,4,4,4,4,2,2,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,6,6,0,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,6,6,0,0,0,0},
    {0,0,0,0,0,6,6,6,6,6,6,0,0,0,0,0},
    {0,0,0,0,0,6,6,6,6,6,6,0,0,0,0,0},
    {0,0,0,0,0,7,7,7,7,7,7,0,0,0,0,0},
    {0,0,0,0,7,7,7,7,7,7,7,7,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

/* Draw one frame with its feet at (footX, footY), each pixel scaled by `scale`,
 * by filling contiguous runs of the same colour. */
static void DrawFrame(const unsigned char frame[kAvatarH][kAvatarW], short footX, short footY,
                      short scale)
{
    short row;

    for (row = 0; row < kAvatarH; row++) {
        short col = 0;

        while (col < kAvatarW) {
            unsigned char index = frame[row][col];

            if (index == kClear) {
                col++;
                continue;
            }

            {
                short run = 1;
                Rect r;

                while (col + run < kAvatarW && frame[row][col + run] == index) {
                    run++;
                }

                r.left = (short)(footX + col * scale);
                r.right = (short)(r.left + run * scale);
                r.top = (short)(footY - (kAvatarH - row) * scale);
                r.bottom = (short)(r.top + scale);
                Sprite_FillRect(&r, kAvatarColour[index]);

                col = (short)(col + run);
            }
        }
    }
}

void Avatar_Draw(short px, short py, short pz, short camX, short camY,
                 const Rect *display, short phase, Boolean airborne)
{
    short sx, sy;

    Iso_Project(px, py, pz, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);

    /* The figure is 24 px tall and the feet sit on the block's top face. */
    if (sx < (short)(display->left - 12) || sx > (short)(display->right + 12)) {
        return;
    }
    if (sy < (short)(display->top - 28) || sy > (short)(display->bottom + 8)) {
        return;
    }

    /* Contact shadow, then the body. */
    {
        Rect shadow;

        SetRect(&shadow, (short)(sx - 6), sy, (short)(sx + 7), (short)(sy + 3));
        Sprite_FillRect(&shadow, 0x1E3E1EUL);
    }

    if (airborne) {
        DrawFrame(kFrameAir, (short)(sx - kAvatarW / 2), sy, 1);
    }
    else if (phase == 2 || phase == 3 || phase == 6 || phase == 7) {
        DrawFrame(kFrameWalkA, (short)(sx - kAvatarW / 2), sy, 1);
    }
    else if (phase == 1 || phase == 4 || phase == 5) {
        DrawFrame(kFrameWalkB, (short)(sx - kAvatarW / 2), sy, 1);
    }
    else {
        DrawFrame(kFrameStand, (short)(sx - kAvatarW / 2), sy, 1);
    }
}
