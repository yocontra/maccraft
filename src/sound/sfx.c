/*
 * sfx.c - the game's sound effects, synthesised by the Sound Manager.
 *
 * There are no 'snd ' resources and no sample data.  Each effect is a
 * format-2 sound command list built once in static memory at init and played
 * through a single square-wave channel.  The list layout mirrors Apple's
 * Snd2ListResource: format (2), reference count, command count, then 8-byte
 * SndCommand records.  noteCmd (33) is the original Sound Driver note command;
 * the Sound Manager C interfaces and Rez template stopped declaring it, but
 * the Sound Manager keeps it for compatibility with existing 'snd ' resources
 * that use it, and it takes a MIDI pitch and a duration in ticks, which avoids
 * having to encode freqDurationCmd's 16.16 fixed-point frequency.  ampCmd
 * (param1, 0..255) sets the per-effect level so the step sound can be quiet
 * without changing the hardware volume or overlapping two channels.
 *
 * Sound Manager calls, and why each one is here:
 *   SndNewChannel(squareWaveSynth) - one channel for the life of the app;
 *       creating it per effect would allocate and could drop the first
 *       milliseconds of every sound.
 *   SndDoImmediate(flushCmd)       - discard whatever is still queued so a
 *       new effect is heard now instead of after the previous one (walking
 *       emits step sounds faster than their duration).
 *   SndPlay(list, async = true)    - queue the effect and return immediately;
 *       the game loop must never stall for a 130 ms placement tone.
 *   SndDisposeChannel(quietNow)    - stop playback and release the channel on
 *       the quit path.
 *
 * List ownership: SndPlay never disposes the list it is given.  The list must
 * be a real, relocatable handle because the Sound Manager locks it while an
 * asynchronous sound is playing; casting a static buffer to SndListHandle
 * would make the Memory Manager rewrite the word in front of that buffer.
 * The command bytes live in static memory; only one 30-byte copy per effect
 * sits in the heap, allocated at init and freed in Sfx_Dispose.
 *
 * Degradation: a machine without the Sound Manager in ROM fails
 * SndNewChannel, and every later call becomes a silent no-op instead of an
 * error or a crash.  Sound is a nicety; the game is not.
 */

#include <Memory.h>
#include <Sound.h>
#include <Types.h>

#include "sfx.h"

/* The command number that the Sound Manager C interfaces no longer expose. */
#define kSfxNoteCmd 33

/* The longest effect is break: amplitude plus two notes. */
#define kSfxMaxCommands 3

typedef struct
{
    short      format;    /* secondSoundFormat */
    short      refCount;  /* unused by the Sound Manager; zero for new lists */
    short      numCommands;
    SndCommand commands[kSfxMaxCommands];
} SfxList;

/* SndPlay walks the list as a byte stream, so its in-memory layout must match
   the resource format exactly.  Both checks fail the build rather than ship
   lists that play garbage silence. */
typedef char SfxCommandSizeIsEight[(sizeof(SndCommand) == 8) ? 1 : -1];
typedef char SfxListLayoutIsPacked[
    (sizeof(SfxList) == 6 + kSfxMaxCommands * 8) ? 1 : -1];

static SfxList       sTemplates[kSfxCount];
static SndListHandle sLists[kSfxCount];
static SndChannelPtr sChannel = NULL;
static OSErr         sLastError = noErr;
static OSErr         sInitError = noErr;
static Boolean       sInitTried = false;

static void Sfx_ClearCommand(SfxList *list, short index)
{
    list->commands[index].cmd = nullCmd;
    list->commands[index].param1 = 0;
    list->commands[index].param2 = 0;
}

static void Sfx_SetAmp(SfxList *list, short index, short amplitude)
{
    list->commands[index].cmd = ampCmd;
    list->commands[index].param1 = amplitude;
    list->commands[index].param2 = 0;
}

static void Sfx_SetNote(SfxList *list, short index, short pitch, short ticks)
{
    list->commands[index].cmd = kSfxNoteCmd;
    list->commands[index].param1 = pitch;
    list->commands[index].param2 = (long)ticks;
}

/* One square-wave voice, so the effects are told apart by pitch, length and
   level only: click is a bright tick, place a low long thunk, break a
   descending pair, step a very short quiet blip. */
