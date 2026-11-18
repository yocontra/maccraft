/*
 * Host stand-in for the Toolbox <Types.h>.
 *
 * src/game/world.h and iso.h include <Types.h>, which only exists in the
 * Retro68 / Multiversal Interfaces SDK.  The host tests put this directory
 * first on the include path so the two headers compile natively.
 */
#ifndef MACCRAFT_HOST_TYPES_H
#define MACCRAFT_HOST_TYPES_H

typedef unsigned char Boolean;

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#endif /* MACCRAFT_HOST_TYPES_H */
