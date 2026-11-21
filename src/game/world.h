#ifndef WORLD_H
#define WORLD_H

#include <Types.h>

#define kWorldX 96
#define kWorldY 96
#define kWorldZ 8

typedef unsigned char BlockID; /* 0 empty, 1 dirt, 2 stone, 3 grass */

void    World_Reset(void); /* flat 12x12 grass plateau centred at 24,24 */
BlockID World_Get(short x, short y, short z);
Boolean World_Set(short x, short y, short z, BlockID id); /* false when out of range */
short   World_Height(short x, short y);                   /* stacked blocks 0..kWorldZ */
short   World_TopZ(short x, short y);                     /* highest occupied z, -1 when empty */

#endif
