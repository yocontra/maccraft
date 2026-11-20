/*
 * pstring.c - shared Pascal-string helpers. See pstring.h for why.
 */

#include <Quickdraw.h>
#include <TextUtils.h>
#include <Types.h>

#include "pstring.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* A Str255 holds 255 characters in bytes 1..255; the length byte caps at 255,
 * so the last writable byte is index 255. */
#define kMaxChars 255

void PStr_Clear(Str255 dst)
{
    dst[0] = 0;
}

void PStr_AppendC(Str255 dst, const char *text)
{
    short len = (short)dst[0];

    while (*text != '\0' && len < kMaxChars) {
        dst[++len] = (unsigned char)*text++;
    }
    dst[0] = (unsigned char)len;
}

void PStr_AppendNum(Str255 dst, long value)
{
    Str255 num;
    short len = (short)dst[0];
    short i;

    NumToString(value, num);
    for (i = 1; i <= (short)num[0] && len < kMaxChars; i++) {
        dst[++len] = num[i];
    }
    dst[0] = (unsigned char)len;
}

void PStr_Version(Str255 dst)
{
    PStr_Clear(dst);
    PStr_AppendC(dst, "Version ");
    PStr_AppendC(dst, APP_VERSION_STR);
}
