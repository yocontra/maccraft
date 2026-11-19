/* hud.c - the game's interface.
 *
 * Minecraft's chrome is a dark translucent bar with a lighter slot grid and a
 * white outline on the selected slot, a debug-style readout along the top, a
 * one-pixel crosshair, and the held block swinging in the corner. That is what
 * this draws.
 *
 * Every fill goes through Sprite_FillRect and every icon through Sprite_Draw,
 * because QuickDraw fills inside the play area do not land on this port; the
 * caller keeps the screen locked for the duration of a paint.
 */

#include <Fonts.h>
#include <Quickdraw.h>
#include <TextUtils.h>

#include "hud.h"
#include "sprite_sheet.h"
#include "../game/blocks.h"

#define kSlotW 32
#define kSlotH 20
#define kSlotGap 4
#define kStripH 14
#define kTextSize 9

static const RGBColor kTextLight = {0xE0E0, 0xE0E0, 0xE0E0};

static Rect sStrip;
static Rect sHotbar;
static Rect sPlay;

void HUD_SetTextStyle(void)
{
    TextFont(kFontIDGeneva);
    TextSize(kTextSize);
    TextFace(normal);
}

void HUD_Layout(const Rect *strip, const Rect *hotbar, const Rect *play)
{
    if (strip != NULL) {
        sStrip = *strip;
    }
    if (hotbar != NULL) {
        sHotbar = *hotbar;
    }
    if (play != NULL) {
        sPlay = *play;
    }
}

void HUD_SlotRect(short index, short kinds, Rect *out)
{
    short total = (short)(kinds * (kSlotW + kSlotGap) - kSlotGap);
    short startX = (short)(sHotbar.left + (sHotbar.right - sHotbar.left - total) / 2);
    short x = (short)(startX + index * (kSlotW + kSlotGap));

    SetRect(out, x, (short)(sHotbar.top + (sHotbar.bottom - sHotbar.top - kSlotH) / 2),
            (short)(x + kSlotW), (short)(sHotbar.top + (sHotbar.bottom - sHotbar.top - kSlotH) / 2 + kSlotH));
}

/* A one-pixel frame drawn as four fills; Sprite_FillRect is the primitive that
 * always lands here. */
static void Frame(const Rect *r, unsigned long colour)
{
    Rect line;

    line.left = r->left;
    line.right = r->right;
    line.top = r->top;
    line.bottom = (short)(r->top + 1);
    Sprite_FillRect(&line, colour);

    line.top = (short)(r->bottom - 1);
    line.bottom = r->bottom;
    Sprite_FillRect(&line, colour);

    line.top = r->top;
    line.bottom = r->bottom;
    line.left = r->left;
    line.right = (short)(r->left + 1);
    Sprite_FillRect(&line, colour);

    line.left = (short)(r->right - 1);
    line.right = r->right;
    Sprite_FillRect(&line, colour);
}

static void Fill(const Rect *r, unsigned long colour)
{
    Sprite_FillRect(r, colour);
}

/* Append a Pascal string to a growing one. */
static void AppendPStr(Str255 dst, const char *text)
{
    short len = (short)dst[0];
    short i = 0;

    while (text[i] != '\0' && len < 254) {
        dst[++len] = (unsigned char)text[i++];
    }
    dst[0] = (unsigned char)len;
}

static void AppendNum(Str255 dst, long value)
{
    Str255 num;

    NumToString(value, num);
    {
        short len = (short)dst[0];
        short i;

        for (i = 1; i <= num[0] && len < 254; i++) {
            dst[++len] = num[i];
        }
        dst[0] = (unsigned char)len;
    }
}

