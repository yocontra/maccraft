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

void Avatar_Draw(short px, short py, short pz, short camX, short camY, const Rect *display)
{
    short sx, sy;
    Rect r;

    Iso_Project(px, py, pz, camX, camY, &sx, &sy);
    sx = (short)(sx + display->left);
    sy = (short)(sy + display->top);

    /* Nothing to draw when the player is not in the play area, and the figure's
     * own extent is clamped to it. */
    if (sx < (short)(display->left - 8) || sx > (short)(display->right + 8)) {
        return;
    }
    if (sy < (short)(display->top - 32) || sy > (short)(display->bottom + 8)) {
        return;
    }

    SetRect(&r, (short)(sx - 5), sy, (short)(sx + 6), (short)(sy + 3));
    Sprite_FillRect(&r, 0x1E3E1EUL);          /* contact shadow */
    SetRect(&r, (short)(sx - 3), (short)(sy - 10), (short)(sx + 4), sy);
    Sprite_FillRect(&r, 0x2B3D8CUL);          /* legs */
    SetRect(&r, (short)(sx - 4), (short)(sy - 20), (short)(sx + 5), (short)(sy - 10));
    Sprite_FillRect(&r, 0x3D5BD6UL);          /* torso */
    SetRect(&r, (short)(sx - 3), (short)(sy - 27), (short)(sx + 4), (short)(sy - 20));
    Sprite_FillRect(&r, 0xE0B088UL);          /* head */
}
