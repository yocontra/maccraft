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

    /* The plateau is two blocks high at 24,24; the display is centred on it. */
    topZ = World_TopZ(24, 24);
    LogLine("topz_before=", NULL, topZ, true);

    /* Place a block through the same handler a click runs. The click point is
     * the display centre, which is where tile 24,24 projects. */
    GameWindow_StateForTest(&camX, &camY, &blockType);
    centreX = 300;
    centreY = 200;
    LogLine("click_placed=", NULL,
            (long)GameWindow_ClickForTest(centreX, centreY, false), true);
    Pump(20);

    /* Remove it again with a shift-click on the same point. */
    LogLine("click_removed=", NULL,
            (long)GameWindow_ClickForTest(centreX, centreY, true), true);
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

    /* Hold the final state for ~12 s so the harness can photograph it. */
    Pump(1800);
    LogLine("done", NULL, 0, false);

    if (sLogRef >= 0) {
        FSClose(sLogRef);
        sLogRef = -1;
    }

    /* The harness reads the log off the clone after the VM is killed, so push
     * it out of the guest's cache first. */
    FlushVol(NULL, 0);
    ExitToShell();
}
