#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "../game/iso.h"
#include "../game/world.h"
#include "game_window.h"
#include "utils.h"

/* Module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;       /* Main content area */
static Rect sBlockSelectorRect; /* Block selector area at bottom */
static Rect sDisplayRect;       /* Game view area */

/* Camera offset in display-local coordinates (see Iso_Project). */
static short sCamX = 0;
static short sCamY = 0;

/* Selected swatch index; the placed block id is sBlockType + 1. */
static short sBlockType = 0;

/* Pending repaint: state changes record the smallest affected rect here and
 * GameWindow_Idle turns it into an InvalRect, so painting only ever happens
 * inside a BeginUpdate/EndUpdate pass. */
static Boolean sDirty = false;
static Rect sDirtyRect;

/* Intersection of the display rect with the current update region's bounds,
 * so an update pass only walks the tiles it can actually paint. */
static Rect sRenderRect;

/* Private function declarations */
static void MarkDirty(const Rect *r);
static void FlushDirty(void);
static void GameWindow_Update(void);
static void GameWindow_Render(void);
static void DrawWorld(Rect *view);
static void PaintQuad(short x0, short y0, short x1, short y1, short x2, short y2, short x3,
                      short y3, Pattern *pat);
static Pattern *BlockPattern(BlockID id);
static void DrawBlock(short x, short y, short z, BlockID id);
static void GameWindow_HandleActivate(Boolean becomingActive);
static Boolean GameWindow_HandleContentClick(Point localPt, Boolean shiftDown);
static Boolean GameWindow_HandleKeyDown(char key);
static void SelectorBlockRect(short index, Rect *outRect);
static void DrawBlockSelector(void);
static void InvalidateColumn(short x, short y, short z);
static void PanCamera(short dx, short dy);
static short FloorDivInt(int a, int b);
static void UnprojectClamped(short px, short py, short *outX, short *outY);
static void ComputeVisibleRange(Rect *view, short *xMin, short *yMin, short *xMax, short *yMax);

/* Record a rect that needs repainting. */
static void MarkDirty(const Rect *r)
{
    if (r == NULL) {
        return;
    }

    if (sDirty) {
        UnionRect(r, &sDirtyRect, &sDirtyRect);
    }
    else {
        sDirtyRect = *r;
        sDirty     = true;
    }
}

/* Turn the recorded dirty rect into an InvalRect. Never draws. */
static void FlushDirty(void)
{
    GrafPtr savedPort;

    if (!sDirty || sWindow == NULL) {
        return;
    }

    /* InvalRect targets the current port's window, so make ours current. */
    GetPort(&savedPort);
    SetPort(sWindow);
    InvalRect(&sDirtyRect);
    SetPort(savedPort);

    sDirty = false;
}

/* Initialize and create the game window */
void GameWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight, windowWidth, windowHeight, displayWidth, displayHeight;
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

    /* Centre the world tile (24,24) in the display: Iso_Project(24,24,0,0,0) is
     * (0, 384), so the camera offset is the display centre minus that. */
    displayWidth  = sDisplayRect.right - sDisplayRect.left;
    displayHeight = sDisplayRect.bottom - sDisplayRect.top;
    sCamX         = displayWidth / 2;
    sCamY         = displayHeight / 2 - 384;

    /* Start with the default flat plateau so the first frame is playable. */
    World_Reset();

    sDirty       = false;
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
    sDirty       = false;
}

/* Fill a four-point polygon with the given pattern. */
static void PaintQuad(short x0, short y0, short x1, short y1, short x2, short y2, short x3,
                      short y3, Pattern *pat)
{
    PolyHandle poly;

    poly = OpenPoly();
    if (poly == NULL) {
        return;
    }

    PenPat(pat);
    MoveTo(x0, y0);
    LineTo(x1, y1);
    LineTo(x2, y2);
    LineTo(x3, y3);
    ClosePoly();
    PaintPoly(poly);
    KillPoly(poly);
}

/* Pattern used for each block id (0 = empty, 1 = dirt, 2 = stone, 3 = grass). */
static Pattern *BlockPattern(BlockID id)
{
    switch (id) {
    case 2:
        return &qd.gray; /* stone */
    case 3:
        return &qd.ltGray; /* grass */
    case 1:
    default:
        return &qd.dkGray; /* dirt */
    }
}

/* Draw one block: top diamond plus its two visible side faces, all centred on
 * Iso_Project(x, y, z, ...) and clipped nowhere (the update region clips). */
