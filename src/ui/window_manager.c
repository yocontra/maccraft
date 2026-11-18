#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "event.h"
#include "game_window.h"
#include "menu.h"
#include "splash_window.h"
#include "window_manager.h"

/*
 * Window Manager module that coordinates operations between
 * individual window modules using a standardized interface
 */

/* Window modules registration */
static WindowModule gWindowModules[2];

/* Current active window tracking */
static WindowType gForegroundWindowType = kWindowTypeSplash;

/* The game window module keeps the historical void-returning API; adapt it to
 * the Boolean module interface so the manager can report creation failures. */
static Boolean GameModule_Initialize(void)
{
    GameWindow_Initialize();
    return GameWindow_GetWindowRef() != NULL;
}

static Boolean GameModule_Show(Boolean visible)
{
    GameWindow_Show(visible);
    return visible ? GameWindow_IsVisible() : true;
}

/* Register window module functions */
static void RegisterWindowModules(void)
{
    /* Splash window */
    gWindowModules[kWindowTypeSplash].initialize   = SplashWindow_Initialize;
    gWindowModules[kWindowTypeSplash].dispose      = SplashWindow_Dispose;
    gWindowModules[kWindowTypeSplash].show         = SplashWindow_Show;
    gWindowModules[kWindowTypeSplash].handleEvent  = SplashWindow_HandleEvent;
    gWindowModules[kWindowTypeSplash].getWindowRef = SplashWindow_GetWindowRef;
    gWindowModules[kWindowTypeSplash].isVisible    = SplashWindow_IsVisible;
    gWindowModules[kWindowTypeSplash].initialized  = false;
    gWindowModules[kWindowTypeSplash].visible      = false;

    /* Game window */
    gWindowModules[kWindowTypeGame].initialize   = GameModule_Initialize;
    gWindowModules[kWindowTypeGame].dispose      = GameWindow_Dispose;
    gWindowModules[kWindowTypeGame].show         = GameModule_Show;
    gWindowModules[kWindowTypeGame].handleEvent  = GameWindow_HandleEvent;
    gWindowModules[kWindowTypeGame].getWindowRef = GameWindow_GetWindowRef;
    gWindowModules[kWindowTypeGame].isVisible    = GameWindow_IsVisible;
    gWindowModules[kWindowTypeGame].initialized  = false;
    gWindowModules[kWindowTypeGame].visible      = false;
}

/* Initialize the window manager and the initial window */
Boolean WindowManager_Initialize(void)
{
    /* Register all window modules first */
    RegisterWindowModules();

    /* Initialize splash window as the default view */
    if (!gWindowModules[kWindowTypeSplash].initialize()) {
        return false;
    }
    gWindowModules[kWindowTypeSplash].initialized = true;

    if (!gWindowModules[kWindowTypeSplash].show(true)) {
        return false;
    }
    gWindowModules[kWindowTypeSplash].visible = true;

    /* Set the splash window as foreground */
    gForegroundWindowType = kWindowTypeSplash;

    return true;
}

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void)
{
    int i;

    /* Dispose windows that were initialized */
    for (i = 0; i < 2; i++) {
        if (gWindowModules[i].initialized) {
            gWindowModules[i].dispose();
            gWindowModules[i].initialized = false;
            gWindowModules[i].visible     = false;
        }
    }
}

/* Open a specific type of window */
void WindowManager_OpenWindow(WindowType windowType)
{
    if (windowType >= 2) {
        return;
    }

    /* Initialize the window if it hasn't been initialized yet */
    if (!gWindowModules[windowType].initialized) {
        if (!gWindowModules[windowType].initialize()) {
            return;
        }
        gWindowModules[windowType].initialized = true;
    }

    /* Show the window */
    if (!gWindowModules[windowType].show(true)) {
        return;
    }
    gWindowModules[windowType].visible = true;

    WindowManager_SetForegroundWindow(windowType);
}

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType)
{
    if (windowType >= 2) {
        return;
    }

    /* Only hide the window if it was initialized */
    if (gWindowModules[windowType].initialized) {
        gWindowModules[windowType].show(false);
        gWindowModules[windowType].visible = false;
    }
}

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType)
{
    WindowRef windowRef;

    /* Ignore if the window hasn't been initialized */
    if (!gWindowModules[windowType].initialized) {
        return;
    }

    /* Get the window reference */
    windowRef = gWindowModules[windowType].getWindowRef();
    if (windowRef == NULL) {
        return;
    }

    /* Bring the window to front and select it */
    ShowWindow(windowRef);
    SelectWindow(windowRef);

    /* Update the foreground window type */
    gForegroundWindowType = windowType;

    /* Update application mode for compatibility with existing code */
    switch (windowType) {
    case kWindowTypeSplash:
        gAppMode = kModeMainSplash;
        break;

    case kWindowTypeGame:
        gAppMode = kModeGameWindow;
        break;

    default:
        /* No change to app mode for other window types */
        break;
    }

    /* Update menus based on active window */
    UpdateMenus();
}

