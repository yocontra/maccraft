#include <Events.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "event.h"
#include "game_window.h"
#include "window_manager.h"

/*********************************************************************
 * APPLICATION CONTROL FUNCTIONS
 *********************************************************************/

/* Quit the application.
 *
 * `saveChanges` used to be ignored, and two of the three quit paths passed
 * false, so closing the splash window threw away a world the player had just
 * been editing. The flag is honoured here instead of at every caller. */
void QuitApplication(Boolean saveChanges)
{
    if (saveChanges) {
        (void)GameWindow_SaveWorld();
    }

    /* Clean up windows through the window manager */
    WindowManager_Dispose();

    /* Exit application */
    ExitToShell();
}