static void DrawBlock(short x, short y, short z, BlockID id)
{
    short sx, sy;
    Pattern *pat;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx += sDisplayRect.left;
    sy += sDisplayRect.top;

    pat = BlockPattern(id);

    /* Top face: diamond centred at (sx, sy), kTileW x kTileH. */
    PaintQuad(sx, sy - kTileH / 2, sx + kTileW / 2, sy, sx, sy + kTileH / 2, sx - kTileW / 2, sy,
              pat);

    /* Left face, dropping one half tile per z level. */
    PaintQuad(sx - kTileW / 2, sy, sx, sy + kTileH / 2, sx, sy + kTileH / 2 + kTileH / 2,
              sx - kTileW / 2, sy + kTileH / 2, pat);

    /* Right face. */
    PaintQuad(sx, sy + kTileH / 2, sx + kTileW / 2, sy, sx + kTileW / 2, sy + kTileH / 2,
              sx, sy + kTileH / 2 + kTileH / 2, pat);
}

/* Inverse of Iso_Project's documented z=0 mapping, clamped to the world.
 * Used only to derive the visible tile range; clicks use Iso_Unproject. */
static short FloorDivInt(int a, int b)
{
    int q = a / b;

    if ((a % b) != 0 && ((a < 0) != (b < 0))) {
        q--;
    }
    return (short)q;
}

static void UnprojectClamped(short px, short py, short *outX, short *outY)
{
    short sx, sy, x, y;

    sx = FloorDivInt((int)px - (int)sCamX, kTileW / 2);
    sy = FloorDivInt((int)py - (int)sCamY, kTileH / 2);

    x = FloorDivInt((int)sx + (int)sy, 2);
    y = FloorDivInt((int)sy - (int)sx, 2);

    if (x < 0) {
        x = 0;
    }
    else if (x >= kWorldX) {
        x = kWorldX - 1;
    }

    if (y < 0) {
        y = 0;
    }
    else if (y >= kWorldY) {
        y = kWorldY - 1;
    }

    *outX = x;
    *outY = y;
}

/* Visible tile range for the part of the display being repainted, in
 * display-local coordinates. Expanded by kWorldZ tiles so that raised columns
 * whose tops are in view but whose base tiles are below still render. */
