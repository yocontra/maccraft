/* remote.c - see remote.h. */

#include <Files.h>
#include <Folders.h>
#include <Script.h>
#include <Processes.h>
#include <Strings.h>
#include <string.h>
#include <Files.h>

#include "diagnostics.h"
#include "game_window.h"
#include "hover.h"
#include "../game/pick.h"
#include "world_view.h"
#include "remote.h"

/* What the caller decided, so the receipt can say whether the command was
 * understood. Set by Remote_SetResult before each poll. */
static char sResult[48] = "none";

void Remote_SetResult(const char *text)
{
    short i = 0;

    while (text[i] != '\0' && i < (short)sizeof(sResult) - 1) {
        sResult[i] = text[i];
        i++;
    }
    sResult[i] = '\0';
}

static const unsigned char kShareVolume[] = "\pUnix";
static const unsigned char kCommandName[] = "\pMacCraft cmd";
static const unsigned char kAckName[] = "\pMacCraft ack";

/* Find the extfs volume by name. The share is mounted for the whole session, so
 * the answer is cached; a miss is retried next time in case the share appears
 * late in the boot. */
static short ShareVolume(void)
{
    static short sVRef = 0;
    short index;
    HParamBlockRec pb;

    if (sVRef != 0) {
        return sVRef;
    }

    for (index = 1; index < 32; index++) {
        Str63 name;

        memset(&pb, 0, sizeof(pb));
        memset(name, 0, sizeof(name));
        pb.volumeParam.ioNamePtr = name;
        pb.volumeParam.ioVolIndex = index;
        if (PBHGetVInfoSync((HParmBlkPtr)&pb) != noErr) {
            break;
        }
        if (EqualString(name, kShareVolume, false, false)) {
            sVRef = pb.volumeParam.ioVRefNum;
            return sVRef;
        }
    }
    return 0;
}

/* Write the receipt for a command the caller has acted on. Kept separate from
 * Remote_Take because the decision is made after the read returns, and a receipt
 * written before it would describe the previous command. */
/* Write a stats line into the receipt: paint ticks, paint count, physics steps. */
/* Defined by the game window: total pixels the world pass has covered. */

