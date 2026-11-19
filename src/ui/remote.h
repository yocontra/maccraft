/* remote.h - commands from the host, through the shared folder.
 *
 * The emulator harness cannot deliver keyboard events into the guest: host focus
 * belongs to whoever is working on the Mac, and synthetic events posted at the
 * HID tap go to that window. The shared folder is the one channel both sides
 * already use, so the harness drops a line into "Unix:MacCraft cmd" and the game
 * reads it on its next idle pass. That makes input testable end to end -- the
 * commands run through the same code the keyboard and mouse run through.
 *
 * This is a test hook, not a game feature: nothing in the game writes to it, and
 * with no file present it costs one failed File Manager open per second.
 */

#ifndef REMOTE_H
#define REMOTE_H

#include <Types.h>

/* Read and consume a pending command line. Returns true when one was taken;
 * `buf` is NUL-terminated and at most max-1 characters. */
Boolean Remote_Take(char *buf, short max);

/* Record what the caller did with the last command; it goes into the receipt
 * file so the host can see whether the command was understood. */
void Remote_SetResult(const char *text);

/* Launch a build from the shared folder and quit this one. Does not return when
 * it succeeds, which is why it is a separate call rather than a verb return. */
Boolean Remote_Launch(const char *name);

/* Write a one-line stats report into the receipt. */
void Remote_ReportStats(long paintTicks, long paints, long steps);

/* Append text to the result Remote_Ack will write. */
void Remote_AppendResult(const char *text);

/* Write the receipt for a command the caller has acted on. */
void Remote_Ack(const char *cmd);

#endif
