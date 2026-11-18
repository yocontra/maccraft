#!/bin/sh
# 03-pan: five right-arrow presses pan the camera one tile step each and the
# rendered display visibly moves.
#
# Asserts:
#   - at least 20 % of the "display" region differs from the pre-pan capture
#
# The threshold is the plan's: it assumes the 12x12 plateau covers enough of
# the view that a 5 * 16 px pan (kGameCameraPanX) moves a fifth of the pixels.
# If it fails, the printed fraction is the evidence: either the pan step or
# the drawn world is smaller than the plan assumed.

set -eu
. "$(dirname "$0")/../lib.sh"

boot_to_game

emu move 5 5
emu sleep 1
emu shot before

i=0
while [ "$i" -lt 5 ]; do
    emu key right
    i=$((i + 1))
done

emu move 5 5
emu wait_stable display 20 0.002
emu changed display before.png 0.20
