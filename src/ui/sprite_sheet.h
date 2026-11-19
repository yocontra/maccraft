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

/* The window's global position, as it was created. */
void Sprite_SetOrigin(short dx, short dy);

void Sprite_Draw(short left, short top, BlockID id, short face, short shade);
short Sprite_ShadeStep(short factor);

#endif
