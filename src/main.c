#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Sound.h>
#include <TextEdit.h>
#include <Windows.h>

#include "constants.h"
#include "error.h"
#include "selftest.h"
/* Removed sound include */
#include "ui/event.h"
#include "ui/menu.h"
#include "ui/window_manager.h"

/* Application mode tracking (used by multiple modules) */
short gAppMode = kModeMainSplash;

/*********************************************************************
 * MAIN PROGRAM
 *********************************************************************/

/* Main entry point - initial function called when application starts
 *
 * This initializes the Macintosh toolbox, sets up UI, and runs the main event loop.
 * Classic Mac applications use a cooperative multitasking model where each
 * application must regularly check for and handle system events.
 */
int main(void)
{
    OSErr err;

    /*----------------------------------------
     * Basic Macintosh Toolbox initialization
     *----------------------------------------*/
    MaxApplZone(); /* Maximize heap space */
    MoreMasters(); /* Allocate additional master pointers for handles */
    MoreMasters(); /* Call twice to ensure plenty of master pointers */

    InitGraf(&qd.thePort); /* QuickDraw */
    InitFonts();           /* Font Manager */
    InitWindows();         /* Window Manager */
    InitMenus();           /* Menu Manager */
    TEInit();              /* TextEdit */
    InitDialogs(NULL);     /* Dialog Manager */

    /* Check available memory after the Toolbox is initialized */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    InitCursor();          /* Set arrow cursor */

    /* Clear any pending events */
    FlushEvents(everyEvent, 0);

    /*----------------------------------------
     * Setup application UI
     *----------------------------------------*/
    /* Set up menu bar */
    Handle menuBar = GetNewMBar(128);
    if (menuBar == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    SetMenuBar(menuBar);

    /* Get Apple menu and append desk accessories */
    MenuHandle appleMenu = GetMenu(kMenuApple);
    if (appleMenu == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    AppendResMenu(appleMenu, 'DRVR');
    DrawMenuBar();

    /* Initialize window manager and default windows */
    if (!WindowManager_Initialize()) {
        HandleError(kErrWindowCreation, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    /* A harness can ask the game to exercise itself and quit; see selftest.c. */
    if (SelfTest_Requested()) {
        SelfTest_Run(); /* does not return */
    }

    /* Or to go straight into the game: that is what a relaunched build does when
     * the harness swaps in a new binary, rather than showing the splash and
     * waiting for a click nobody can make from the host. */
    if (SelfTest_PlayRequested()) {
        WindowManager_OpenWindow(kWindowTypeGame);
        WindowManager_CloseWindow(kWindowTypeSplash);
    }

    /*----------------------------------------
     * Main event loop
     *----------------------------------------*/
    for (;;) {
        EventRecord event;

        /* Give time to background processes */
        SystemTask();

        /* Placeholder for future audio idle function */

        /* One tick, not six: the simulation advances in the idle pass, so the
         * delay is the frame interval. Six ticks capped it at ~10 passes a
         * second, which is where the stutter came from. */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            /* MenuKey resolves every Cmd-key menu equivalent */
            if (event.what == keyDown && (event.modifiers & cmdKey)) {
                long menuCommand = MenuKey(event.message & charCodeMask);
                if (menuCommand != 0) {
                    HiliteMenu(0);
                    DoMenuCommand(menuCommand);
                    continue;
                }
            }

            /* Delegate all event handling to the window manager */
            WindowManager_HandleEvent(&event);
        }

        /* Idle on every pass, not only when WaitNextEvent times out. A held key
         * delivers an autoKey event every few ticks, so the timeout rarely
         * expires: with idle in the else branch the simulation advanced only
         * between bursts -- movement stalled, gravity ran at a fraction of its
         * rate, and the world only caught up when the keyboard went quiet. */
        WindowManager_Idle();
    }

    return 0;
}