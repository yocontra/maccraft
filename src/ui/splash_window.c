#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "splash_window.h"
#include "window_manager.h"

/* Private module variables */
static WindowRef sWindow              = NULL;
static ControlHandle sStartGameButton = NULL;
static Boolean sInitialized           = false;
static Boolean sIsVisible             = false;

/* Initialize a pattern */
static void PatternInit(Pattern *pattern, unsigned char b1, unsigned char b2, unsigned char b3,
                        unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7,
                        unsigned char b8)
{
    pattern->pat[0] = b1;
    pattern->pat[1] = b2;
    pattern->pat[2] = b3;
    pattern->pat[3] = b4;
    pattern->pat[4] = b5;
    pattern->pat[5] = b6;
    pattern->pat[6] = b7;
    pattern->pat[7] = b8;
}

/* Draw the splash window UI elements */
static void DrawSplashWindowUI(WindowPtr window)
{
    Rect frameRect, logoRect, blockRect;
    Pattern theBits;
    short i, j, patternHeight;

    /* Get window dimensions */
    frameRect     = window->portRect;
    patternHeight = 80;

    /* Create visual gradient for background */
    for (i = 0; i < patternHeight; i++) {
        /* Create a pattern for this row */
        for (j = 0; j < 8; j++) {
            theBits.pat[j] = (i % 4 == 0) ? 0xAA : ((i % 4 == 1) ? 0x55 : 0);
        }

        /* Set the pattern and draw a line */
        PenPat(&theBits);
        MoveTo(frameRect.left, frameRect.top + i);
        LineTo(frameRect.right, frameRect.top + i);
    }

    /* Fill rest of window with solid color */
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);

    /* Draw MacCraft logo area */
    SetRect(&logoRect, (frameRect.right - 150) / 2, frameRect.top + 30, (frameRect.right + 150) / 2,
            frameRect.top + 100);

    /* Draw 3D blocks as part of the logo */
    short blockSize = 40;
    short xOffset   = (frameRect.right - blockSize * 3) / 2;
    short yOffset   = frameRect.top + 30;

    /* First row of blocks */
    SetRect(&blockRect, xOffset, yOffset, xOffset + blockSize, yOffset + blockSize);

    /* Draw first block (dirt) */
    PenSize(1, 1);
    ForeColor(blackColor);
    FrameRect(&blockRect);
    PenPat(&qd.dkGray);
    PaintRect(&blockRect);

    /* Draw second block (stone) */
    OffsetRect(&blockRect, blockSize, 0);
    PenNormal();
    FrameRect(&blockRect);
    PenPat(&qd.gray);
    PaintRect(&blockRect);

    /* Draw third block (grass) */
    OffsetRect(&blockRect, blockSize, 0);
    PenNormal();
    FrameRect(&blockRect);
    PenPat(&qd.ltGray);
    PaintRect(&blockRect);

    /* Draw logo text */
    TextFont(kFontGeneva);
    TextSize(36);
    TextFace(bold);
    ForeColor(blackColor);

    /* Calculate text position centered below blocks */
    short logoTextWidth = StringWidth("\pMacCraft");
    MoveTo((frameRect.right - logoTextWidth) / 2, yOffset + blockSize + 40);
    DrawString("\pMacCraft");

    /* Draw subtitle */
    TextFont(kFontGeneva);
    TextSize(14);
    TextFace(normal);
    MoveTo((frameRect.right - StringWidth("\pA 3D voxel game for classic Macintosh")) / 2,
           yOffset + blockSize + 65);
    DrawString("\pA 3D voxel game for classic Macintosh");

    /* Add decorative lines */
    PenSize(1, 1);
    PenPat(&qd.gray);
    MoveTo(frameRect.left + 20, yOffset + blockSize + 85);
    LineTo(frameRect.right - 20, yOffset + blockSize + 85);
    PenNormal();
}

