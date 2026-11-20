#ifndef PSTRING_H
#define PSTRING_H

#include <Types.h>

/*
 * Pascal-string helpers, in one place.
 *
 * The About box, the splash screen and the HUD each had their own copy of
 * "append a C string", and they had already drifted on the length bound (254 in
 * one, 255 in another, where the byte after 254 characters is the one the
 * length byte cannot describe). These are the shared versions.
 */

/* Append a C string to a Pascal string, never past what a Str255 can hold. */
void PStr_AppendC(Str255 dst, const char *text);

/* Append a decimal number: signed long, no padding. */
void PStr_AppendNum(Str255 dst, long value);

/* Empty the string. */
void PStr_Clear(Str255 dst);

/* "Version <APP_VERSION_STR>", the small print both About and the splash show. */
void PStr_Version(Str255 dst);

#endif
