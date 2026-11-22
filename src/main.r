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
        "-", noIcon, noKey, noMark, plain;
        "Save World", noIcon, noKey, noMark, plain;
        "Toggle Day / Night", noIcon, "N", noMark, plain;
    }
};

/* Menu bar definition */
resource 'MBAR' (128) {
    { 128, 129, 130 };
};

/* About box - modal dialog */
resource 'DLOG' (128) {
    {80, 60, 300, 460},
    dBoxProc,
    visible,
    noGoAway,
    0,
    128,
    "About MacCraft",
    noAutoCenter
};

resource 'DITL' (128) {
    {
        {180, 150, 200, 250},
        Button { enabled, "OK" };

        {10, 10, 150, 390},
        StaticText { enabled,
            "MacCraft\r\r"
            "A 3D voxel game for classic Macintosh.\r\r"
            "Build and explore in a lightweight block-based\r"
            "construction game for 68K Macs.\r\r"
            "Shift-click a column to remove a block." };

        {150, 10, 172, 390},
        StaticText { enabled, "Version ^0" };
    }
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

        {8, 10, 40, 290},
        StaticText { enabled, "^0" };

        {40, 10, 66, 290},
        StaticText { enabled, "^1" };
    }
};

/* Memory allocation */
resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    3072 * 1024,  /* Two 108K light grids, a 108K world, and the sprite sheet */
    4096 * 1024   /* Preferred */
};
