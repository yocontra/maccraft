#ifndef GAME_WINDOW_H
#define GAME_WINDOW_H

#include <Windows.h>

/* Initialize and create the game window */
void GameWindow_Initialize(void);

/* Dispose of the game window and clean up resources */
void GameWindow_Dispose(void);

/* Handle all events for the game window */
void GameWindow_HandleEvent(EventRecord *event);

/* Get the window reference for the game window */
WindowRef GameWindow_GetWindowRef(void);

/* Show or hide the game window */
void GameWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean GameWindow_IsVisible(void);

/* Harness hook: repaint `iterations` times, return elapsed ticks. */
unsigned long GameWindow_TimeRender(short iterations, short *outBlocks);

/* Perform idle processing */
void GameWindow_Idle(void);

/* Toggle between block types */
void GameWindow_ToggleBlockType(void);

/* Test hooks used by src/selftest.c: same code paths a real click and a real
   arrow key take, without going through the event queue. */
Boolean GameWindow_ClickForTest(short localX, short localY, Boolean shiftDown);
Boolean GameWindow_KeyForTest(char key);
void    GameWindow_StateForTest(short *camX, short *camY, short *blockType);

#endif /* GAME_WINDOW_H */