#include <Dialogs.h>
#include <Events.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* Feed DITL 128's "Version ^0" item the build version. */
static void SetAboutVersion(void)
{
    Str255 version;
    const char *src = APP_VERSION_STR;
    short len      = 0;

    while (*src != '\0' && len < 255) {
        version[1 + len] = *src++;
        len++;
    }
    version[0] = (unsigned char)len;

    ParamText(version, "\p", "\p", "\p");
}

/* Show the About box as a modal dialog.  The dialog is loaded from DLOG 128 /
 * DITL 128, run to completion, and disposed.  No window is retained, so there
 * is no stale pointer to free later. */
void AboutWindow_Show(void)
{
    DialogPtr dialog;
    short itemHit;

    SetAboutVersion();

    dialog = GetNewDialog(kAboutBoxID, NULL, (WindowPtr)-1);
    if (dialog == NULL) {
        HandleError(kErrResourceNotFound, kCtxShowingAboutBox, false); /* Non-fatal */
        return;
    }

    /* Return / Enter dismisses the box through the OK button */
    SetDialogDefaultItem(dialog, 1);
    SelectWindow(dialog);

    do {
        ModalDialog(NULL, &itemHit);
    } while (itemHit != 1 && itemHit != 0);

    DisposeDialog(dialog);
}
