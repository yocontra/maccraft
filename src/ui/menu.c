#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
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

    /* First determine which window is in front to properly set menu items */
    switch (gAppMode) {
    case kModeMainSplash:
        /* In splash mode, Close is disabled unless a window is open */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeSplash))
            EnableItem(fileMenu, kItemClose); /* Can close main window */
        else
            DisableItem(fileMenu, kItemClose);

        /* Always enable New Game menu item in splash mode */
        EnableItem(fileMenu, kItemNewGame);

        /* Disable Game menu in splash mode */
        DisableItem(gameMenu, 0); /* Disable entire menu */
        break;

    case kModeGameWindow:
        /* In game mode, Close will close the game window */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeGame))
            EnableItem(fileMenu, kItemClose); /* Can close game window */
        else
            DisableItem(fileMenu, kItemClose);

        /* Enable Game menu in game mode */
        EnableItem(gameMenu, 0); /* Enable entire menu */
        break;
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
            WindowManager_OpenWindow(kWindowTypeAbout);
        else {
            GetMenuItemText(GetMenu(128), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if (menuID == kMenuFile) {
        switch (menuItem) {
        case kItemNewGame:
            /* This switches between modes */
            if (gAppMode == kModeMainSplash || !WindowManager_IsWindowVisible(kWindowTypeGame)) {
                /* Open game window and close splash window */
                WindowManager_OpenWindow(kWindowTypeGame);
                WindowManager_CloseWindow(kWindowTypeSplash);
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
                    /* Close main window (not normally done - usually = quit) */
                    QuitApplication(false);
                }
                else {
                    /* Other window */
                    DisposeWindow(w);
                }
            }
            break;

        case kItemQuit:
            /* Clean up resources before quitting using standard function */
            QuitApplication(false);
            break;
        }
    }
    else if (menuID == kMenuGame) {
        /* Only handle game menu commands when in game mode */
        if (gAppMode == kModeGameWindow && WindowManager_IsWindowVisible(kWindowTypeGame)) {
            /* Handle game menu items */
            switch (menuItem) {
            case kItemToggleBlock:
                /* Toggle between different block types */
                GameWindow_ToggleBlockType();
                break;

            case kItemResetWorld:
                /* Reset the game world */
                /* World reset logic would go here */
                break;
            }
        }
    }

    HiliteMenu(0);
}