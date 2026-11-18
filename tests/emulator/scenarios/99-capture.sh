#!/bin/sh
# 99-capture: boot with the in-guest self-test trigger and photograph the run.
#
# No input events are posted: the game drives itself (see src/selftest.c) and
# this scenario only reads the screen. Use it to look at what actually renders.
#
#   MAC_EMU_SELFTEST=1 tests/emulator/run.sh 99-capture --record
#
# Frames land in tests/emulator/.work/shots/frameNN.png.

set -eu
. "$(dirname "$0")/../lib.sh"

# The app launches itself from Startup Items; give the boot time to finish.
emu sleep 45

i=1
while [ "$i" -le 30 ]; do
    emu shot "frame$(printf '%02d' "$i")" >/dev/null
    emu sleep 3
    i=$((i + 1))
done