static void ComputeVisibleRange(Rect *view, short *xMin, short *yMin, short *xMax, short *yMax)
{
    short left, top, right, bottom, x, y;

    left   = (short)(view->left - sDisplayRect.left);
    top    = (short)(view->top - sDisplayRect.top);
    right  = (short)(view->right - sDisplayRect.left);
    bottom = (short)(view->bottom - sDisplayRect.top);

    UnprojectClamped(left, top, xMin, yMin);
    *xMax = *xMin;
    *yMax = *yMin;

    UnprojectClamped(right, top, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(left, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    UnprojectClamped(right, bottom, &x, &y);
    if (x < *xMin) *xMin = x;
    if (x > *xMax) *xMax = x;
    if (y < *yMin) *yMin = y;
    if (y > *yMax) *yMax = y;

    *xMin -= kWorldZ;
    *yMin -= kWorldZ;
    *xMax += kWorldZ;
    *yMax += kWorldZ;

    if (*xMin < 0) *xMin = 0;
    if (*yMin < 0) *yMin = 0;
    if (*xMax >= kWorldX) *xMax = kWorldX - 1;
    if (*yMax >= kWorldY) *yMax = kWorldY - 1;
}

/* Draw the world, bottom z level first, back-to-front inside each level. */
static void DrawWorld(Rect *view)
{
    short xMin, yMin, xMax, yMax, z, sum, x, y, sumMax;
    BlockID id;

    ComputeVisibleRange(view, &xMin, &yMin, &xMax, &yMax);
    sumMax = xMax + yMax;

    for (z = 0; z < kWorldZ; z++) {
        for (sum = xMin + yMin; sum <= sumMax; sum++) {
            for (x = xMin; x <= xMax; x++) {
                y = sum - x;
                if (y < yMin || y > yMax) {
                    continue;
                }

                id = World_Get(x, y, z);
                if (id != 0) {
                    DrawBlock(x, y, z, id);
                }
            }
        }
    }
}

/* Compute the rect of one swatch in the selector strip. */
static void SelectorBlockRect(short index, Rect *outRect)
{
    Rect frameRect;
    short totalWidth, startX, startY;
    short blockSize = kGameBlockSwatchSize;
    short spacing   = kGameBlockSwatchSpacing;

    frameRect = sBlockSelectorRect;
    InsetRect(&frameRect, 1, 1);

    totalWidth = (blockSize + spacing) * kGameNumBlockTypes - spacing;
    startX     = frameRect.left + (frameRect.right - frameRect.left - totalWidth) / 2;
    startY     = frameRect.top + (frameRect.bottom - frameRect.top - blockSize) / 2;

    SetRect(outRect, startX + index * (blockSize + spacing), startY,
            startX + index * (blockSize + spacing) + blockSize, startY + blockSize);
}

/* Draw the block selector UI at the bottom of the window. */
static void DrawBlockSelector(void)
{
    Rect frameRect  = sBlockSelectorRect;
    Rect shadowRect = frameRect;
    Rect blockRect;
    short i;

    /* Shadow effect */
    PenNormal();
    PenSize(1, 1);
    OffsetRect(&shadowRect, 1, 1);
    PenPat(&qd.gray);
    FrameRect(&shadowRect);
    PenNormal();

    /* Frame around the block selector area */
    ForeColor(blackColor);
    FrameRect(&frameRect);

    /* Light background */
    InsetRect(&frameRect, 1, 1);
    BackPat(&qd.white);
    BackColor(whiteColor);
    EraseRect(&frameRect);

    /* Draw the three block swatches (dirt, stone, grass) */
    for (i = 0; i < kGameNumBlockTypes; i++) {
        SelectorBlockRect(i, &blockRect);

        PenNormal();
        ForeColor(blackColor);
        FrameRect(&blockRect);

        PenPat(BlockPattern((BlockID)(i + 1)));
        PaintRect(&blockRect);

        /* Highlight the selected block type */
        if (i == sBlockType) {
            Rect highlightRect = blockRect;
            InsetRect(&highlightRect, -2, -2);
            PenNormal();
            PenSize(2, 2);
            FrameRect(&highlightRect);
            PenSize(1, 1);
        }
    }

    PenNormal();
}

/* Invalidate only the screen rect covered by one column at height z. */
static void InvalidateColumn(short x, short y, short z)
{
    short sx, sy;
    Rect r;

    Iso_Project(x, y, z, sCamX, sCamY, &sx, &sy);
    sx += sDisplayRect.left;
    sy += sDisplayRect.top;

    SetRect(&r, sx - kTileW / 2 - 2, sy - kTileH / 2 - 2, sx + kTileW / 2 + 2, sy + kTileH + 2);
    MarkDirty(&r);
}

/* Handle mouse clicks in the content area of the game window. */
static Boolean GameWindow_HandleContentClick(Point localPt, Boolean shiftDown)
{
    short x, y, topZ, newZ;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Block placement/removal in the game display */
    if (PtInRect(localPt, &sDisplayRect)) {
        if (!Iso_Unproject(localPt.h - sDisplayRect.left, localPt.v - sDisplayRect.top, sCamX,
                           sCamY, &x, &y)) {
            return true;
        }

        topZ = World_TopZ(x, y);

        if (shiftDown) {
            /* Remove the top block of the column */
            if (topZ >= 0 && World_Set(x, y, topZ, 0)) {
                InvalidateColumn(x, y, topZ);
            }
        }
        else {
            /* Place the selected block on top of the column */
            newZ = (short)(topZ + 1);
            if (newZ < kWorldZ && World_Set(x, y, newZ, (BlockID)(sBlockType + 1))) {
                InvalidateColumn(x, y, newZ);
            }
        }

        return true;
    }

    /* Block type selection */
    if (PtInRect(localPt, &sBlockSelectorRect)) {
        short i;
        Rect blockRect;

        for (i = 0; i < kGameNumBlockTypes; i++) {
            SelectorBlockRect(i, &blockRect);
            if (PtInRect(localPt, &blockRect)) {
                sBlockType = i;
                MarkDirty(&sBlockSelectorRect);
                return true;
            }
        }

        return true;
    }

    return false;
}

/* Pan the camera by one tile step and repaint the display. */
static void PanCamera(short dx, short dy)
{
    int newX = (int)sCamX + (int)dx;
    int newY = (int)sCamY + (int)dy;

    if (newX < -kGameCameraLimit) newX = -kGameCameraLimit;
    if (newX > kGameCameraLimit) newX = kGameCameraLimit;
    if (newY < -kGameCameraLimit) newY = -kGameCameraLimit;
    if (newY > kGameCameraLimit) newY = kGameCameraLimit;

    sCamX = (short)newX;
    sCamY = (short)newY;

    MarkDirty(&sDisplayRect);
}

/*
 * Cursor-key character codes. Apple's current headers no longer name them
 * (they lived in the old Events.h); 0x1C..0x1F is what the key map reports
 * for left/right/up/down through charCodeMask, on every keyboard layout.
 */
enum {
    kLeftArrowKey  = 0x1C,
    kRightArrowKey = 0x1D,
    kUpArrowKey    = 0x1E,
    kDownArrowKey  = 0x1F
};

/* Handle key events in the game window (menu keys are handled by MenuKey). */
static Boolean GameWindow_HandleKeyDown(char key)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    switch (key) {
    case '1': /* Select dirt */
        sBlockType = 0;
        MarkDirty(&sBlockSelectorRect);
        return true;

    case '2': /* Select stone */
        sBlockType = 1;
        MarkDirty(&sBlockSelectorRect);
        return true;

    case '3': /* Select grass */
        sBlockType = 2;
        MarkDirty(&sBlockSelectorRect);
        return true;

    case 't':
    case 'T': /* Cycle block type */
        sBlockType = (short)((sBlockType + 1) % kGameNumBlockTypes);
        MarkDirty(&sBlockSelectorRect);
        return true;

    case kLeftArrowKey:
    case 'a':
    case 'A':
        PanCamera(-kGameCameraPanX, 0);
        return true;

    case kRightArrowKey:
    case 'd':
    case 'D':
        PanCamera(kGameCameraPanX, 0);
        return true;

    case kUpArrowKey:
    case 'w':
    case 'W':
        PanCamera(0, -kGameCameraPanY);
        return true;

    case kDownArrowKey:
    case 's':
    case 'S':
        PanCamera(0, kGameCameraPanY);
        return true;

    default:
        break;
    }

    return false;
}

/* Handle window activation/deactivation events */
static void GameWindow_HandleActivate(Boolean becomingActive)
{
    (void)becomingActive;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    /* Force window redraw */
    MarkDirty(&sWindow->portRect);
}

/* Render the game window contents. Only called between BeginUpdate/EndUpdate. */
static void GameWindow_Render(void)
{
    Rect strip;
    PenState savedPen;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    GetPenState(&savedPen);

    /* World drawing is limited to whole tiles, so it covers the display rect;
     * BeginUpdate has already clipped the port to the update region, which is
     * what keeps a one-column invalidation from repainting the rest. */
    sRenderRect = sDisplayRect;

    /* White window background and content frame */
    BackPat(&qd.white);
    DrawStandardFrame(sWindow);

    /* Display area: black background */
    BackColor(blackColor);
    BackPat(&qd.black);
    EraseRect(&sDisplayRect);

    /* World blocks (nothing to do when only the selector strip was dirty) */
    if (sRenderRect.right > sRenderRect.left && sRenderRect.bottom > sRenderRect.top) {
        DrawWorld(&sRenderRect);
    }

    /* Erase the margins so a partially visible edge tile cannot bleed outside
     * the display rect (no manual clipping anywhere). */
    BackColor(whiteColor);
    BackPat(&qd.white);

    /* Top strip */
    strip = sWindow->portRect;
    strip.bottom = sDisplayRect.top;
    EraseRect(&strip);

    /* Left strip */
    strip = sWindow->portRect;
    strip.top    = sDisplayRect.top;
    strip.bottom = sDisplayRect.bottom;
    strip.right  = sDisplayRect.left;
    EraseRect(&strip);

    /* Right strip */
    strip = sWindow->portRect;
    strip.top    = sDisplayRect.top;
    strip.bottom = sDisplayRect.bottom;
    strip.left   = sDisplayRect.right;
    EraseRect(&strip);

    /* Bottom strip: the gap between the display and the selector (the
     * selector repaints its own area afterwards). */
    strip = sWindow->portRect;
    strip.top = sDisplayRect.bottom;
    EraseRect(&strip);

    /* Display frame, then the selector on top of any remaining spill. */
    PenNormal();
    PenSize(1, 1);
    ForeColor(blackColor);
    FrameRect(&sDisplayRect);

    DrawBlockSelector();

    SetPenState(&savedPen);
}

/* Handle update events for the game window */
static void GameWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

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
    case keyDown:
    case autoKey:
        /* Command-key combinations belong to the menu bar (MenuKey). */
        if ((event->modifiers & cmdKey) == 0) {
            GameWindow_HandleKeyDown((char)(event->message & charCodeMask));
        }
        break;

    case mouseDown: {
        WindowRef clickedWindow;
        short part;
        Point localPt;
        GrafPtr savedPort;

        part = FindWindow(event->where, &clickedWindow);
        if (clickedWindow != sWindow || part != inContent) {
            break;
        }

        GetPort(&savedPort);
        SetPort(sWindow);
        localPt = event->where;
        GlobalToLocal(&localPt);
        GameWindow_HandleContentClick(localPt, (event->modifiers & shiftKey) != 0);
        SetPort(savedPort);
        break;
    }

    case activateEvt:
        if ((WindowPtr)event->message == sWindow) {
            GameWindow_HandleActivate((event->modifiers & activeFlag) != 0);
        }
        break;

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            GameWindow_Update();
        }
        break;

    default:
        break;
    }

    FlushDirty();
}

/* Show, hide or dispose of the game window. */
void GameWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        ShowWindow(sWindow);
        SelectWindow(sWindow);
        sIsVisible = true;

        /* The update event performs the actual drawing. */
        MarkDirty(&sWindow->portRect);
        FlushDirty();
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
        sDirty     = false;
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

    sBlockType = (short)((sBlockType + 1) % kGameNumBlockTypes);
    MarkDirty(&sBlockSelectorRect);
}

/* Perform idle processing: turn a recorded dirty rect into an InvalRect. */
void GameWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    FlushDirty();
}
