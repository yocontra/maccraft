#!/bin/sh
# Entry point for the MacCraft emulator tests.
#
#   tests/emulator/run.sh <scenario> [--record] [--keep]
#
# <scenario> is a name under scenarios/ with or without the .sh suffix.
# --record writes tests/emulator/baselines/*.png instead of asserting against
# them (use it once, then review the images and commit them).
# --keep leaves tests/emulator/.work/Test.sheepvm and the cloned boot volume
# in place for debugging.
#
# Exit codes:
#   0  pass
#   1  assertion failed (the observed value and OCR text are printed)
#   2  environment missing (no ROM, boot disk, built .APPL, tesseract,
#      pyobjc/Pillow, or macOS Accessibility/Screen Recording permission)
#
# run.sh never builds the application.  Run `make build APP_NAME=MacCraft`
# first so that dist/MacCraft.APPL exists.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)

PY=${EMU_PYTHON:-}
if [ -z "$PY" ]; then
    if [ -x "$HERE/.venv/bin/python3" ]; then
        PY="$HERE/.venv/bin/python3"
    elif [ -x /opt/homebrew/bin/python3 ]; then
        PY=/opt/homebrew/bin/python3
    else
        PY=$(command -v python3 || true)
    fi
fi
if [ -z "$PY" ]; then
    echo "run.sh: python3 not found" >&2
    exit 2
fi
export EMU_PYTHON=$PY

if [ $# -lt 1 ]; then
    echo "usage: tests/emulator/run.sh <scenario> [--record] [--keep]" >&2
    echo "scenarios: $(cd "$HERE/scenarios" && ls *.sh 2>/dev/null | sed 's/\.sh$//' | tr '\n' ' ')" >&2
    exit 2
fi

NAME=$1
shift
case "$NAME" in
    *.sh) ;;
    *) NAME="$NAME.sh" ;;
esac

SCENARIO="$HERE/scenarios/$NAME"
if [ ! -f "$SCENARIO" ]; then
    echo "run.sh: no such scenario: $SCENARIO" >&2
    exit 2
fi

exec "$PY" "$HERE/driver.py" --scenario "$SCENARIO" "$@"
