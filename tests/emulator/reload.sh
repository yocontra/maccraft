#!/bin/sh
#
# reload.sh -- put a new build into the guest and relaunch the application.
#
# The emulator stays up. Rebooting Mac OS 9 for every change costs a minute and
# makes the machine unusable while it happens, so instead:
#
#   1. the build is staged into one of two slots in the shared folder;
#   2. a `launch <slot>` command is dropped where the running app polls;
#   3. the running app opens that build, quits itself, and the new one runs.
#
# The two slots alternate so the file currently being executed is never the one
# being written. Nothing on the boot volume is touched.
#
#   tests/emulator/reload.sh [shot-name]
#
# Requires a guest that is already running with a build that has the command
# channel (v1.5.3 and later). Boot it once with vm/make_vm.sh + driver start.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK=${EMU_WORK:-"$HERE/.work"}
SHARE="$WORK/share"
PYTHON=${PYTHON:-/opt/homebrew/bin/python3}
APP_NAME=${APP_NAME:-MacCraft}
SHOT=${1:-reload}
BUILD="$ROOT/build"

[ -d "$SHARE" ] || {
    echo "reload: $SHARE is missing; boot the emulator once first" >&2
    exit 2
}
[ -f "$BUILD/$APP_NAME.APPL" ] || {
    echo "reload: no build; run make build first" >&2
    exit 2
}

# Build under the shared compile lock.
flock -w 600 /tmp/retro68-build.lock \
    docker exec retro68dev bash -c "cd /projects/maccraft && cmake --build build -j8" \
    2>&1 | grep -E "error" && exit 1

# Alternate slots.
A="$SHARE/$APP_NAME-1"
B="$SHARE/$APP_NAME-2"
if [ ! -f "$A" ]; then
    TARGET=$A
elif [ ! -f "$B" ]; then
    TARGET=$B
elif [ "$A" -ot "$B" ]; then
    rm -f "$A"
    TARGET=$A
else
    rm -f "$B"
    TARGET=$B
fi

"$PYTHON" "$HERE/vm/stage_app.py" "$BUILD/$APP_NAME.APPL" "$BUILD/%$APP_NAME.ad" "$TARGET" 2>&1 |
    sed 's/^make_vm:/reload:/'

# Tell the next build to open the game window rather than the splash.
: > "$SHARE/$APP_NAME play"

printf 'launch %s' "$(basename "$TARGET")" > "$SHARE/$APP_NAME cmd"

echo "reload: asked the guest to run $(basename "$TARGET")"

if [ -x "$HERE/.venv/bin/python3" ]; then
    sleep 2
    "$HERE/.venv/bin/python3" "$HERE/driver.py" shot "$SHOT" >/dev/null 2>&1 || true
    [ -f "$WORK/shots/$SHOT.png" ] && echo "reload: $WORK/shots/$SHOT.png"
fi
