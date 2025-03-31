#include "Types.r"
#include "Menus.r"
#include "Windows.r"
#include "Dialogs.r"

/* Apple menu */
resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About MacCraft...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

/* File menu */
resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "New Game", noIcon, "N", noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

/* Game menu */
resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Game";
    {
        "Toggle Block Type", noIcon, "T", noMark, plain;
        "Reset World", noIcon, noKey, noMark, plain;
    }
};

/* Menu bar definition */
resource 'MBAR' (128) {
    { 128, 129, 130 };
};

/* String resources */
resource 'STR#' (128) {
    {
        "MacCraft";
    }
};

/* About box text */
data 'TEXT' (128) {
    "MacCraft\r\r"
    "A 3D voxel game for classic Macintosh\r\r"
    "Build and explore in this lightweight block-based construction game designed specifically for 68K Macintosh computers.\r\r"
    "Press Cmd-N to start a new game."
};

/* About box window */
resource 'WIND' (128) {
    {40, 40, 260, 360}, altDBoxProc;
    invisible;
    noGoAway;
    0, "";
    noAutoCenter;
};

/* Main application window */
resource 'WIND' (129) {
    {40, 40, 640, 480}, documentProc;
    visible;
    goAway;
    0, "MacCraft";
    centerMainScreen;
};

/* Game window */
resource 'WIND' (130) {
    {50, 50, 640, 480}, documentProc;
    visible;
    goAway;
    0, "MacCraft - Game";
    centerMainScreen;
};

/* Error alert */
resource 'ALRT' (200) {
    {50, 50, 150, 350},
    200,
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    },
    alertPositionMainScreen
};

resource 'DITL' (200) {
    {
        {70, 250, 90, 310},
        Button { enabled, "OK" };
        
        {10, 10, 60, 310},
        StaticText { enabled, "^0" };
    }
};

/* Application icon */
resource 'ICON' (128) {
    /* 32x32 1-bit icon for MacCraft - cube */
    $"0000 0000 0000 0000"
    $"0000 0000 0000 0000"
    $"0000 0000 0000 0000"
    $"0000 FFFF FFFF 0000"
    $"0000 FFFF FFFF 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 F000 000F 0000"
    $"0000 FFFF FFFF 0000"
    $"0000 FFFF FFFF 0000"
    $"0000 0000 0000 0000"
    $"0000 0000 0000 0000"
    $"0000 0000 0000 0000"
    $"0000 0000 0000 0000"
};

/* Application icon for Finder */
resource 'ICN#' (128) {
    {
        /* 32x32 icon */
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 F000 000F 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000",
        
        /* Mask */
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 FFFF FFFF 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
        $"0000 0000 0000 0000"
    }
};

/* Memory allocation */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    768 * 1024,   /* Minimal memory size for world data and rendering */
    1024 * 1024   /* Preferred memory size with buffer for larger worlds */
};