void Remote_ReportStats(long paintTicks, long paints, long steps)
{
    /* Fixed order, space separated:
     *   paintTicks paints steps paintedPixels phaseSetup phaseWorld phaseChrome
     * Every field is decimal, negative allowed. */
    char line[96];
    short i = 0;
    long values[8];
    short v;

    values[0] = paintTicks;
    values[1] = paints;
    values[2] = steps;
    values[3] = WorldView_Stats[0];
    values[4] = Sprite_Phase[0];
    values[5] = Sprite_Phase[1];
    values[6] = Sprite_Phase[2];
    values[7] = WorldView_Stats[1];

    for (v = 0; v < 8; v++) {
        char digits[12];
        short n = 0;
        long value = values[v];

        if (value < 0) {
            if (i < (short)sizeof(line) - 2) {
                line[i++] = '-';
            }
            value = -value;
        }
        if (value == 0) {
            digits[n++] = '0';
        }
        while (value > 0 && n < 11) {
            digits[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (n > 0 && i < (short)sizeof(line) - 2) {
            line[i++] = digits[--n];
        }
        if (i < (short)sizeof(line) - 2) {
            line[i++] = ' ';
        }
    }
    line[i] = '\0';

    Remote_SetResult(line);
}

void Remote_AppendResult(const char *text)
{
    short n = (short)strlen(sResult);

    while (*text != 0 && n < (short)sizeof(sResult) - 1) {
        sResult[n++] = *text++;
    }
    sResult[n] = 0;
}

/* The command language. It lives here rather than in the window module: it is
 * harness plumbing, not a rendering concern, and the window file is already the
 * biggest unit in the program. */
static short TakeInt(const char **p)
{
    short value = 0;
    Boolean negative = false;

    while (**p == ' ') {
        (*p)++;
    }
    if (**p == '-') {
        negative = true;
        (*p)++;
    }
    while (**p >= '0' && **p <= '9') {
        value = (short)(value * 10 + (**p - '0'));
        (*p)++;
    }
    return negative ? (short)-value : value;
}

/* Run a command from the harness: walk, jump, select. Every verb goes through
 * the same code the keyboard and the mouse use, so what the harness proves is
 * what a player gets. */
static Boolean PickSurfaceAt(Point localPt, short *outX, short *outY, short *outZ);
static Boolean PickSurfaceAtFace(Point localPt, short *outX, short *outY, short *outZ,
                                 short *outFace);

void Remote_Apply(const char *cmd)
{
    if (strncmp(cmd, "walk", 4) == 0) {
        const char *p = cmd + 4;
        short dx = TakeInt(&p);
        short dy = TakeInt(&p);
        short ticks = TakeInt(&p);

        Remote_SetResult("walk");
        GameWindow_WalkForTest(dx, dy, ticks);
    }
    else if (strncmp(cmd, "jump", 4) == 0) {
        Remote_SetResult("jump");
        GameWindow_JumpForTest();
    }
    else if (strncmp(cmd, "select", 6) == 0) {
        const char *p = cmd + 6;
        short n = TakeInt(&p);

        if (GameWindow_SelectBlockType((short)(n - 1))) {
            Remote_SetResult("select");
        }
        else {
            Remote_SetResult("select out of range");
        }
    }
    else if (strncmp(cmd, "launch", 6) == 0) {
        const char *p = cmd + 6;

        while (*p == ' ') {
            p++;
        }
        Remote_SetResult("launch");
        Remote_Ack(cmd);
        Remote_Launch(p);
        /* Only reached when the launch failed. */
        Remote_SetResult("launch failed");
    }
    else if (strncmp(cmd, "pick", 4) == 0) {
        /* Harness: resolve a window-space point through the picker and report
         * the tile plus where that tile projects, so the two can be compared
         * without reading pixels. */
        const char *p = cmd + 4;
        short px = TakeInt(&p);
        short py = TakeInt(&p);
        short tx = 0, ty = 0, tz = 0;
        char result[64];

        short face = 0;

        if (GameWindow_PickAt(px, py, &tx, &ty, &tz, &face)) {
            short sx = 0, sy = 0;

            GameWindow_TileScreen(tx, ty, (short)(tz - 1), &sx, &sy);
            {
                char *q = result;
                short vals[4];
                short k;

                short bx = 0, by = 0;

                GameWindow_TileScreen(tx, ty, (short)(tz - 1), &bx, &by);
                vals[0] = bx;
                vals[1] = by;
                vals[2] = face;
                vals[3] = tz;
                for (k = 0; k < 5; k++) {
                    if (k == 4) {
                        /* The point is inside the tile's diamond, so its centre
                         * is where the box will be drawn. */
                        short digits2[6];
                        short n2 = 0;
                        long v2 = (long)sy;

                        if (v2 < 0) { *q++ = '-'; v2 = -v2; }
                        do { digits2[n2++] = (short)(v2 % 10); v2 /= 10; } while (v2 > 0);
                        while (n2 > 0) { *q++ = (char)('0' + digits2[--n2]); }
                        break;
                    }
                    short digits[6];
                    short n = 0;
                    long v = vals[k];

                    if (v < 0) { *q++ = '-'; v = -v; }
                    do { digits[n++] = (short)(v % 10); v /= 10; } while (v > 0);
                    while (n > 0) { *q++ = (char)('0' + digits[--n]); }
                    if (k < 3) { *q++ = ' '; }
                }
                *q = 0;
                (void)sy;
            }
        }
        else {
            strcpy(result, "none");
        }
        Remote_SetResult(result);
    }
    else if (strncmp(cmd, "place", 5) == 0) {
        /* Harness: resolve the point, run the click handler, report which cell
         * the click acted on -- face placement is then checkable without
         * reading pixels. */
        const char *p = cmd + 5;
        short px = TakeInt(&p);
        short py = TakeInt(&p);
        Point probe;
        short tx = 0, ty = 0, tz = 0, face = 0;
        char result[64];

        probe.h = px;
        probe.v = py;
        if (!GameWindow_PickAt(px, py, &tx, &ty, &tz, &face)) {
            strcpy(result, "nopick");
        }
        else {
            short vals[4];
            short k, q = 0;

            vals[0] = tx;
            vals[1] = ty;
            vals[2] = tz;
            vals[3] = face;
            for (k = 0; k < 4; k++) {
                short digits[6];
                short n = 0;
                long v = vals[k];

                if (v == 0) {
                    digits[n++] = 0;
                }
                while (v > 0) {
                    digits[n++] = (short)(v % 10);
                    v /= 10;
                }
                while (n > 0) {
                    result[q++] = (char)('0' + digits[--n]);
                }
                if (k < 3) {
                    result[q++] = ' ';
                }
            }
            result[q] = 0;

            /* Report the cell the click will act on and whether it was free:
             * "tile x y z face -> cell x y z wasEmpty". */
            {
                short cX = tx, cY = ty, cZ;
                short wasEmpty;

                if (face == kPickFaceLeft) {
                    cY = (short)(ty + 1);
                    cZ = (short)(tz - 1);
                }
                else if (face == kPickFaceRight) {
                    cX = (short)(tx + 1);
                    cZ = (short)(tz - 1);
                }
                else {
                    cZ = (short)(World_TopZ(tx, ty) + 1);
                }

                wasEmpty = (World_Get(cX, cY, cZ) == 0) ? 1 : 0;
                {
                    const char *word = wasEmpty ? " free" : " busy";
                    short w = 0;

                    while (word[w] != 0 && q < (short)sizeof(result) - 1) {
                        result[q++] = word[w++];
                    }
                    result[q] = 0;
                }

                GameWindow_ClickForTest(px, py, false);
            }
        }
        Remote_SetResult(result);
    }
    else if (strncmp(cmd, "toggle", 6) == 0) {
        /* Same entry point the Game menu's Toggle Block Type uses. */
        GameWindow_ToggleBlockType();
        Remote_SetResult("toggle");
    }
    else if (strncmp(cmd, "where", 5) == 0) {
        char out[48];
        short q = 0;
        long vals[4];
        short k;

        vals[0] = GameWindow_PlayerTileX();
        vals[1] = GameWindow_PlayerTileY();
        vals[2] = GameWindow_PlayerZ();
        vals[3] = (long)GameWindow_PlayerGrounded();
        for (k = 0; k < 4; k++) {
            short digits[12];
            short n = 0;
            long v = vals[k];

            if (v < 0) { out[q++] = '-'; v = -v; }
            if (v == 0) { digits[n++] = 0; }
            while (v > 0 && n < 11) { digits[n++] = (short)(v % 10); v /= 10; }
            while (n > 0) { out[q++] = (char)('0' + digits[--n]); }
            if (k < 3) { out[q++] = ' '; }
        }
        out[q] = 0;
        Remote_SetResult(out);
    }
    else if (strncmp(cmd, "stats", 5) == 0) {
        Remote_ReportStats(Sprite_Work[2], Sprite_Work[3], Sprite_Work[5]);
        {
            char hover[40];
            char *q = hover;
            Point m;

            GameWindow_CursorLocal(&m);

            *q++ = (char)(Hover_Valid() ? 'V' : '-');
            *q++ = ' ';
            {   /* window-space cursor, then the hovered tile */
                short vals[3];
                short k;

                vals[0] = m.h;
                Hover_Tile(&vals[1], &vals[2], NULL);
                for (k = 0; k < 3; k++) {
                    short digits[6];
                    short n = 0;
                    long v = vals[k];

                    if (v < 0) { *q++ = '-'; v = -v; }
                    do { digits[n++] = (short)(v % 10); v /= 10; } while (v > 0);
                    while (n > 0) { *q++ = (char)('0' + digits[--n]); }
                    if (k < 2) { *q++ = ' '; }
                }
            }
            *q = 0;
            Remote_AppendResult(hover);
        }
    }
    else if (strncmp(cmd, "mine", 4) == 0) {
        const char *p = cmd + 4;
        short mx = TakeInt(&p);
        short my = TakeInt(&p);
        short mz = TakeInt(&p);

        Remote_SetResult("mine");
        GameWindow_MineForTest(mx, my, mz, 60);
    }
    else if (strncmp(cmd, "save", 4) == 0) {
        Remote_SetResult("save");
        GameWindow_SaveWorld();
    }
    else {
        Remote_SetResult("unknown verb");
    }
}

void Remote_Ack(const char *cmd)
{
    short vRef = ShareVolume();
    FSSpec ack;
    short ackRef;
    char line[96];
    short i = 0;
    short j;

    if (vRef == 0) {
        return;
    }
    if (FSMakeFSSpec(vRef, 0, kAckName, &ack) != fnfErr &&
        FSMakeFSSpec(vRef, 0, kAckName, &ack) != noErr) {
        return;
    }
    {
        OSErr created = FSpCreate(&ack, 'MCft', 'TEXT', 0);

        if (created != noErr && created != dupFNErr) {
            return;
        }
    }
    if (FSpOpenDF(&ack, fsRdWrPerm, &ackRef) != noErr) {
        return;
    }

    while (cmd[i] != '\0' && i < 40) {
        line[i] = cmd[i];
        i++;
    }
    line[i++] = ' ';
    line[i++] = '-';
    line[i++] = '>';
    line[i++] = ' ';
    for (j = 0; sResult[j] != '\0' && i < (short)sizeof(line) - 2; j++) {
        line[i++] = sResult[j];
    }
    line[i++] = '\n';

    {
        long wrote = i;

        SetEOF(ackRef, 0);
        FSWrite(ackRef, &wrote, line);
    }
    FSClose(ackRef);
}

/* Relaunch the application from the shared folder, then quit this one.
 *
 * This is how a new build is loaded without rebooting the guest: the harness
 * stages the build into an alternating slot on the share and asks the running
 * app to launch it. The running app's own file is on the boot volume and is
 * never touched, and the slot alternates so the file being executed is never
 * overwritten either. */
/* Relaunch the application from a fresh build in the shared folder.
 *
 * The build is first copied onto the boot volume -- data fork and resource fork
 * -- and launched from there. Launching straight out of the extfs volume killed
 * the emulator outright, twice; an application running from the local disk is
 * the same situation as the one in Startup Items, which is known to work.
 *
 * The copy is a fixed two-file scheme ("MacCraft build" and its predecessor) so
 * the running application's own file is never touched.
 */
#define kForkBuffer 81920L

Boolean Remote_Launch(const char *name)
{
    short vRef = ShareVolume();
    FSSpec src;
    FSSpec dest;
    LaunchParamBlockRec pb;
    Str255 pname;
    Str255 destName;
    short i = 0;
    short srcRef;
    short dstRef;
    OSErr err;
    long count;
    static unsigned char sFork[kForkBuffer];
    short dirRef;
    long dirID;
    /* Each launch gets its own file. Reusing one name meant launching a file the
     * Finder or the previous launch still had open, which is where the silent
     * "launch failed" replies came from. */
    static short sLaunchSeq = 0;
    char seqDigits[2];

    if (vRef == 0 || name == NULL || name[0] == '\0') {
        Remote_SetResult("launch: no name");
        return false;
    }
    while (name[i] != '\0' && i < 31) {
        pname[i + 1] = (unsigned char)name[i];
        i++;
    }
    pname[0] = (unsigned char)i;

    if (FSMakeFSSpec(vRef, 0, pname, &src) != noErr) {
        Remote_SetResult("launch: no app in share");
        return false;
    }

    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &srcRef, &dirID) != noErr) {
        Remote_SetResult("launch: no Preferences");
        return false;
    }

    /* "MacCraft run N", N cycling 1..8, so old copies are replaced rather than
     * accumulating. */
    sLaunchSeq = (short)(sLaunchSeq % 8);
    seqDigits[0] = (char)('1' + sLaunchSeq);
    {
        static const char prefix[] = "MacCraft run ";
        short n = 0;

        destName[0] = 14;   /* "MacCraft run " is 13 chars plus the slot digit */
        while (prefix[n] != '\0') {
            destName[n + 1] = (unsigned char)prefix[n];
            n++;
        }
        destName[n + 1] = (unsigned char)seqDigits[0];
    }
    sLaunchSeq++;

    FSMakeFSSpec(srcRef, dirID, destName, &dest);
    FSpDelete(&dest); /* an old copy of this slot; its absence is fine */

    err = FSpCreate(&dest, 'MCft', 'APPL', smSystemScript);
    if (err != noErr && err != dupFNErr) {
        Remote_SetResult("launch: create failed");
        return false;
    }

    /* Data fork (usually empty: the code lives in the resource fork). */
    if (FSpOpenDF(&src, fsRdPerm, &srcRef) != noErr) {
        Remote_SetResult("launch: no data fork");
        return false;
    }
    if (FSpOpenDF(&dest, fsRdWrPerm, &dstRef) != noErr) {
        FSClose(srcRef);
        Remote_SetResult("launch: data open failed");
        return false;
    }
    SetEOF(dstRef, 0);
    for (;;) {
        count = kForkBuffer;
        err = FSRead(srcRef, &count, sFork);
        if (count > 0) {
            long wrote = count;

            if (FSWrite(dstRef, &wrote, sFork) != noErr || wrote != count) {
                FSClose(dstRef);
                FSClose(srcRef);
                Remote_SetResult("launch: data write failed");
                return false;
            }
        }
        if (err == eofErr || count == 0) {
            break;
        }
        if (err != noErr) {
            FSClose(dstRef);
            FSClose(srcRef);
            Remote_SetResult("launch: data read failed");
            return false;
        }
    }
    FSClose(dstRef);
    FSClose(srcRef);

    /* Resource fork: the code. */
    if (FSpOpenRF(&src, fsRdPerm, &srcRef) != noErr) {
        Remote_SetResult("launch: no resource fork");
        return false;
    }
    if (FSpOpenRF(&dest, fsWrPerm, &dstRef) != noErr) {
        FSClose(srcRef);
        Remote_SetResult("launch: resource open failed");
        return false;
    }
    SetEOF(dstRef, 0);
    for (;;) {
        count = kForkBuffer;
        err = FSRead(srcRef, &count, sFork);
        if (count > 0) {
            long wrote = count;

            if (FSWrite(dstRef, &wrote, sFork) != noErr || wrote != count) {
                FSClose(dstRef);
                FSClose(srcRef);
                Remote_SetResult("launch: resource write failed");
                return false;
            }
        }
        if (err == eofErr || count == 0) {
            break;
        }
        if (err != noErr) {
            FSClose(dstRef);
            FSClose(srcRef);
            Remote_SetResult("launch: resource read failed");
            return false;
        }
    }
    FSClose(dstRef);
    FSClose(srcRef);

    memset(&pb, 0, sizeof(pb));
    pb.launchBlockID      = extendedBlock;
    pb.launchEPBLength    = extendedBlockLen;
    pb.launchFileFlags    = 0;
    pb.launchControlFlags = launchContinue | launchNoFileFlags;
    pb.launchAppSpec      = &dest;
    pb.launchAppParameters = NULL;

    err = LaunchApplication(&pb);
    if (err != noErr) {
        char msg[40];
        char *q = msg;

        *q++ = 'l'; *q++ = 'a'; *q++ = 'u'; *q++ = 'n'; *q++ = 'c';
        *q++ = 'h'; *q++ = ' '; *q++ = 'r'; *q++ = 'c'; *q++ = '=';
        {
            short digits[6];
            short n = 0;
            long v = (long)err;

            if (v < 0) {
                *q++ = '-';
                v = -v;
            }
            do {
                digits[n++] = (short)(v % 10);
                v /= 10;
            } while (v > 0 && n < 5);
            while (n > 0) {
                *q++ = (char)('0' + digits[--n]);
            }
        }
        *q = '\0';
        Remote_SetResult(msg);
        return false;
    }

    ExitToShell();
    return true;
}

Boolean Remote_Take(char *buf, short max)
{
    short vRef = ShareVolume();
    FSSpec spec;
    short refNum;
    long count;
    OSErr err;

    if (vRef == 0 || max <= 1) {
        return false;
    }
    if (FSMakeFSSpec(vRef, 0, kCommandName, &spec) != noErr) {
        return false;
    }
    /* Read-only: the file belongs to the host user and extfs does not always
     * grant the guest write access, but reading and then deleting it is enough. */
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) {
        return false;
    }

    count = (long)(max - 1);
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);
    /* Consume the command whether or not the read was clean: a repeated command
     * would otherwise replay for ever. */
    FSpDelete(&spec);

    if ((err != noErr && err != eofErr) || count <= 0) {
        return false;
    }

    buf[count] = '\0';

    /* Strip the newline the shell leaves at the end. */
    while (count > 0 && (buf[count - 1] == '\n' || buf[count - 1] == '\r')) {
        buf[--count] = '\0';
    }
    return (Boolean)(count > 0);
}
