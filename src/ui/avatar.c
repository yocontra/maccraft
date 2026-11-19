/* avatar.c - the player's figure.
 *
 * Drawn with the sprite layer's pixel fills rather than a block texture: at two
 * pixels per limb a 16x16 texture would be one bright speck. Coordinates come in
 * as the block the player stands on, so this file needs no game state.
 */

#include <Quickdraw.h>

#include "avatar.h"
#include "sprite_sheet.h"
#include "../game/iso.h"

void Avatar_Draw(short px, short py, short pz, short camX, short camY,
                 const Rect *display, short phase, Boolean airborne)
{
    short sx, sy;
    short bob = 0;
    Rect r;

    Iso_Project(px, py, pz, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);

    /* Nothing to draw when the player is not in the play area. */
    if (sx < (short)(display->left - 8) || sx > (short)(display->right + 8)) {
        return;
    }
    if (sy < (short)(display->top - 32) || sy > (short)(display->bottom + 8)) {
        return;
    }

    /* The torso rises half a pixel per step on alternate phases, which at this
     * size reads as a walk rather than as a slide. */
    if (!airborne && phase >= 2 && phase <= 5) {
        bob = 1;
    }

    SetRect(&r, (short)(sx - 5), sy, (short)(sx + 6), (short)(sy + 3));
    Sprite_FillRect(&r, 0x1E3E1EUL);          /* contact shadow */

    if (airborne) {
        /* Legs together and tucked: the silhouette says "off the ground". */
        SetRect(&r, (short)(sx - 3), (short)(sy - 8 - bob), (short)(sx + 4), (short)(sy - 2));
        Sprite_FillRect(&r, 0x2B3D8CUL);
    }
    else {
        /* Two legs, one forward and one back, alternating with the phase. */
        short swing = (short)((phase <= 3) ? phase : 8 - phase);

        SetRect(&r, (short)(sx - 4), (short)(sy - 10 - bob), (short)(sx - 1), (short)(sy - swing / 2));
        Sprite_FillRect(&r, 0x24337AUL);
        SetRect(&r, (short)(sx + 1), (short)(sy - 10 - bob), (short)(sx + 4), (short)(sy - (4 - swing) / 2));
        Sprite_FillRect(&r, 0x2B3D8CUL);
    }

    SetRect(&r, (short)(sx - 4), (short)(sy - 20 - bob), (short)(sx + 5), (short)(sy - 10 - bob));
    Sprite_FillRect(&r, 0x3D5BD6UL);          /* torso */
    SetRect(&r, (short)(sx - 3), (short)(sy - 27 - bob), (short)(sx + 4), (short)(sy - 20 - bob));
    Sprite_FillRect(&r, 0xE0B088UL);          /* head */
}
