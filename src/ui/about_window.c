#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"
#include "pstring.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* Replace a DITL item's text at runtime. The About box draws its body in the
 * Dialog Manager's text-drawing path only for the button, so the resource's
 * static text items are blanked and redrawn here with known fonts. */
static void EmptyDialogItem(DialogPtr dialog, short itemNo)
{
    short itemType;
    Handle item;
    Rect itemRect;

    GetDialogItem(dialog, itemNo, &itemType, &item, &itemRect);
    if (item != NULL) {
        SetDialogItemText(item, "\p");
    }
}

/* Draw the About box contents with Geneva 9 and a bold title. Called after
 * DrawDialog has erased the dialog and drawn the OK button, and again after
 * the Dialog Manager redraws the dialog on update or activation. */
static void DrawAboutContent(DialogPtr dialog)
{
    GrafPtr savedPort;
    GrafPtr dialogPort;
    PenState savedPen;
    RGBColor savedFgColor, savedBackColor;
    RGBColor gray;
    short savedFont, savedSize;
    Style savedFace;

    gray.red = gray.green = gray.blue = 0x8888;

    GetPort(&savedPort);
    dialogPort = (GrafPtr)dialog;
    SetPort(dialogPort);
    savedFont = dialogPort->txFont;
    savedSize = dialogPort->txSize;
    savedFace = dialogPort->txFace;
    GetPenState(&savedPen);
    GetForeColor(&savedFgColor);
    GetBackColor(&savedBackColor);
    ForeColor(blackColor);
    BackColor(whiteColor);

    /* Title: Chicago 12 bold, the same app-name face as the splash screen. */
    TextFont(kFontSystem);
    TextSize(12);
    TextFace(bold);
    PenPat(&qd.black);
    PenSize(1, 1);
    MoveTo(12, 26);
    DrawString("\pMacCraft");

    /* One hairline rule under the title; no boxes around the body. */
    PenPat(&qd.dkGray);
    MoveTo(12, 32);
    LineTo(388, 32);

    /* Body: Geneva 9, twelve pixels of leading. */
    TextFont(kFontGeneva);
    TextSize(9);
    TextFace(normal);
    PenPat(&qd.black);
    MoveTo(12, 50);
    DrawString("\pAn isometric voxel world for classic Macintosh.");
    MoveTo(12, 66);
    DrawString("\pClick a tile to place the selected block.");
    MoveTo(12, 78);
    DrawString("\pShift-click a tile to remove its top block.");
    MoveTo(12, 94);
    DrawString("\pArrow keys or WASD pan the camera.");
    MoveTo(12, 106);
    DrawString("\pKeys 1-9 pick a block, T cycles, arrows walk, space jumps.");

    /* Version in the grey small-print style. */
    {
        Str255 version;
        PStr_Version(version);
        MoveTo(12, 210);
        RGBForeColor(&gray);
        DrawString(version);
    }

    SetPenState(&savedPen);
    TextFont(savedFont);
    TextSize(savedSize);
    TextFace(savedFace);
    RGBForeColor(&savedFgColor);
    RGBBackColor(&savedBackColor);
    SetPort(savedPort);
}

/* Draw the whole dialog, including our custom body, with the dialog's port
 * current inside an update cycle. */
static void RedrawAboutDialog(DialogPtr dialog)
{
    GrafPtr savedPort;

    GetPort(&savedPort);
    SetPort((GrafPtr)dialog);
    DrawDialog(dialog);
    DrawAboutContent(dialog);
    SetPort(savedPort);
}

/* Show the About box as a modal dialog. The dialog resource supplies the
 * frame and the OK button; the text is drawn here so it can be styled. */
void AboutWindow_Show(void)
{
    DialogPtr dialog;
    GrafPtr savedPort;
    short itemHit;
    EventRecord event;

    GetPort(&savedPort);

    dialog = GetNewDialog(kAboutBoxID, NULL, (WindowPtr)-1);
    if (dialog == NULL) {
        HandleError(kErrResourceNotFound, kCtxShowingAboutBox, false); /* Non-fatal */
        return;
    }

    EmptyDialogItem(dialog, 2);
    EmptyDialogItem(dialog, 3);

    SetDialogDefaultItem(dialog, 1);
    ShowWindow((WindowPtr)dialog);
    SelectWindow((WindowPtr)dialog);

    /* Paint once before waiting; the update path below repaints after moves
     * and activation changes. */
    RedrawAboutDialog(dialog);

    itemHit = 0;
    for (;;) {
        WaitNextEvent(everyEvent, &event, 6, NULL);

        if (event.what == updateEvt && (WindowPtr)event.message == (WindowPtr)dialog) {
            BeginUpdate((WindowPtr)dialog);
            DrawDialog(dialog);
            DrawAboutContent(dialog);
            EndUpdate((WindowPtr)dialog);
            continue;
        }

        if (event.what == activateEvt && (WindowPtr)event.message == (WindowPtr)dialog) {
            /* Activating a dialog makes the Dialog Manager redraw it, which
             * would erase the custom body; redraw both halves afterwards. */
            DialogSelect(&event, &dialog, &itemHit);
            RedrawAboutDialog(dialog);
            continue;
        }

        if (DialogSelect(&event, &dialog, &itemHit) && itemHit == 1) {
            break;
        }

        if (event.what == keyDown) {
            char c = (char)(event.message & charCodeMask);

            if (c == 13 || c == 3 || c == 27) { /* Return, Enter, Escape */
                break;
            }
        }
    }

    SetPort(savedPort);
    DisposeDialog(dialog);
}
