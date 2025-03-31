#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Controls.h>
#include <Lists.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

/*********************************************************************
 * CONSTANTS
 *********************************************************************/

/* Resource IDs */
enum {
    /* Menu IDs */
    kMenuApple = 128,
    kMenuFile  = 129,
    kMenuGame  = 130,

    /* Menu item indices */
    kItemAbout   = 1,
    kItemNewGame = 1,
    kItemClose   = 2,
    kItemQuit    = 4,

    /* Game menu items */
    kItemToggleBlock = 1,
    kItemResetWorld  = 2,

    /* Window resource IDs */
    kAboutBoxID   = 128,
    kMainWindowID = 129,
    kGameWindowID = 130,

    /* Button resource ID and value */
    kStartGameBtn = 128
};

/* Application modes */
enum {
    kModeMainSplash = 0, /* Main splash screen is shown */
    kModeGameWindow = 1  /* Game window is active */
};

/* Control constants */
#define pushButProc 0
#define scrollBarProc 16

/* Control part codes (Toolbox constants that may not be in headers) */
#ifndef inUpButton
#define inUpButton 20
#define inDownButton 21
#define inPageUp 22
#define inPageDown 23
#define inThumb 129
#endif

/* Font identifiers */
enum { kFontSystem = 0, kFontGeneva = 3, kFontMonaco = 4 };

/* Error codes */
enum {
    kErrNoError          = 0,
    kErrMemoryFull       = 1,
    kErrWindowCreation   = 2,
    kErrControlCreation  = 3,
    kErrTextEditCreation = 4,
    kErrResourceNotFound = 5,
    kErrDiskFull         = 6,
    kErrUnknown          = 99
};

/* Error context codes - more efficient than passing strings */
enum {
    kCtxLaunchingApp        = 1,
    kCtxCreatingMainWindow  = 2,
    kCtxCreatingButton      = 3,
    kCtxShowingAboutBox     = 4,
    kCtxAboutBoxText        = 5,
    kCtxOpeningGameWindow   = 6,
    kCtxQuittingApplication = 11
};

/* Color constants if not defined */
#ifndef blackColor
#define blackColor 33
#endif

#ifndef whiteColor
#define whiteColor 30
#endif

#ifndef blueColor
#define blueColor 204
#endif

/* UI layout constants */
enum { kWindowMargin = 10, kGameViewMargin = 10, kGameBlockSelectorHeight = 40 };

/*********************************************************************
 * GLOBAL VARIABLES
 *********************************************************************/

/* External declarations for global variables defined in main.c */
extern short gAppMode; /* Current application mode */


#endif /* CONSTANTS_H */