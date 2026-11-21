#ifndef SFX_H
#define SFX_H

#include <Types.h>

/* The implementation sizes its effect table with kSfxCount, so the count must
   stay last; the game only ever names the effects, never their numbers. */
typedef enum {
    kSfxClick = 0,
    kSfxPlace,
    kSfxBreak,
    kSfxStep,
    kSfxJump,      /* take-off: a short rise */
    kSfxLand,      /* landing: a low thud */
    kSfxSplash,    /* entering water */
    kSfxBreakStone,/* mining stone: harder and lower than grass */
    kSfxStepStone, /* a footstep on stone, not soil */
    kSfxAmbientWater, /* the pond, heard from nearby */
    kSfxCount
} SfxKind;

OSErr Sfx_Init(void);
void  Sfx_Play(SfxKind kind);
void  Sfx_Dispose(void);
OSErr Sfx_LastError(void);

#endif
