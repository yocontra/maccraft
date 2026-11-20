#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <Events.h>
#include <Windows.h>

/* Window types. kWindowTypeCount is the sentinel every entry point checks
 * against, instead of a bare literal 2. */
enum { kWindowTypeSplash = 0, kWindowTypeGame = 1, kWindowTypeCount = 2 };
typedef unsigned short WindowType;

/* The module table itself is private to window_manager.c: callers only ever
 * name a WindowType, so the vtable does not belong in the public header. */

/* Initialize the window manager and the initial window.
 * Returns false when the initial window could not be created. */
Boolean WindowManager_Initialize(void);

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void);

/* Open a specific type of window */
void WindowManager_OpenWindow(WindowType windowType);

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType);

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType);

/* Get the current foreground window type */
WindowType WindowManager_GetForegroundWindowType(void);

/* Get window reference for a specific window type */
WindowRef WindowManager_GetWindowRef(WindowType windowType);

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType);

/* Harness: which window type keystrokes are routed to. */
short WindowManager_ForegroundForTest(void);

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event);

/* Perform idle-time processing */
void WindowManager_Idle(void);

#endif /* WINDOW_MANAGER_H */
