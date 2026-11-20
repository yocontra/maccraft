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

/* Rebuild the island and respawn the player. */
void GameWindow_ResetWorld(void);

/* Write the world to disk now. */
OSErr GameWindow_SaveWorld(void);

/* Resolve a point in the play area to a block and the face under it. Used by
 * the harness verbs that click and by the click handler itself. */
Boolean GameWindow_PickAt(short localX, short localY, short *outX, short *outY,
                          short *outZ, short *outFace);

/* Select a block kind by index (0-based); false when out of range. */
Boolean GameWindow_SelectBlockType(short index);

/* The pointer in window coordinates. GetMouse reports in the current port's
 * coordinates, so the port has to be set for it to be window-relative. */
void GameWindow_CursorLocal(Point *out);

/* Where a tile's block projects on screen, in window coordinates. */
void GameWindow_TileScreen(short x, short y, short z, short *sx, short *sy);

/* Player state for the harness. */
Boolean GameWindow_PlayerGrounded(void);
short GameWindow_PlayerZ(void);
short GameWindow_PlayerTileX(void);
short GameWindow_PlayerTileY(void);
short GameWindow_PlayerRespawns(void);
short GameWindow_PlayerJumpPeakForTest(void);
void GameWindow_WalkForTest(short dx, short dy, short ticks);
void GameWindow_MineForTest(short x, short y, short z, short ticks);
void GameWindow_JumpForTest(void);

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