/* save.c - the world on disk.
 *
 * The block array itself is private to world.c, so the file is written and read
 * through the public accessors: World_Get over the whole grid on the way out,
 * World_Set on the way in. That keeps one owner for the storage layout and means
 * a save can never put the world into a state World_Set would reject.
 *
 * Header: 'MCWF' magic, a format version, and the world dimensions. A file that
 * disagrees about any of those is refused rather than partially applied.
 */

#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <Script.h>

#include "save.h"
#include "world.h"

#include "../constants.h"

#define kSaveMagic0 'M'
#define kSaveMagic1 'C'
#define kSaveMagic2 'W'
#define kSaveMagic3 'F'
#define kSaveVersion 1

static const unsigned char kSaveName[] = "\pMacCraft World";

/* Where the file lives: the Preferences folder if there is one, the application
 * folder otherwise, exactly where the self-test log goes. */
static OSErr SaveSpec(FSSpec *spec, Boolean create)
{
    short vRef;
    long dirID;
    OSErr err = noErr;

    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &vRef, &dirID) != noErr) {
        if (FindFolder(kOnSystemDisk, kPreferencesFolderType, 0, &vRef, &dirID) != noErr) {
            return fnfErr;
        }
    }

    /* FSMakeFSSpec answers fnfErr for a file that does not exist yet, which is
     * the normal case on a first save: it still fills in vRefNum and parID, so
     * the error is ignored here and only the create/write path can fail. */
    FSMakeFSSpec(vRef, dirID, kSaveName, spec);

    if (create) {
        OSErr created = FSpCreate(spec, 'MCft', 'TEXT', smSystemScript);

        if (created != noErr && created != dupFNErr) {
            err = created;
        }
    }
    return err;
}

Boolean Save_Exists(void)
{
    FSSpec spec;
    short refNum;
    OSErr err;

    if (SaveSpec(&spec, false) != noErr) {
        return false;
    }
    err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr) {
        return false;
    }
    FSClose(refNum);
    return true;
}

static void PutShort(unsigned char *p, short v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

static short GetShort(const unsigned char *p)
{
    return (short)((short)(p[0] << 8) | (short)p[1]);
}

OSErr Save_Write(void)
{
    FSSpec spec;
    short refNum;
    long count;
    long offset = 0;
    short x, y, z;
    unsigned char block[128];
    OSErr err;

    err = SaveSpec(&spec, true);
    if (err != noErr) {
        return err;
    }
    err = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (err != noErr) {
        return err;
    }
    SetEOF(refNum, 0);

    /* Header: magic, version, dimensions. */
    block[0] = kSaveMagic0;
    block[1] = kSaveMagic1;
    block[2] = kSaveMagic2;
    block[3] = kSaveMagic3;
    PutShort(block + 4, kSaveVersion);
    PutShort(block + 6, kWorldX);
    PutShort(block + 8, kWorldY);
    PutShort(block + 10, kWorldZ);
    count = 12;
    err = FSWrite(refNum, &count, block);
    if (err != noErr) {
        FSClose(refNum);
        return err;
    }

    /* The grid, in file order, one byte per cell, flushed in blocks so the
     * write does not become 18,432 File Manager calls on a 68k. */
    for (x = 0; x < kWorldX; x++) {
        for (y = 0; y < kWorldY; y++) {
            for (z = 0; z < kWorldZ; z++) {
                block[offset++] = (unsigned char)World_Get(x, y, z);
                if (offset == (long)sizeof(block)) {
                    count = offset;
                    err = FSWrite(refNum, &count, block);
                    if (err != noErr) {
                        FSClose(refNum);
                        return err;
                    }
                    offset = 0;
                }
            }
        }
    }
    if (offset > 0) {
        count = offset;
        err = FSWrite(refNum, &count, block);
    }

    SetFPos(refNum, fsFromStart, 0);
    FSClose(refNum);
    return err;
}

OSErr Save_Read(void)
{
    FSSpec spec;
    short refNum;
    long count;
    unsigned char block[128];
    short x, y, z;
    long offset = 0;
    OSErr err;

    err = SaveSpec(&spec, false);
    if (err != noErr) {
        return err;
    }
    err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr) {
        return err;
    }

    count = 12;
    err = FSRead(refNum, &count, block);
    if (err != noErr || count != 12 ||
        block[0] != kSaveMagic0 || block[1] != kSaveMagic1 ||
        block[2] != kSaveMagic2 || block[3] != kSaveMagic3 ||
        GetShort(block + 4) != kSaveVersion ||
        GetShort(block + 6) != kWorldX ||
        GetShort(block + 8) != kWorldY ||
        GetShort(block + 10) != kWorldZ) {
        FSClose(refNum);
        return (OSErr)-1; /* wrong magic, version or size: refuse the file */
    }

    /* Read the whole payload before touching the world. Applying it as it
     * arrived meant a file truncated half way left the live world partly
     * replaced with zeroes even though the call reported an error -- and the
     * caller's "is there an island" sanity count still passed. */
    {
        static unsigned char staging[kWorldX * kWorldY * kWorldZ];
        long got = 0;

        while (got < (long)sizeof(staging)) {
            count = (long)sizeof(staging) - got;
            err = FSRead(refNum, &count, staging + got);
            if (err != noErr && err != eofErr) {
                FSClose(refNum);
                return err;
            }
            if (count <= 0) {
                break;
            }
            got += count;
        }
        if (got != (long)sizeof(staging)) {
            FSClose(refNum);
            return (OSErr)-2;   /* short file: leave the world alone */
        }

        for (x = 0; x < kWorldX; x++) {
            for (y = 0; y < kWorldY; y++) {
                for (z = 0; z < kWorldZ; z++) {
                    World_Set(x, y, z, (BlockID)staging[offset++]);
                }
            }
        }
    }

    FSClose(refNum);
    return noErr;
}

OSErr Save_Delete(void)
{
    FSSpec spec;

    if (SaveSpec(&spec, false) != noErr) {
        return fnfErr;
    }
    return FSpDelete(&spec);
}
