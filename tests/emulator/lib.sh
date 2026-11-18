#!/bin/sh
# Helper functions for tests/emulator/scenarios/*.sh.
#
# Scenario scripts are POSIX sh and are started by tests/emulator/run.sh, which
# exports EMU_DRIVER and EMU_PYTHON.  Every `emu` call is one driver.py command
# against the already-running emulator.

: "${EMU_DRIVER:?scenarios must be run through tests/emulator/run.sh}"
: "${EMU_PYTHON:?scenarios must be run through tests/emulator/run.sh}"

emu() {
    "$EMU_PYTHON" "$EMU_DRIVER" "$@"
}

# Boot the guest, wait for the splash screen and open the game window.
boot_to_game() {
    emu wait_ocr full "Start" 120
    emu click start-button
    emu wait_no_ocr full "Start" 60
    emu wait_stable display 60 0.002
}