static void Sfx_BuildEffects(void)
{
    SfxList *list;
    short i;
    short j;

    for (i = 0; i < kSfxCount; i++)
    {
        list = &sTemplates[i];
        list->format = secondSoundFormat;
        list->refCount = 0;
        list->numCommands = 0;
        for (j = 0; j < kSfxMaxCommands; j++)
        {
            Sfx_ClearCommand(list, j);
        }
    }

    list = &sTemplates[kSfxClick];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 170);
    Sfx_SetNote(list, 1, 84, 2); /* C6, 33 ms */

    list = &sTemplates[kSfxPlace];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 220);
    Sfx_SetNote(list, 1, 48, 8); /* C3, 133 ms */

    list = &sTemplates[kSfxBreak];
    list->numCommands = 3;
    Sfx_SetAmp(list, 0, 200);
    Sfx_SetNote(list, 1, 72, 2); /* C5 */
    Sfx_SetNote(list, 2, 60, 3); /* falling to C4 */

    list = &sTemplates[kSfxStep];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 60);
    Sfx_SetNote(list, 1, 40, 1); /* E2, one tick */

    /* Take-off rises, landing falls: two notes in opposite directions is
     * enough to tell them apart without a second voice. */
    list = &sTemplates[kSfxJump];
    list->numCommands = 3;
    Sfx_SetAmp(list, 0, 120);
    Sfx_SetNote(list, 1, 60, 2); /* C4 */
    Sfx_SetNote(list, 2, 72, 2); /* up to C5 */

    list = &sTemplates[kSfxLand];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 180);
    Sfx_SetNote(list, 1, 36, 4); /* C2, short and low */

    /* Water: a fast two-note wobble, quiet enough to sit under the step
     * sounds while wading. */
    list = &sTemplates[kSfxSplash];
    list->numCommands = 3;
    Sfx_SetAmp(list, 0, 110);
    Sfx_SetNote(list, 1, 76, 1);
    Sfx_SetNote(list, 2, 64, 2);

    /* A footstep on stone is a harder tick than on soil. */
    list = &sTemplates[kSfxStepStone];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 70);
    Sfx_SetNote(list, 1, 52, 1);

    /* The pond, from nearby: a single quiet low blip, played every couple of
     * seconds while the player is close to water. */
    list = &sTemplates[kSfxAmbientWater];
    list->numCommands = 2;
    Sfx_SetAmp(list, 0, 45);
    Sfx_SetNote(list, 1, 31, 2);

    /* Stone breaks with a lower, harder pair than grass or dirt. */
    list = &sTemplates[kSfxBreakStone];
    list->numCommands = 3;
    Sfx_SetAmp(list, 0, 210);
    Sfx_SetNote(list, 1, 55, 2);
    Sfx_SetNote(list, 2, 43, 3);
}

/* Release everything this module owns.  Idempotent: every pointer is nulled,
   and nothing is called for a resource that was never allocated.  Returns the
   last OSErr the Sound Manager produced, so the caller can decide whether it
   is worth recording. */
static OSErr Sfx_FreeResources(void)
{
    OSErr err = noErr;
    short i;

    /* The channel goes first: disposing it stops an asynchronous SndPlay and
       releases that play's lock on the list before the list is freed. */
    if (sChannel != NULL)
    {
        err = SndDisposeChannel(sChannel, true);
        sChannel = NULL;
    }

    for (i = 0; i < kSfxCount; i++)
    {
        if (sLists[i] != NULL)
        {
            DisposeHandle((Handle)sLists[i]);
            sLists[i] = NULL;
        }
    }

    return err;
}

static OSErr Sfx_Fail(OSErr err)
{
    Sfx_FreeResources();
    sInitError = err;
    sLastError = err;
    return err;
}

OSErr Sfx_Init(void)
{
    OSErr err;
    short i;

    if (sInitTried)
    {
        return sInitError;
    }
    sInitTried = true;

    Sfx_BuildEffects();

    err = SndNewChannel(&sChannel, squareWaveSynth, 0, NULL);
    if (err != noErr)
    {
        sChannel = NULL;
        return Sfx_Fail(err);
    }
    sLastError = noErr;

    for (i = 0; i < kSfxCount; i++)
    {
        sLists[i] = (SndListHandle)NewHandle((Size)sizeof(SfxList));
        if (sLists[i] == NULL)
        {
            err = MemError();
            if (err == noErr)
            {
                err = memFullErr;
            }
            return Sfx_Fail(err);
        }
        *(SfxList *)(*sLists[i]) = sTemplates[i];
    }

    return noErr;
}

void Sfx_Play(SfxKind kind)
{
    SndCommand flush;
    OSErr err;

    if (sChannel == NULL || (int)kind < 0 || (int)kind >= (int)kSfxCount)
    {
        return;
    }
    if (sLists[kind] == NULL)
    {
        return;
    }

    flush.cmd = flushCmd;
    flush.param1 = 0;
    flush.param2 = 0;
    err = SndDoImmediate(sChannel, &flush);
    if (err != noErr)
    {
        sLastError = err;
        return;
    }

    /* async = true: the Sound Manager takes the lock on the list, queues the
       commands and returns; the caller must leave the list alone until the
       sound ends, which is why the handles live until Sfx_Dispose. */
    err = SndPlay(sChannel, sLists[kind], true);
    sLastError = err;
}

void Sfx_Dispose(void)
{
    OSErr err;

    err = Sfx_FreeResources();
    sInitTried = false;
    sInitError = noErr;
    if (err != noErr)
    {
        sLastError = err;
    }
}

OSErr Sfx_LastError(void)
{
    return sLastError;
}
