#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../constants.h"
#include "../error.h"
#include "game_window.h"
#include "utils.h"

/* Module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;       /* Main content area */
static Rect sBlockSelectorRect; /* Block selector area at bottom */
static Rect sDisplayRect;       /* Game view area */

/* Game state variables */
static short sBlockType = 0; /* 0 = dirt, 1 = stone, 2 = grass */

/* Private function declarations */
static void GameWindow_Update(void);
static void GameWindow_Render(void);
static void GameWindow_HandleActivate(Boolean becomingActive);
static Boolean GameWindow_HandleContentClick(Point localPt);
static Boolean GameWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown);
static void DrawBlockSelector(void);

/* Initialize and create the game window */
void GameWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight, windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxOpeningGameWindow, false); /* Non-fatal */
        return;
    }

    /* Calculate a window size that fits well on the screen */
    screenWidth  = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    screenHeight = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;
    windowWidth  = (screenWidth * 4 / 5);
    windowHeight = screenHeight - 80;

    /* Create centered window */
    SetRect(&windowRect, (screenWidth - windowWidth) / 2, 40,
            (screenWidth - windowWidth) / 2 + windowWidth, 40 + windowHeight);

    /* Create the window */
    sWindow = NewWindow(NULL, &windowRect, "\pMacCraft - Game", true, documentProc, (WindowPtr)-1,
                        true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxOpeningGameWindow, false);
        return;
    }

    SetPort(sWindow);

    /* Setup main content area (whole window) */
    sContentRect = sWindow->portRect;

    /* Setup block selector area at bottom */
    SetRect(&sBlockSelectorRect, kWindowMargin,
            sContentRect.bottom - kGameBlockSelectorHeight - kWindowMargin,
            sContentRect.right - kWindowMargin, sContentRect.bottom - kWindowMargin);

    /* Setup game view area - full width, from top to block selector */
    SetRect(&sDisplayRect, kGameViewMargin, kGameViewMargin, sContentRect.right - kGameViewMargin,
            sBlockSelectorRect.top - kGameViewMargin);

    sInitialized = true;
    sIsVisible   = false;
}

/* Dispose of the game window and clean up resources */
void GameWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose window after all resources are freed */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
}

/* Draw the block selector UI at the bottom of the window */
static void DrawBlockSelector(void)
{
    Rect frameRect  = sBlockSelectorRect;
    Rect shadowRect = frameRect;
    Rect blockRect;
    GrafPtr savePort;
    Rect origClip;
    short i, blockSize, spacing;

    /* Save original clipping */
    GetPort(&savePort);
    origClip = savePort->clipRgn[0]->rgnBBox;

    /* Set clipping to entire selector area */
    Rect selectorClip = frameRect;
    selectorClip.left -= 5;
    selectorClip.right += 5;
    selectorClip.bottom += 5;
    selectorClip.top -= 5;
    ClipRect(&selectorClip);

    /* Draw shadow effect */
    PenSize(1, 1);
    OffsetRect(&shadowRect, 1, 1);
    PenPat(&qd.gray);
    FrameRect(&shadowRect);
    PenNormal();

    /* Draw a frame around the block selector area */
    ForeColor(blackColor);
    FrameRect(&frameRect);

    /* Draw a light background for the block selector */
    InsetRect(&frameRect, 1, 1);
    BackPat(&qd.white);
    BackColor(whiteColor);
    EraseRect(&frameRect);

    /* Draw the block selection blocks */
    blockSize = 24;
    spacing   = 12;

    /* Calculate center starting position */
    short totalWidth = (blockSize + spacing) * 3 - spacing;
    short startX     = frameRect.left + (frameRect.right - frameRect.left - totalWidth) / 2;
    short startY     = frameRect.top + (frameRect.bottom - frameRect.top - blockSize) / 2;

    /* Draw the three block types (dirt, stone, grass) */
    for (i = 0; i < 3; i++) {
        /* Calculate block position */
        SetRect(&blockRect, startX + i * (blockSize + spacing), startY,
                startX + i * (blockSize + spacing) + blockSize, startY + blockSize);

        /* Draw block outline */
        PenNormal();
        ForeColor(blackColor);
        FrameRect(&blockRect);

        /* Fill with appropriate pattern */
        if (i == 0) {
            /* Dirt block - dark brown */
            PenPat(&qd.dkGray);
            PaintRect(&blockRect);
        }
        else if (i == 1) {
            /* Stone block - gray */
            PenPat(&qd.gray);
            PaintRect(&blockRect);
        }
        else {
            /* Grass block - light green */
            PenPat(&qd.ltGray);
            PaintRect(&blockRect);
        }

        /* Highlight the selected block type */
        if (i == sBlockType) {
            Rect highlightRect = blockRect;
            InsetRect(&highlightRect, -2, -2);
            PenSize(2, 2);
            PenNormal();
            FrameRect(&highlightRect);
        }
    }

    /* Return to standard clipping */
    ClipRect(&origClip);
}

