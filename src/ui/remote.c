/* remote.c - see remote.h. */

#include <Files.h>
#include <Folders.h>
#include <Script.h>
#include <Processes.h>
#include <Strings.h>
#include <string.h>
#include <Files.h>

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
extern long Sprite_PaintedPixels;
extern long Sprite_Phase[3];

void Remote_ReportStats(long paintTicks, long paints, long steps)
{
    /* Fixed order, space separated:
     *   paintTicks paints steps paintedPixels phaseSetup phaseWorld phaseChrome
     * Every field is decimal, negative allowed. */
    char line[96];
    short i = 0;
    long values[7];
    short v;

    values[0] = paintTicks;
    values[1] = paints;
    values[2] = steps;
    values[3] = Sprite_PaintedPixels;
    values[4] = Sprite_Phase[0];
    values[5] = Sprite_Phase[1];
    values[6] = Sprite_Phase[2];

    for (v = 0; v < 7; v++) {
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

    if (vRef == 0 || name == NULL || name[0] == '\0') {
        return false;
    }
    while (name[i] != '\0' && i < 31) {
        pname[i + 1] = (unsigned char)name[i];
        i++;
    }
    pname[0] = (unsigned char)i;

    if (FSMakeFSSpec(vRef, 0, pname, &src) != noErr) {
        return false;
    }

    /* Destination: Preferences, next to the save file. */
    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &srcRef, &dirID) != noErr) {
        return false;
    }
    destName[0] = 15;
    memcpy(destName + 1, "MacCraft build", 14);
    FSMakeFSSpec(srcRef, dirID, destName, &dest);

    /* Copy the data fork. */
    if (FSpOpenDF(&src, fsRdPerm, &srcRef) == noErr) {
        SetEOF(0, 0);
        if (FSpCreate(&dest, 'MCft', 'APPL', smSystemScript) != noErr &&
            FSpCreate(&dest, 'MCft', 'APPL', smSystemScript) != dupFNErr) {
            /* An existing file of that name is fine: it is about to be replaced. */
        }
        if (FSpOpenDF(&dest, fsRdWrPerm, &dstRef) == noErr) {
            SetEOF(dstRef, 0);
            for (;;) {
                count = kForkBuffer;
                err = FSRead(srcRef, &count, sFork);
                if (count > 0) {
                    long wrote = count;

                    FSWrite(dstRef, &wrote, sFork);
                }
                if (err != noErr || count == 0) {
                    break;
                }
            }
            FSClose(dstRef);
        }
        FSClose(srcRef);
    }

    /* Copy the resource fork, which is where the code lives. */
    if (FSpOpenRF(&src, fsRdPerm, &srcRef) == noErr) {
        if (FSpOpenRF(&dest, fsWrPerm, &dstRef) == noErr) {
            SetEOF(dstRef, 0);
            for (;;) {
                count = kForkBuffer;
                err = FSRead(srcRef, &count, sFork);
                if (count > 0) {
                    long wrote = count;

                    FSWrite(dstRef, &wrote, sFork);
                }
                if (err != noErr || count == 0) {
                    break;
                }
            }
            FSClose(dstRef);
        }
        FSClose(srcRef);
    }

    memset(&pb, 0, sizeof(pb));
    pb.launchBlockID      = extendedBlock;
    pb.launchEPBLength    = extendedBlockLen;
    pb.launchFileFlags    = 0;
    pb.launchControlFlags = launchContinue | launchNoFileFlags;
    pb.launchAppSpec      = &dest;
    pb.launchAppParameters = NULL;

    if (LaunchApplication(&pb) != noErr) {
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
