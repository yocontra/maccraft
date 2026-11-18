#!/bin/sh
# 02-place-block: clicking the centre tile places a block; shift-clicking the
# same tile removes it and restores the frame exactly.
#
# Asserts:
#   - clicking the tile point changes at least 5 % of the "tile" region
#   - the "outside-tile" region is unchanged (<= 1 %)
#   - shift-clicking the same point restores "tile" to <= 2 % of before.png
#
# The cursor is parked in the menu bar before every capture so it cannot be
# mistaken for a block.

set -eu
. "$(dirname "$0")/../lib.sh"

boot_to_game

emu move 5 5
emu shot before

emu click tile
emu sleep 1
emu move 5 5
emu sleep 1

emu changed tile before.png 0.05
emu stable outside-tile before.png 0.01

emu click tile --shift
emu sleep 1
emu move 5 5
emu sleep 1

emu stable tile before.png 0.02
