#include <Dialogs.h>
#include <Memory.h>
#include <Sound.h>
#include <TextUtils.h>
#include <Windows.h>

#include "constants.h"
#include "error.h"

/*********************************************************************
 * ERROR HANDLING FUNCTIONS
 *********************************************************************/

/* Build "<context> (error N)" into a Str255 so the alert always shows both
 * where the failure happened and which error code caused it. */
static void MakeContextText(short errorCode, short contextID, Str255 out)
{
    const char *base;
    Str255 number;
    short len = 0;
    short i;

    switch (contextID) {
    case kCtxLaunchingApp:
        base = "Error launching app";
        break;
    case kCtxCreatingMainWindow:
        base = "Error creating main window";
        break;
    case kCtxCreatingButton:
        base = "Error creating button";
        break;
    case kCtxShowingAboutBox:
        base = "Error showing about box";
        break;
    case kCtxAboutBoxText:
        base = "Error loading about box text";
        break;
    case kCtxOpeningGameWindow:
        base = "Error opening game window";
        break;
    case kCtxQuittingApplication:
        base = "Quitting application";
        break;
    default:
        base = "Unknown error occurred";
        break;
    }

    NumToString(errorCode, number);

    while (*base != '\0' && len < 200) {
        out[1 + len] = *base++;
        len++;
    }

    if (len < 200) {
        out[1 + len] = ' ';
        len++;
        out[1 + len] = '(';
        len++;
    }

    for (i = 1; i <= number[0] && len < 250; i++) {
        out[1 + len] = number[i];
        len++;
    }

    if (len < 250) {
        out[1 + len] = ')';
        len++;
    }

    out[0] = (unsigned char)len;
}

/* Error handling function with alert dialog and optional fatal exit */
void HandleError(short errorCode, short contextID, Boolean fatal)
{
    Str255 contextText;
    ConstStr255Param fatalText;

    /* Double beep to alert the user of an error */
    SysBeep(10);
    SysBeep(20);

    /* Item 1 of the dialog carries the context plus the numeric error code;
     * item 2 carries the fatal warning when present. */
    MakeContextText(errorCode, contextID, contextText);
    fatalText = fatal ? (ConstStr255Param) "\pFatal error: the application will exit."
                      : (ConstStr255Param) "\p";

    ParamText(contextText, fatalText, "\p", "\p");

    if (fatal) {
        StopAlert(200, NULL);

        /* For fatal errors, it's safer to use ExitToShell directly
           rather than trying to clean up resources that might be corrupt */
        ExitToShell();
    }
    else {
        /* For non-fatal errors, use standard note alert */
        NoteAlert(200, NULL);
    }
}

/* Check available memory */
OSErr CheckMemory(void)
{
    long totalMem, contigMem;

    /* Check free memory */
    PurgeSpace(&totalMem, &contigMem);

    if (contigMem < 50 * 1024) { /* Require at least 50K of contiguous memory */
        return memFullErr;
    }

    if (totalMem < 100 * 1024) { /* Require at least 100K of total memory */
        return memFullErr;
    }

    return noErr;
}

/* QuitApplication is now defined in event.c */