void HUD_DrawStrip(BlockID targeted, BlockID selected, short tileX, short tileY)
{
    Rect panel;
    Rect label;
    Str255 text;

    /* Dark panel across the strip so light text always reads, whatever the
     * window background happens to be. */
    Fill(&sStrip, kHudEdge);

    HUD_SetTextStyle();
    /* The panel is near-black, so the text must be set by RGB: ForeColor on a
     * direct-colour port left the label invisible. */
    RGBForeColor(&kTextLight);

    /* Left: the title, in the panel's light colour. */
    MoveTo((short)(sStrip.left + 4), (short)(sStrip.top + 11));
    DrawString("\pMacCraft");

    /* Right: what is under the crosshair, then the block that a click places and
     * the camera. The old strip printed the selected kind while calling it the
     * block, which was simply wrong. */
    text[0] = 0;
    AppendPStr(text, "look: ");
    AppendPStr(text, Block_Name(targeted));
    AppendPStr(text, "   place: ");
    AppendPStr(text, Block_Name(selected));
    AppendPStr(text, "   tile: ");
    AppendNum(text, tileX);
    AppendPStr(text, ",");
    AppendNum(text, tileY);

    label.left = (short)(sStrip.right - StringWidth(text) - 4);
    /* Never let the readout run into the title: the title is drawn at
     * sStrip.left + 4 in the same font, and 80 px clears it. */
    if (label.left < (short)(sStrip.left + 80)) {
        label.left = (short)(sStrip.left + 80);
    }
    label.top = sStrip.top;
    label.right = (short)(sStrip.right - 2);
    label.bottom = sStrip.bottom;
    panel = label;
    panel.right = sStrip.right;
    Fill(&panel, kHudEdge);

    MoveTo(label.left, (short)(sStrip.top + 11));
    DrawString(text);
}

void HUD_DrawHotbar(BlockID selected, short kinds)
{
    Rect slot;
    Rect outline;
    short i;

    /* Panel behind the slots, then one recessed slot each. */
    Fill(&sHotbar, kHudPanel);
    Frame(&sHotbar, kHudEdge);

    for (i = 0; i < kinds; i++) {
        HUD_SlotRect(i, kinds, &slot);
        Fill(&slot, kHudSlot);
        Frame(&slot, kHudEdge);

        /* The icon is the block's own top face, a 32x16 diamond, which reads as
         * a mini block the way Minecraft's hotbar icons do. */
        Sprite_Draw(slot.left, (short)(slot.top + 2), (BlockID)(i + 1), kBlockFaceTop, 0);
    }

    /* Selected slot: a two-pixel white frame just outside the slot. */
    HUD_SlotRect(selected >= 1 ? (short)(selected - 1) : 0, kinds, &outline);
    InsetRect(&outline, -2, -2);
    Frame(&outline, kHudSelect);
    InsetRect(&outline, 1, 1);
    Frame(&outline, kHudSelect);
}

void HUD_DrawCrosshair(void)
{
    short cx = (short)((sPlay.left + sPlay.right) / 2);
    short cy = (short)((sPlay.top + sPlay.bottom) / 2);
    Rect bar;

    /* One pixel thick with a dark outline, Minecraft's proportions. */
    SetRect(&bar, (short)(cx - 5), (short)(cy - 1), (short)(cx + 6), cy);
    Fill(&bar, kHudEdge);
    SetRect(&bar, (short)(cx - 5), cy, (short)(cx + 6), (short)(cy + 1));
    Fill(&bar, kHudSelect);

    SetRect(&bar, (short)(cx - 1), (short)(cy - 5), cx, (short)(cy + 6));
    Fill(&bar, kHudEdge);
    SetRect(&bar, cx, (short)(cy - 5), (short)(cx + 1), (short)(cy + 6));
    Fill(&bar, kHudSelect);
}

void HUD_DrawHeld(BlockID selected, short swing)
{
    short bx = (short)(sPlay.right - 116);
    short by = (short)(sPlay.bottom - 84);
    short drop = 0;

    if (selected == 0) {
        return;
    }

    /* A placement drops the block and lets it rise again: a triangle over the
     * swing's eight ticks, which is enough motion to feel like an action. */
    if (swing > 0) {
        drop = (short)(swing > 4 ? (8 - swing) * 3 : swing * 3);
    }

    by = (short)(by + drop);
    {
        Rect probe;

        SetRect(&probe, bx, by, (short)(bx + 6), (short)(by + 6));
        Sprite_FillRect(&probe, 0xFF00FFUL);
    }
    Sprite_DrawScaled((short)(bx + 32), by, selected, kBlockFaceTop, 0, 2);
    Sprite_DrawScaled((short)(bx + 32), (short)(by + 16), selected, kBlockFaceLeft, 1, 2);
    Sprite_DrawScaled((short)(bx + 64), (short)(by + 16), selected, kBlockFaceRight, 2, 2);
}
