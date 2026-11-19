/* sprite_sheet.h - baked, shaded block faces drawn straight to the screen. */
#ifndef SPRITE_SHEET_H
#define SPRITE_SHEET_H

#include <Quickdraw.h>
#include "../game/world.h"

#define kSpriteW 32
#define kSpriteH 16
#define kSpriteShades 4

Boolean Sprite_Init(void);

/* Lock the screen for a frame's worth of Sprite_Draw calls, and release it. */
void Sprite_BeginFrame(void);
void Sprite_EndFrame(void);
void Sprite_Dispose(void);
Boolean Sprite_Ready(void);

/* Play area, in the window's local coordinates; blits are trimmed to it. */
void Sprite_SetView(const Rect *view);

/* Shift the pixels inside r by (dx, dy) within the screen. Used to scroll the
 * previous frame when the camera pans, so only the exposed strip is redrawn. */
void Sprite_ScrollRect(const Rect *r, short dx, short dy);

/* Fill a rectangle in the window's local coordinates with an 0x00RRGGBB colour,
 * through the sprite pixel path. Used by the HUD. */
void Sprite_FillRect(const Rect *r, unsigned long rgb);

/* The window's global position, as it was created. */
void Sprite_SetOrigin(short dx, short dy);

void Sprite_Draw(short left, short top, BlockID id, short face, short shade);

/* Same, with each pixel magnified by `scale` (1 or 2). Used for the held block. */
void Sprite_DrawScaled(short left, short top, BlockID id, short face, short shade, short scale);
short Sprite_ShadeStep(short factor);

#endif
