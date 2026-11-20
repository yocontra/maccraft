/* hud.h - everything that is not the world: status strip, hotbar, crosshair and
 * the held block.
 *
 * The renderer owns the world pass and hands the HUD the rectangles it should
 * work inside, so layout stays in one place and this module never guesses at
 * window geometry. All drawing goes through the sprite layer's pixel path
 * (Sprite_Draw / Sprite_DrawScaled / Sprite_FillRect), because QuickDraw fills
 * inside the play area do not land on this port.
 */

#ifndef HUD_H
#define HUD_H

#include <Quickdraw.h>

#include "../game/world.h"

/* Colours, in 0x00RRGGBB, matching Minecraft's interface greys. */
#define kHudPanel   0x3F3F3FUL
#define kHudSlot    0x8B8B8BUL
#define kHudEdge    0x1A1A1AUL
#define kHudSelect  0xFFFFFFUL
#define kHudText    0xE0E0E0UL

/* Number of slot columns the hotbar shows. */
#define kHudSlots kBlockKinds

/* Tell the HUD where it may draw. Called from the window layout code. */
void HUD_Layout(const Rect *strip, const Rect *hotbar, const Rect *play);

/* The hotbar slot rectangle for a kind index, for hit testing a click. */
void HUD_SlotRect(short index, short kinds, Rect *out);

/* Strip on the left, hotbar along the bottom: the chrome that frames the play
 * area. Drawn inside a Sprite_BeginFrame/EndFrame pair with the play view
 * narrowed to the relevant rect by the caller. */
void HUD_DrawStrip(BlockID targeted, BlockID selected, short tileX, short tileY);

/* The hotbar: a dark panel, one slot per kind, the selected kind outlined. */
void HUD_DrawHotbar(BlockID selected, short kinds);

/* Crosshair at the centre of the play area, one pixel thick. */
void HUD_DrawCrosshair(void);

/* The held block, magnified, in the bottom-right of the play area. `swing` is
 * 0 when idle and counts down from a small number after a placement, which
 * lowers and raises the block. */
void HUD_DrawHeld(BlockID selected, short swing);

/* Text helpers: the HUD draws its own labels so the strip and the hotbar share
 * one font and one colour convention. */
void HUD_SetTextStyle(void);

#endif