/* Handle mouse clicks in the content area of the game window */
static Boolean GameWindow_HandleContentClick(Point localPt)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Handle clicks in the game display area - for placement/interaction */
    if (PtInRect(localPt, &sDisplayRect)) {
        /* Game display click logic would go here */
        /* We would handle block placement or removal here */
        return true;
    }

    /* Handle clicks in the block selector area */
    if (PtInRect(localPt, &sBlockSelectorRect)) {
        /* Calculate which block might have been clicked */
        short blockSize = 24;
        short spacing   = 12;

        /* Calculate center starting position */
        Rect frameRect = sBlockSelectorRect;
        InsetRect(&frameRect, 1, 1);
        short totalWidth = (blockSize + spacing) * 3 - spacing;
        short startX     = frameRect.left + (frameRect.right - frameRect.left - totalWidth) / 2;
        short startY     = frameRect.top + (frameRect.bottom - frameRect.top - blockSize) / 2;

        /* Check each block */
        short i;
        Rect blockRect;

        for (i = 0; i < 3; i++) {
            /* Calculate block position */
            SetRect(&blockRect, startX + i * (blockSize + spacing), startY,
                    startX + i * (blockSize + spacing) + blockSize, startY + blockSize);

            /* If this block was clicked, select it */
            if (PtInRect(localPt, &blockRect)) {
                /* Set the selected block type */
                sBlockType = i;

                /* Redraw the block selector */
                InvalRect(&sBlockSelectorRect);

                /* No text feedback now that we removed text fields */

                return true;
            }
        }

        return true;
    }

    return false;
}

/* Handle key events in the game window */
static Boolean GameWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Handle command key shortcuts */
    if (isCmdDown) {
        switch (key) {
        case '1': /* Cmd-1: Select Dirt block */
            sBlockType = 0;
            InvalRect(&sBlockSelectorRect);
            return true;

        case '2': /* Cmd-2: Select Stone block */
            sBlockType = 1;
            InvalRect(&sBlockSelectorRect);
            return true;

        case '3': /* Cmd-3: Select Grass block */
            sBlockType = 2;
            InvalRect(&sBlockSelectorRect);
            return true;

        case 't': /* Cmd-T: Toggle block type (alternative) */
        case 'T':
            sBlockType = (sBlockType + 1) % 3;
            InvalRect(&sBlockSelectorRect);
            return true;
        }
    }

    /* Handle game controls */
    if (!isCmdDown) {
        switch (key) {
        case 'w': /* W: Move forward */
        case 'W':
            /* Game movement logic would go here */
            return true;

        case 's': /* S: Move backward */
        case 'S':
            /* Game movement logic would go here */
            return true;

        case 'a': /* A: Move left */
        case 'A':
            /* Game movement logic would go here */
            return true;

        case 'd': /* D: Move right */
        case 'D':
            /* Game movement logic would go here */
            return true;

        case ' ': /* Space: Jump */
            /* Jump logic would go here */
            return true;

        case 't': /* T: Toggle block type (without Cmd) */
        case 'T':
            sBlockType = (sBlockType + 1) % 3;
            InvalRect(&sBlockSelectorRect);
            return true;
        }
    }

    return false;
}

/* Handle window activation/deactivation events */
static void GameWindow_HandleActivate(Boolean becomingActive)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    /* Force window redraw */
    InvalRect(&sWindow->portRect);
}

/* Render the game window contents */
static void GameWindow_Render(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);

    /* Draw the window with standard frame */
    DrawStandardFrame(sWindow);

    /* Draw game view area with black background */
    PenNormal();
    PenSize(1, 1);
    FrameRect(&sDisplayRect);

    /* Fill game view with black */
    Rect gameRect = sDisplayRect;
    InsetRect(&gameRect, 1, 1);
    BackColor(blackColor);
    EraseRect(&gameRect);

    /* Draw block selector UI at bottom */
    DrawBlockSelector();
}

/* Handle update events for the game window */
static void GameWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Call the render function to draw window contents */
    GameWindow_Render();

    EndUpdate(sWindow);
}

/* Handle all events for the game window */
void GameWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
    case keyDown: {
        char key            = event->message & charCodeMask;
        Boolean isShiftDown = (event->modifiers & shiftKey) != 0;
        Boolean isCmdDown   = (event->modifiers & cmdKey) != 0;
        GameWindow_HandleKeyDown(key, isShiftDown, isCmdDown);
        break;
    }

    case mouseDown: {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        GameWindow_HandleContentClick(mousePt);
        break;
    }

    case activateEvt: {
        Boolean becomingActive = (event->modifiers & activeFlag) != 0;
        GameWindow_HandleActivate(becomingActive);
        break;
    }

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            GameWindow_Update();
        }
        break;
    }
}

/* Show or hide the game window */
void GameWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        /* Show window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Update display */
        GameWindow_Render();

        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }
}

/* Get the window reference for the game window */
WindowRef GameWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Determine if the window is visible */
Boolean GameWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return sIsVisible;
}

/* Toggle between block types */
void GameWindow_ToggleBlockType(void)
{
    if (!sInitialized || !sIsVisible) {
        return;
    }

    /* Toggle the block type */
    sBlockType = (sBlockType + 1) % 3;

    /* Redraw the block selector */
    InvalRect(&sBlockSelectorRect);
}

/* Perform idle processing */
void GameWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    /* Could add animations or other idle processing here */
}