#!/bin/sh
# 01-launch: the app boots, shows the splash screen with a Start button, and
# opening the game window paints the initial world.
#
# Asserts:
#   - OCR of the full screen contains "Start" within 120 s of boot
#   - the splash frame matches baselines/launch.png (<= 2 % changed)
#   - clicking the Start Game button removes the splash ("Start" disappears)
#   - the game frame matches baselines/game.png (<= 2 % changed)
#
# Run once with --record to write both baselines, review them, then commit.

set -eu
. "$(dirname "$0")/../lib.sh"

emu wait_ocr full "Start" 120

# Park the guest cursor in the menu bar so it is not part of the splash diff.
emu move 5 5
emu wait_stable full 30 0.002
emu stable full baselines/launch.png 0.02

emu click start-button
emu wait_no_ocr full "Start" 60
emu wait_stable display 60 0.002

# The game window inherits the cursor position from the click; move it away.
emu move 5 5
emu wait_stable full 10 0.002
emu stable full baselines/game.png 0.02