/* Get the current foreground window type */
WindowType WindowManager_GetForegroundWindowType(void)
{
    return gForegroundWindowType;
}

/* Get window reference for a specific window type */
WindowRef WindowManager_GetWindowRef(WindowType windowType)
{
    if (windowType < 2 && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].getWindowRef();
    }
    return NULL;
}

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType)
{
    if (windowType < 2 && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].isVisible();
    }
    return false;
}

/* Handle standard window operations like dragging, closing, etc. */
static Boolean HandleStandardWindowOps(EventRecord *event)
{
    WindowRef window;
    short part;

    if (event->what == mouseDown) {
        part = FindWindow(event->where, &window);

        if (part == inMenuBar) {
            /* Handle menu clicks */
            UpdateMenus();
            DoMenuCommand(MenuSelect(event->where));
            return true;
        }
        else if (part == inSysWindow) {
            /* Handle clicks in system windows (desk accessories) */
            SystemClick(event, window);
            return true;
        }
        else if (part == inDrag) {
            /* User is dragging a window */
            DragWindow(window, event->where, &qd.screenBits.bounds);
            return true;
        }
        else if (part == inGoAway) {
            /* User clicked window close box */
            if (TrackGoAway(window, event->where)) {
                if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                    /* Closing main window quits app */
                    QuitApplication(false);
                }
                else if (window == gWindowModules[kWindowTypeGame].getWindowRef()) {
                    /* Closing game window returns to splash screen */
                    WindowManager_CloseWindow(kWindowTypeGame);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else {
                    DisposeWindow(window);
                }
            }
            return true;
        }
        else if (part == inContent && window != FrontWindow()) {
            /* Activate clicked window */
            SelectWindow(window);

            /* Update foreground window type */
            if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeSplash);
            }
            else if (window == gWindowModules[kWindowTypeGame].getWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeGame);
            }
            return true;
        }
    }

    return false;
}

/* Handle window activation events */
static void HandleWindowActivation(EventRecord *event)
{
    WindowRef window;
    Boolean becomingActive;

    if (event->what == activateEvt) {
        window         = (WindowRef)event->message;
        becomingActive = (event->modifiers & activeFlag) != 0;

        /* Update the foreground window type if a window is being activated */
        if (becomingActive) {
            if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                gForegroundWindowType = kWindowTypeSplash;
                gAppMode              = kModeMainSplash;
            }
            else if (window == gWindowModules[kWindowTypeGame].getWindowRef()) {
                gForegroundWindowType = kWindowTypeGame;
                gAppMode              = kModeGameWindow;
            }
        }
    }
}

/* Route update events to the appropriate window */
static Boolean RouteUpdateEvent(EventRecord *event)
{
    WindowRef window;
    int i;

    if (event->what == updateEvt) {
        window = (WindowRef)event->message;

        /* Find which window needs updating */
        for (i = 0; i < 2; i++) {
            if (window == gWindowModules[i].getWindowRef() && gWindowModules[i].isVisible()) {
                gWindowModules[i].handleEvent(event);
                return true;
            }
        }
    }

    return false;
}

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event)
{
    int i;
    WindowRef clicked;

    /* Handle standard window operations first */
    if (HandleStandardWindowOps(event)) {
        return;
    }

    /* Handle window activation events */
    HandleWindowActivation(event);

    /* Route update events to specific windows */
    if (RouteUpdateEvent(event)) {
        return;
    }

    /* Route mouse events to the window that was actually clicked */
    if (event->what == mouseDown) {
        FindWindow(event->where, &clicked);
        for (i = 0; i < 2; i++) {
            if (clicked == gWindowModules[i].getWindowRef() &&
                gWindowModules[i].initialized && gWindowModules[i].visible) {
                gWindowModules[i].handleEvent(event);
                return;
            }
        }
        return;
    }

    /* Route other events to the foreground window */
    if (gForegroundWindowType < 2 && gWindowModules[gForegroundWindowType].initialized &&
        gWindowModules[gForegroundWindowType].visible) {
        gWindowModules[gForegroundWindowType].handleEvent(event);
    }
}

/* Perform idle-time processing */
void WindowManager_Idle(void)
{
    /* Only need idle processing for game window */
    if (gForegroundWindowType == kWindowTypeGame && gWindowModules[kWindowTypeGame].initialized &&
        gWindowModules[kWindowTypeGame].visible) {
        GameWindow_Idle(); /* Direct call since not part of standard interface */
    }
}