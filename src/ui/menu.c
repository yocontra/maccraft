#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"
#include "game_window.h"
#include "menu.h"
#include "window_manager.h"

/*********************************************************************
 * MENU HANDLING FUNCTIONS
 *********************************************************************/

/* Update the menu state based on current application mode */
void UpdateMenus(void)
{
    MenuRef fileMenu = GetMenu(kMenuFile);
    MenuRef gameMenu = GetMenu(kMenuGame);
    WindowRef w      = FrontWindow();

    /* Menu state follows the window manager's foreground state. This used to
     * read gAppMode, a second copy of the same fact that could go stale; ask the
     * owner instead. */
    if (WindowManager_GetForegroundWindowType() == kWindowTypeGame) {
        /* Game mode: Close closes the game window, and the Game menu is live. */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeGame))
            EnableItem(fileMenu, kItemClose);
        else
            DisableItem(fileMenu, kItemClose);

        EnableItem(gameMenu, 0);
    }
    else {
        /* Splash mode: Close closes the main window, New Game is always
         * available, and the Game menu is disabled outright. */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeSplash))
            EnableItem(fileMenu, kItemClose);
        else
            DisableItem(fileMenu, kItemClose);

        EnableItem(fileMenu, kItemNewGame);
        DisableItem(gameMenu, 0);
    }
}

/* Handle menu commands */
void DoMenuCommand(long menuCommand)
{
    Str255 str;
    WindowRef w;
    short menuID   = menuCommand >> 16;
    short menuItem = menuCommand & 0xFFFF;

    if (menuID == kMenuApple) {
        if (menuItem == kItemAbout)
            AboutWindow_Show();
        else {
            GetMenuItemText(GetMenu(128), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if (menuID == kMenuFile) {
        switch (menuItem) {
        case kItemNewGame:
            /* This switches between modes */
            if (!WindowManager_IsWindowVisible(kWindowTypeGame)) {
                /* Open game window and close splash window only on success */
                WindowManager_OpenWindow(kWindowTypeGame);
                if (WindowManager_IsWindowVisible(kWindowTypeGame)) {
                    WindowManager_CloseWindow(kWindowTypeSplash);
                }
            }
            else {
                /* Already in game mode, bring window to front */
                WindowRef w = WindowManager_GetWindowRef(kWindowTypeGame);
                if (w != NULL) {
                    SelectWindow(w);
                }
            }
            break;

        case kItemClose:
            w = FrontWindow();
            if (w) {
                if (GetWindowKind(w) < 0) {
                    /* Close desk accessory */
                    CloseDeskAcc(GetWindowKind(w));
                }
                else if (w == WindowManager_GetWindowRef(kWindowTypeGame)) {
                    /* Close game window and switch back to splash mode */
                    WindowManager_CloseWindow(kWindowTypeGame);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else if (w == WindowManager_GetWindowRef(kWindowTypeSplash)) {
                    /* Closing the main window quits, and the world is saved
                     * here rather than at the caller: it is the only copy. */
                    QuitApplication(true);
                }
                else {
                    /* Other window */
                    DisposeWindow(w);
                }
            }
            break;

        case kItemQuit:
            QuitApplication(true);
            break;
        }
    }
    else if (menuID == kMenuGame) {
        /* Only handle game menu commands when in game mode */
        if (WindowManager_IsWindowVisible(kWindowTypeGame)) {
            /* Handle game menu items */
            switch (menuItem) {
            case kItemToggleBlock:
                /* Toggle between different block types */
                GameWindow_ToggleBlockType();
                break;

            case kItemSaveWorld:
                GameWindow_SaveWorld();
                break;
            case kItemDayNight:
                GameWindow_ToggleDayNight();
                break;
            case kItemResetWorld:
                /* Rebuild the island and put the player back on it: the menu
                 * promised a reset and used to do nothing at all. */
                GameWindow_ResetWorld();
                break;
            }
        }
    }

    HiliteMenu(0);
}