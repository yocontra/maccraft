#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "splash_window.h"
#include "pstring.h"
#include "window_manager.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* Private module variables */
static WindowRef sWindow              = NULL;
static ControlHandle sStartGameButton = NULL;
static Boolean sInitialized           = false;
static Boolean sIsVisible             = false;

/* Fill a four-point polygon with a pattern and outline it in black. A splash
 * repaint is rare, so the polygon is allocated and released here; the game
 * view is the only place with a no-per-tile-allocation requirement. */
static void SplashFillQuad(Point p0, Point p1, Point p2, Point p3, const Pattern *pat,
                           Boolean outline)
{
    PolyHandle poly;

    poly = OpenPoly();
    if (poly == NULL) {
        return;
    }

    MoveTo(p0.h, p0.v);
    LineTo(p1.h, p1.v);
    LineTo(p2.h, p2.v);
    LineTo(p3.h, p3.v);
    ClosePoly();

    FillPoly(poly, pat);

    if (outline) {
        PenPat(&qd.black);
        PenSize(1, 1);
        FramePoly(poly);
    }

    KillPoly(poly);
}

/* Draw one isometric cube mark. (cx, cy) is the centre of the top face;
 * halfW/halfH describe that diamond and depth is the side-face height. */
static void DrawSplashCube(short cx, short cy, short halfW, short halfH, short depth,
                           const Pattern *top, const Pattern *left, const Pattern *right)
{
    Point t0, t1, t2, t3;
    Point l0, l1, l2, l3;
    Point r0, r1, r2, r3;

    t0.h = cx;
    t0.v = (short)(cy - halfH);
    t1.h = (short)(cx + halfW);
    t1.v = cy;
    t2.h = cx;
    t2.v = (short)(cy + halfH);
    t3.h = (short)(cx - halfW);
    t3.v = cy;

    l0 = t3;
    l1 = t2;
    l2.h = t2.h;
    l2.v = (short)(t2.v + depth);
    l3.h = t3.h;
    l3.v = (short)(t3.v + depth);

    r0 = t2;
    r1 = t1;
    r2.h = t1.h;
    r2.v = (short)(t1.v + depth);
    r3.h = t2.h;
    r3.v = (short)(t2.v + depth);

    SplashFillQuad(t0, t1, t2, t3, top, true);
    SplashFillQuad(l0, l1, l2, l3, left, true);
    SplashFillQuad(r0, r1, r2, r3, right, true);
}

/* Draw the centred splash lockup: a small isometric block cluster, the app
 * name in Chicago 12 bold, a one-line Geneva 9 description, a hairline rule,
 * the hint text, and the version in the bottom-right corner. The Start button
 * is a real control drawn afterwards by DrawControls. */
static void DrawSplashWindowUI(WindowPtr window)
{
    GrafPtr savedPort;
    PenState savedPen;
    RGBColor savedFgColor, savedBackColor;
    RGBColor gray;
    Rect portRect;
    Str255 version;
    short cx, width;
    short savedFont, savedSize;
    Style savedFace;

    if (window == NULL) {
        return;
    }

    GetPort(&savedPort);
    SetPort(window);
    savedFont = window->txFont;
    savedSize = window->txSize;
    savedFace = window->txFace;
    GetPenState(&savedPen);
    GetForeColor(&savedFgColor);
    GetBackColor(&savedBackColor);

    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
    PenSize(1, 1);

    /* Platinum-ish chrome: one flat light-grey field, no hatch or gradient. */
    PenPat(&qd.ltGray);
    PaintRect(&window->portRect);

    portRect = window->portRect;
    cx      = (short)((portRect.left + portRect.right) / 2);

    /* Brand mark: dirt and stone cubes flanking a larger grass cube. */
    DrawSplashCube((short)(cx - 44), (short)(72 - 18), 30, 15, 16, &qd.gray, &qd.dkGray, &qd.gray);
    DrawSplashCube((short)(cx + 46), (short)(72 + 14), 30, 15, 16, &qd.dkGray, &qd.gray, &qd.black);
    DrawSplashCube(cx, 72, 48, 24, 24, &qd.white, &qd.dkGray, &qd.gray);

    /* App name in Chicago 12 bold, centred. */
    TextFont(kFontSystem);
    TextSize(12);
    TextFace(bold);
    PenPat(&qd.black);
    width = StringWidth("\pMacCraft");
    MoveTo((short)((portRect.right - width) / 2), 140);
    DrawString("\pMacCraft");

    /* One-line description in Geneva 9, centred. */
    TextFont(kFontGeneva);
    TextSize(9);
    TextFace(normal);
    PenPat(&qd.black);
    width = StringWidth("\pAn isometric block world for classic Macintosh");
    MoveTo((short)((portRect.right - width) / 2), 158);
    DrawString("\pAn isometric block world for classic Macintosh");

    /* Single hairline rule between the lockup and the button. */
    PenPat(&qd.dkGray);
    MoveTo((short)(cx - 140), 170);
    LineTo((short)(cx + 140), 170);

    /* Small grey hint under the button. */
    gray.red = gray.green = gray.blue = 0x8888;
    RGBForeColor(&gray);
    width = StringWidth("\pClick Start or press Return");
    MoveTo((short)((portRect.right - width) / 2), 236);
    DrawString("\pClick Start or press Return");

    /* Version bottom-right in small grey. */
    PStr_Version(version);
    width = StringWidth(version);
    MoveTo((short)(portRect.right - 12 - width), (short)(portRect.bottom - 12));
    DrawString(version);

    RGBForeColor(&savedFgColor);
    RGBBackColor(&savedBackColor);
    SetPenState(&savedPen);
    TextFont(savedFont);
    TextSize(savedSize);
    TextFace(savedFace);
    SetPort(savedPort);
}

