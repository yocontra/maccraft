#ifndef SELFTEST_H
#define SELFTEST_H

#include <Types.h>

/* True when the harness dropped the trigger file into the Preferences folder. */
Boolean SelfTest_Requested(void);

/* Drive the game through its own click and key handlers, log the results, then
   quit. Never returns. */
void SelfTest_Run(void);

#endif
