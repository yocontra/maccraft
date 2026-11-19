/* save.h - keep the world between launches.
 *
 * The island is 48 x 48 x 8 cells of one byte each: 18,432 bytes, small enough
 * to write whole. The file lives in the Preferences folder next to the
 * self-test log, which is where a System 7 application is expected to put its
 * state, and it starts with a magic and a version so a future format can be
 * rejected instead of misread.
 */

#ifndef SAVE_H
#define SAVE_H

#include <Types.h>

/* Write the current world. Returns a File Manager result; noErr on success. */
OSErr Save_Write(void);

/* Read a saved world over the current one. Returns an error and leaves the world
 * untouched when the file is missing, short, or from another version. */
OSErr Save_Read(void);

/* True when a save file exists, so the caller can offer "resume" honestly. */
Boolean Save_Exists(void);

/* Delete the save; used by Reset World. */
OSErr Save_Delete(void);

#endif
