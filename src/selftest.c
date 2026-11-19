/* selftest.c - exercise the game from inside the guest and write a log.
 *
 * The host-side emulator harness cannot be trusted with synthetic input: a
 * modal tracking loop can lose the button-up, and posting events moves the real
 * cursor of whoever is using the Mac. This runs the same code a click and an
 * arrow key run, and leaves a text log for the harness to read.
 *
 * The harness places "MacCraft selftest" in the clone's System Folder:
 * Preferences before boot and reads "MacCraft selftest log" afterwards.
 */

#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <string.h>
#include <Script.h>

#include "constants.h"
#include "game/iso.h"
#include "game/world.h"
#include "selftest.h"
#include "sound/sfx.h"
#include "game/save.h"
#include "game/blocks.h"
#include "ui/game_window.h"
#include "ui/window_manager.h"

static const unsigned char kTriggerName[] = "\pMacCraft selftest";
static const unsigned char kLogName[] = "\pMacCraft selftest log";

static short sLogRef = -1;

static Boolean FindPrefsFile(FSSpec *spec, ConstStr255Param name)
{
    short vRefNum;
    long dirID;
    OSErr err;

    if (spec == NULL) {
        return false;
    }
    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                     &vRefNum, &dirID);
    if (err != noErr) {
        return false;
    }
    err = FSMakeFSSpec(vRefNum, dirID, name, spec);
    return (err == noErr || err == fnfErr);
}

Boolean SelfTest_Requested(void)
{
    FSSpec spec;

    if (!FindPrefsFile(&spec, kTriggerName)) {
        return false;
    }
    return FSMakeFSSpec(spec.vRefNum, spec.parID, kTriggerName, &spec) == noErr;
}

static char *AppendStr(char *p, const char *text)
{
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != '\0') {
        *p++ = *text++;
    }
    return p;
}

static char *AppendNum(char *p, long value)
{
    char digits[12];
    short n = 0;
    Boolean negative = false;

    if (value < 0) {
        negative = true;
        value = -value;
    }
    do {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && n < 11);

    if (negative) {
        *p++ = '-';
    }
    while (n > 0) {
        *p++ = digits[--n];
    }
    return p;
}

/* log_line("open=", "ok", 12) — text and/or a number, then CR. */


static void LogLine(const char *a, const char *b, long num, Boolean withNum)
{
    char line[300];
    char *p = line;
    long count;

    if (sLogRef < 0) {
        return;
    }
    p = AppendStr(p, a);
    if (b != NULL) {
        p = AppendStr(p, b);
    }
    if (withNum) {
        p = AppendNum(p, num);
    }
    *p++ = '\r';
    *p = '\0';

    count = (long)(p - line);
    SetFPos(sLogRef, fsFromLEOF, 0);
    FSWrite(sLogRef, &count, line);
}

static Boolean OpenLog(void)
{
    FSSpec spec;
    OSErr err;

    if (!FindPrefsFile(&spec, kLogName)) {
        return false;
    }
    err = FSpCreate(&spec, 'MCft', 'TEXT', smSystemScript);
    if (err != noErr && err != dupFNErr) {
        return false;
    }
    err = FSpOpenDF(&spec, fsRdWrPerm, &sLogRef);
    if (err != noErr) {
        sLogRef = -1;
        return false;
    }
    SetEOF(sLogRef, 0);
    return true;
}

static void Pump(unsigned long ticks)
{
    unsigned long until = TickCount() + ticks;

    while (TickCount() < until) {
        EventRecord event;

        SystemTask();

        /* Dispatch, not discard: update events are what make the window
         * manager paint. A pump that swallowed them produced blank windows. */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            WindowManager_HandleEvent(&event);
        }
        GameWindow_Idle();
    }
}