/* Initialize and create the splash window */
void SplashWindow_Initialize(void)
{
    Rect screenRect, windowRect, frameRect, buttonRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
    }

    /* Calculate screen dimensions */
    screenRect   = qd.screenBits.bounds;
    screenWidth  = screenRect.right - screenRect.left;
    screenHeight = screenRect.bottom - screenRect.top;

    /* Make the window about 75% of screen width/height */
    windowWidth  = (screenWidth * 3) / 4;
    windowHeight = (screenHeight * 3) / 4;

    /* Create centered window rectangle */
    SetRect(&windowRect, (screenWidth - windowWidth) / 2, (screenHeight - windowHeight) / 2,
            (screenWidth - windowWidth) / 2 + windowWidth,
            (screenHeight - windowHeight) / 2 + windowHeight);

    /* Create the window directly like the chat window does */
    sWindow =
        NewWindow(NULL, &windowRect, "\pMacCraft", true, documentProc, (WindowPtr)-1, true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
    }

    /* Make window visible and active */
    ShowWindow(sWindow);
    SelectWindow(sWindow);

    /* Set port for drawing */
    SetPort(sWindow);

    /* Get window dimensions for drawing */
    frameRect = sWindow->portRect;

    /* Draw all the UI elements */
    DrawSplashWindowUI(sWindow);

    /* Create "Start Game" button - standard Mac rounded button with proper style */
    SetRect(&buttonRect, (frameRect.right - 120) / 2, /* Center button */
            frameRect.top + 190,                      /* Position below the text */
            (frameRect.right + 120) / 2, frameRect.top + 215);

    /* Use proper button proc for Macintosh (pushButProc without outline for better look) */
    sStartGameButton = NewControl(sWindow, &buttonRect, "\pStart Game", true, 0, 0, 1, pushButProc,
                                  (long)kStartGameBtn);

    if (sStartGameButton == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingButton, false); /* Non-fatal */
        /* We can continue without the button - user can still use Cmd-N */
    }

    /* Force a redraw of the entire window */
    InvalRect(&sWindow->portRect);

    sInitialized = true;
}

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose controls */
    if (sStartGameButton != NULL) {
        DisposeControl(sStartGameButton);
        sStartGameButton = NULL;
    }

    /* Dispose window after all resources are freed */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
}

/* Render the splash window contents */
void SplashWindow_Render(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);

    /* Draw the UI elements using the shared function */
    DrawSplashWindowUI(sWindow);

    /* Make sure the button is properly drawn */
    if (sStartGameButton != NULL) {
        DrawControls(sWindow);
    }
}

/* Handle update events for the splash window */
static void SplashWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Call the render function to draw window contents */
    SplashWindow_Render();

    EndUpdate(sWindow);
}

/* Handle mouse clicks in the content area of the splash window */
Boolean SplashWindow_HandleContentClick(Point localPt)
{
    ControlHandle control;
    short part;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Find which control was clicked */
    part = FindControl(localPt, sWindow, &control);

    /* If Start Chat button was clicked */
    if (part && control == sStartGameButton) {
        /* Visual feedback */
        HiliteControl(control, 1); /* Highlight button */
        Delay(8, NULL);            /* Short delay */
        HiliteControl(control, 0); /* Return to normal */

        return true; /* Indicate button was clicked */
    }

    return false; /* No button click handled */
}

/* Handle all events for the splash window */
void SplashWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
    case mouseDown: {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        if (SplashWindow_HandleContentClick(mousePt)) {
            /* If the start game button was clicked, open the game window */
            WindowManager_OpenWindow(kWindowTypeGame);
            WindowManager_CloseWindow(kWindowTypeSplash);
        }
    } break;

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            SplashWindow_Update();
        }
        break;

    default:
        /* Splash window doesn't handle other event types */
        break;
    }
}

/* Get the window reference for the splash window */
WindowRef SplashWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Show or hide the splash window */
void SplashWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        /* Set port before showing window */
        SetPort(sWindow);

        /* Show and select the window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Call render to draw all content */
        SplashWindow_Render();

        /* Update visibility tracking */
        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    return sIsVisible;
}