/* Draw the default-button ring around the Start control. Called with the
 * splash port current, after DrawControls has painted the button. */
static void DrawStartRing(void)
{
    PenState savedPen;
    Rect ring;

    if (sStartGameButton == NULL) {
        return;
    }

    GetPenState(&savedPen);

    ring = (*sStartGameButton)->contrlRect;
    InsetRect(&ring, -4, -4);
    PenNormal();
    PenPat(&qd.black);
    PenSize(2, 2);
    FrameRoundRect(&ring, 16, 16);

    SetPenState(&savedPen);
}

/* Initialize and create the splash window */
Boolean SplashWindow_Initialize(void)
{
    Rect screenRect, windowRect, frameRect, buttonRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight, windowTop;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return true;
    }

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        return false;
    }

    /* Calculate screen dimensions */
    screenRect   = qd.screenBits.bounds;
    screenWidth  = screenRect.right - screenRect.left;
    screenHeight = screenRect.bottom - screenRect.top;

    /* Wide lockup, short height. 340 px holds the whole mark and the button;
     * compact screens get the same layout with less bottom margin. */
    windowWidth  = (screenWidth * 3) / 4;
    windowHeight = 340;
    if (windowHeight > screenHeight - 80) {
        windowHeight = screenHeight - 80;
    }

    /* Anchor the top edge where the original 3/4-screen window had it so the
     * published test coordinates for the Start button stay valid. */
    windowTop = (screenHeight - (screenHeight * 3) / 4) / 2;
    if (windowTop < 22) {
        windowTop = 22;
    }
    if (windowTop + windowHeight > screenHeight - 4) {
        windowTop = screenHeight - 4 - windowHeight;
    }

    SetRect(&windowRect, (screenWidth - windowWidth) / 2, windowTop,
            (screenWidth - windowWidth) / 2 + windowWidth, windowTop + windowHeight);

    sWindow =
        NewWindow(NULL, &windowRect, "\pMacCraft", true, documentProc, (WindowPtr)-1, true, 0);

    if (sWindow == NULL) {
        return false;
    }

    /* Make window visible and active; painting happens in the update event. */
    ShowWindow(sWindow);
    SelectWindow(sWindow);

    SetPort(sWindow);
    frameRect = sWindow->portRect;

    /* Start button, unchanged in position: centred, 120x25 at port y 190. */
    SetRect(&buttonRect, (frameRect.right - 120) / 2, frameRect.top + 190,
            (frameRect.right + 120) / 2, frameRect.top + 215);

    sStartGameButton = NewControl(sWindow, &buttonRect, "\pStart", true, 0, 0, 1, pushButProc,
                                  (long)kStartGameBtn);

    if (sStartGameButton == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingButton, false); /* Non-fatal */
        /* We can continue without the button - the menu still opens the game. */
    }

    InvalRect(&sWindow->portRect);

    sInitialized = true;
    return true;
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

/* Render the splash window contents. Only called between BeginUpdate and
 * EndUpdate, or with the window's port already current. */
void SplashWindow_Render(void)
{
    GrafPtr savedPort;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    GetPort(&savedPort);
    SetPort(sWindow);

    DrawSplashWindowUI(sWindow);

    if (sStartGameButton != NULL) {
        DrawControls(sWindow);
        DrawStartRing();
    }

    SetPort(savedPort);
}

/* Handle update events for the splash window */
static void SplashWindow_Update(void)
{
    GrafPtr savedPort;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    GetPort(&savedPort);
    SetPort(sWindow);
    BeginUpdate(sWindow);

    SplashWindow_Render();

    EndUpdate(sWindow);
    SetPort(savedPort);
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

    /* If Start button was clicked */
    if (sStartGameButton != NULL && part && control == sStartGameButton) {
        /* TrackControl gives the pressed-button feedback, follows the mouse,
         * and returns non-zero only when released inside the button. */
        if (TrackControl(control, localPt, NULL) != 0) {
            return true; /* Indicate button was clicked */
        }
    }

    return false; /* No button click handled */
}

/* Leave the splash for the game window. Shared by the Start button and the
 * Return key, which is what makes Start the default button. */
static void OpenGameFromSplash(void)
{
    WindowManager_OpenWindow(kWindowTypeGame);
    if (WindowManager_IsWindowVisible(kWindowTypeGame)) {
        WindowManager_CloseWindow(kWindowTypeSplash);
    }
}

/* Handle all events for the splash window */
void SplashWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
    case mouseDown: {
        WindowRef clicked;
        short part;
        Point mousePt;
        GrafPtr savePort;

        part = FindWindow(event->where, &clicked);
        if (part != inContent || clicked != sWindow) {
            break;
        }

        GetPort(&savePort);
        SetPort(sWindow);

        mousePt = event->where;
        GlobalToLocal(&mousePt);
        if (SplashWindow_HandleContentClick(mousePt)) {
            OpenGameFromSplash();
        }

        SetPort(savePort);
    } break;

    case keyDown:
    case autoKey:
        /* Return or Enter activates the default Start button. Command-key
         * combinations are handled by MenuKey before they get here. */
        if ((event->modifiers & cmdKey) == 0) {
            char c = (char)(event->message & charCodeMask);
            if (c == 13 || c == 3) {
                OpenGameFromSplash();
            }
        }
        break;

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
Boolean SplashWindow_Show(Boolean visible)
{
    GrafPtr savedPort;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    if (visible) {
        GetPort(&savedPort);
        SetPort(sWindow);
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        sIsVisible = true;

        /* The update event performs the painting. InvalRect must run while
         * our window is the current port. */
        InvalRect(&sWindow->portRect);
        SetPort(savedPort);
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }

    return true;
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    return sIsVisible;
}