void SelfTest_Run(void)
{
    short camX = 0, camY = 0, blockType = 0;
    short topZ;
    short centreX, centreY;
    short i;

    if (!OpenLog()) {
        return;
    }

    LogLine("maccraft selftest ", APP_VERSION_STR, 0, false);
    LogLine("world_x=", NULL, kWorldX, true);
    LogLine("world_y=", NULL, kWorldY, true);
    LogLine("world_z=", NULL, kWorldZ, true);

    /* Open the game window the way the splash's Start button does, so the
     * frames captured during this run show the real play area. */
    WindowManager_OpenWindow(kWindowTypeGame);
    Pump(90);
    if (WindowManager_IsWindowVisible(kWindowTypeSplash)) {
        WindowManager_CloseWindow(kWindowTypeSplash);
    }
    Pump(60);

    LogLine("game_visible=", NULL, (long)GameWindow_IsVisible(), true);
    /* Persistence: save, clobber a cell, reload, and check it came back. */
    {
        BlockID before = World_Get(24, 24, World_TopZ(24, 24));

        OSErr wrote = Save_Write();
        OSErr read;
        short z = World_TopZ(24, 24);

        LogLine("save_write=", NULL, (long)wrote, true);
        World_Set(24, 24, z, kBlockEmpty);
        read = Save_Read();
        LogLine("save_read=", NULL, (long)read, true);
        /* The round trip only counts when both calls succeeded: a failed write
         * and a failed read leave the world unchanged, which looks like success
         * if you only compare the cell. */
        LogLine("save_roundtrip=", NULL,
                (long)(wrote == noErr && read == noErr &&
                       World_Get(24, 24, z) == before), true);
    }
    /* The player is a simulation, so the harness can prove it lands and jumps
     * rather than trusting the picture. */
    LogLine("player_grounded=", NULL, (long)GameWindow_PlayerGrounded(), true);
    LogLine("player_z=", NULL, (long)GameWindow_PlayerZ(), true);
    LogLine("player_jump_peak=", NULL, (long)GameWindow_PlayerJumpPeakForTest(), true);
    {
        short beforeX = GameWindow_PlayerTileX();
        short beforeZ = GameWindow_PlayerZ();

        /* Walk east for 20 ticks (two blocks) and report where that left us. */
        GameWindow_WalkForTest(1, 0, 20);
        LogLine("walk_dx=", NULL, (long)(GameWindow_PlayerTileX() - beforeX), true);
        GameWindow_JumpForTest();
        LogLine("jump_landed=", NULL, (long)(GameWindow_PlayerZ() == beforeZ), true);

    }
    LogLine("player_respawns=", NULL, (long)GameWindow_PlayerRespawns(), true);
    {
        OSErr err = Sfx_LastError();

        LogLine("sfx_lasterr=", NULL, (long)err, true);
        Sfx_Play(kSfxPlace);
        LogLine("sfx_after_play=", NULL, (long)Sfx_LastError(), true);
    }
    {
        short blocks = 0;
        unsigned long ticks = GameWindow_TimeRender(10, &blocks);

        extern long Sprite_Timing[4];

        LogLine("render_ticks_10=", NULL, (long)ticks, true);
        LogLine("sky_ticks_10=", NULL, Sprite_Timing[0], true);
        LogLine("world_ticks_10=", NULL, Sprite_Timing[1], true);
        LogLine("draw_ticks_10=", NULL, Sprite_Timing[2], true);
        LogLine("nodraw_ticks_10=", NULL, Sprite_Timing[1], true);
        (void)blocks;
    }
    GameWindow_StateForTest(&camX, &camY, &blockType);
    LogLine("cam_x=", NULL, camX, true);
    LogLine("cam_y=", NULL, camY, true);
    LogLine("block_type=", NULL, blockType, true);

    {
        short topZBefore = 0;

    /* The plateau is two blocks high at 24,24; the display is centred on it. */
    topZ = World_TopZ(24, 24);
    topZBefore = topZ;
    LogLine("topz_before=", NULL, topZ, true);

    /* Place a block through the same handler a click runs. The click point is
     * the display centre, which is where tile 24,24 projects. */
    GameWindow_StateForTest(&camX, &camY, &blockType);
    centreX = 300;
    centreY = 200;
    LogLine("click_placed=", NULL,
            (long)GameWindow_ClickForTest(centreX, centreY, false), true);
    Pump(20);

    /* Remove it again: a shift-click starts mining now, so the break is driven
     * to completion through the same path the button-hold uses. */
    LogLine("click_started_mine=", NULL,
            (long)GameWindow_ClickForTest(centreX, centreY, true), true);
    GameWindow_MineForTest(24, 24, 0, 60);
    LogLine("click_removed=", NULL, (long)(World_TopZ(24, 24) < topZBefore), true);
    }
    Pump(20);
    LogLine("topz_after=", NULL, World_TopZ(24, 24), true);

    /* Pan with the arrow key the keyboard handler uses. */
    for (i = 0; i < 5; i++) {
        GameWindow_KeyForTest(0x1D); /* right arrow */
    }
    Pump(20);
    GameWindow_StateForTest(&camX, &camY, &blockType);
    LogLine("cam_after_pan_x=", NULL, camX, true);
    LogLine("cam_after_pan_y=", NULL, camY, true);

    /* Block selector cycling. */
    LogLine("select_2=", NULL, (long)GameWindow_KeyForTest('2'), true);
    GameWindow_StateForTest(&camX, &camY, &blockType);
    LogLine("block_type_after=", NULL, blockType, true);

    /* Hold the final state for ~12 s so the harness can photograph it. A
     * "hold" trigger in Preferences skips the exit entirely, which is what
     * interactive checks need: the game window has to stay up to be driven. */
    Pump(1800);
    LogLine("done", NULL, 0, false);

    if (sLogRef >= 0) {
        FSClose(sLogRef);
        sLogRef = -1;
    }

    /* The harness reads the log off the clone after the VM is killed, so push
     * it out of the guest's cache first. */
    FlushVol(NULL, 0);

    {
        static const unsigned char kHoldName[] = "\pMacCraft selftest hold";
        FSSpec holdSpec;

        if (FindPrefsFile(&holdSpec, kHoldName)) {
            /* Stay resident: the harness drives the running game. */
            return;
        }
    }

    ExitToShell();
}
