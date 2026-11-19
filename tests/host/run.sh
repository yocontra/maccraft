#!/bin/sh
# Host-side unit tests for the world and iso modules.
#
# Runs natively with the system C compiler (clang on macOS, gcc on
# ubuntu); no Retro68 toolchain and no Mac are needed.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../.." && pwd)

CC=${CC:-cc}
TMP=${TMPDIR:-/tmp}
work=$(mktemp -d "${TMP%/}/maccraft-host.XXXXXX")

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

CFLAGS="-std=c99 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer"
CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=all"
INCLUDES="-I$here/compat -I$repo/src -I$repo/src/game"

# shellcheck disable=SC2086
$CC $CFLAGS $INCLUDES -o "$work/test_world" \
    "$here/test_world.c" "$repo/src/game/world.c"

# shellcheck disable=SC2086
$CC $CFLAGS $INCLUDES -o "$work/test_iso" \
    "$here/test_iso.c" "$repo/src/game/iso.c"

if [ -f "$here/test_blocks.c" ]; then
    # shellcheck disable=SC2086
    $CC $CFLAGS $INCLUDES -o "$work/test_blocks" \
        "$here/test_blocks.c" "$repo/src/game/blocks.c"
fi

# shellcheck disable=SC2086
$CC $CFLAGS $INCLUDES -o "$work/test_light" \
    "$here/test_light.c" "$repo/src/game/light.c" "$repo/src/game/world.c"

"$work/test_world"
echo "test_world: ok"

"$work/test_iso"
echo "test_iso: ok"

if [ -f "$here/test_blocks.c" ]; then
    "$work/test_blocks"
    echo "test_blocks: ok"
fi

"$work/test_light"

if [ -f "$here/test_sky.c" ]; then
    # shellcheck disable=SC2086
    $CC $CFLAGS $INCLUDES -o "$work/test_sky" "$here/test_sky.c" "$repo/src/game/sky.c" \
        && { "$work/test_sky"; echo "test_sky: ok"; }
fi

if [ -f "$here/test_scene.c" ]; then
    # shellcheck disable=SC2086
    $CC $CFLAGS $INCLUDES -o "$work/test_scene" "$here/test_scene.c" \
        "$repo/src/game/scene.c" "$repo/src/game/world.c" \
        && "$work/test_scene"
fi
