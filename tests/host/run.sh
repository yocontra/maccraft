#!/bin/sh
# Host-side unit tests for the game modules.
#
# Runs natively with the system C compiler (clang on macOS, gcc on ubuntu); no
# Retro68 toolchain and no Mac are needed.

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

run_test() {
    name=$1
    shift

    # shellcheck disable=SC2086
    $CC $CFLAGS $INCLUDES -o "$work/$name" "$here/$name.c" "$@"
    "$work/$name"
    echo "$name: ok"
}

run_test test_world "$repo/src/game/world.c"
run_test test_iso "$repo/src/game/iso.c"
run_test test_blocks "$repo/src/game/blocks.c"
run_test test_light "$repo/src/game/light.c" "$repo/src/game/world.c"
run_test test_sky "$repo/src/game/sky.c"
run_test test_scene "$repo/src/game/scene.c" "$repo/src/game/world.c"

# The texture pipeline: a 64-entry shade-ramp palette and the isometric face
# baker. Complete and tested, but not wired into the renderer yet -- see
# docs/textures.md for the QuickDraw finding that blocks it.
run_test test_art "$repo/src/game/art.c"
run_test test_bake "$repo/src/game/bake.c" "$repo/src/game/art.c"
