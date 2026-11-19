/* remote.c - see remote.h. */

#include <Files.h>
#include <Folders.h>
#include <Strings.h>
#include <string.h>

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
