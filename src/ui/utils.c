#include <Quickdraw.h>
#include <Windows.h>

#include "utils.h"

/* Draw a standard window frame */
void DrawStandardFrame(WindowRef window)
{
    if (window == NULL) {
        return;
    }

    SetPort(window);

    /* Erase window with white background */
    BackColor(whiteColor);
    EraseRect(&window->portRect);

    /* Draw frame if needed */
    PenNormal();
    PenSize(1, 1);
}
