#ifndef SELFTEST_H
#define SELFTEST_H

#include <Types.h>

/* True when the harness dropped the trigger file into the Preferences folder. */
Boolean SelfTest_Requested(void);

/* Harness: open the game directly instead of the splash (and stop the self-test
 * from running again on a relaunch). */
Boolean SelfTest_PlayRequested(void);

/* Drive the game through its own click and key handlers, log the results, then
   quit. Never returns. */
void SelfTest_Run(void);

#endif
