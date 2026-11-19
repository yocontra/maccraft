#!/bin/sh
# Build the disposable MacCraft test VM.
#
#   vm/make_vm.sh            clone the boot volume, install dist/MacCraft.APPL
#                            in System Folder/Startup Items, render prefs
#   vm/make_vm.sh --detach   detach the clone if a previous run left it mounted
#
# The user's MacOS9.sparsebundle is only ever read: it is cloned with `cp -c`
# (APFS clonefile, instantaneous and space-free on the same volume) into
# tests/emulator/.work/.  Every write happens inside that clone or inside
# tests/emulator/.work/.
#
# Retro68 writes dist/<App>.APPL as a bare data fork and dist/%<App>.ad as an
# AppleDouble holding the resource fork and Finder info.  A Mac OS 9 volume
# needs both to launch the file, so the two are merged into macOS xattrs on a
# staged copy before `ditto` puts it on the HFS volume.
#
# Environment overrides:
#   MAC_ROM        path to the PowerPC "Mac OS ROM" file
#   MAC_BOOT_DISK  path to the Mac OS 9 boot volume (sparsebundle or disk image)
#   EMU_APP        path to the built .APPL (default dist/MacCraft.APPL)
#   EMU_WORK       work directory (default tests/emulator/.work)
#   PYTHON         python3 interpreter (default /opt/homebrew/bin/python3)

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
EMU_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
REPO=$(cd "$EMU_DIR/../.." && pwd)
WORK=${EMU_WORK:-"$EMU_DIR/.work"}
PYTHON=${PYTHON:-}
if [ -z "$PYTHON" ]; then
    if [ -x /opt/homebrew/bin/python3 ]; then
        PYTHON=/opt/homebrew/bin/python3
    else
        PYTHON=$(command -v python3 || true)
    fi
fi

VM_DIR="$WORK/Test.sheepvm"
CLONE="$WORK/MacOS9Test.sparsebundle"
MNT="$WORK/mnt"
SHARE="$WORK/share"
STAGE="$WORK/stage/MacCraft"

die() {
    echo "make_vm.sh: $*" >&2
    exit 2
}

# Refuse to operate anywhere but the harness scratch directory.
case "$WORK" in
    */tests/emulator/.work) ;;
    *) die "refusing to use work directory $WORK (must end in tests/emulator/.work)" ;;
esac

mkdir -p "$WORK"

detach_clone() {
    if [ -d "$MNT" ] && mount | grep -F " on $MNT " >/dev/null 2>&1; then
        diskutil unmount force "$MNT" >/dev/null 2>&1 \
            || hdiutil detach "$MNT" -force >/dev/null 2>&1 || true
    fi
    rmdir "$MNT" 2>/dev/null || true
}

if [ "${1:-}" = "--detach" ]; then
    detach_clone
    exit 0
fi

[ -n "$PYTHON" ] || die "python3 not found"
trap 'detach_clone' EXIT HUP INT TERM

# ---------------------------------------------------------------------------
# Locate the ROM, the boot volume and the application.
# ---------------------------------------------------------------------------

find_rom() {
    for candidate in \
        "${MAC_ROM:-}" \
        "$HOME/Library/Application Support/MacOS9/MacOS.sheepvm/Mac OS ROM" \
        "/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/Mac OS ROM"
    do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_disk() {
    prefs="$HOME/Library/Application Support/MacOS9/MacOS.sheepvm/prefs"
    configured=""
    if [ -f "$prefs" ]; then
        # `disk <path>` is the line SheepShaver itself uses.
        configured=$(sed -n 's/^[[:space:]]*disk[[:space:]]*//p' "$prefs" | head -n 1)
    fi
    for candidate in \
        "${MAC_BOOT_DISK:-}" \
        "$configured" \
        "/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle" \
        "$HOME/Library/Application Support/MacOS9/MacOS.sheepvm/MacOS9.sparsebundle"
    do
        if [ -n "$candidate" ] && [ -e "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_app() {
    if [ -n "${EMU_APP:-}" ]; then
        [ -e "$EMU_APP" ] || die "EMU_APP=$EMU_APP does not exist"
        printf '%s\n' "$EMU_APP"
        return 0
    fi
    for candidate in "$REPO/dist/MacCraft.APPL" "$REPO/dist/App.APPL"; do
        if [ -e "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    applies=""
    count=0
    for candidate in "$REPO"/dist/*.APPL; do
        [ -e "$candidate" ] || continue
        applies=$candidate
        count=$((count + 1))
    done
    if [ "$count" -eq 1 ] && [ -n "$applies" ]; then
        printf '%s\n' "$applies"
        return 0
    fi
    return 1
}

ROM=$(find_rom) || die "Mac OS ROM not found; set MAC_ROM=/path/to/Mac\\ OS\\ ROM"
DISK=$(find_disk) || die "Mac OS 9 boot volume not found; set MAC_BOOT_DISK=/path/to/MacOS9.sparsebundle"
APP=$(find_app) || die "no built application in $REPO/dist; run make build APP_NAME=MacCraft first"
# Retro68 puts the data fork in <App>.APPL and the resource fork + Finder info
# in %<App>.ad.  Derive the companion from the .APPL basename.
APP_BASE=${APP##*/}
APP_BASE=${APP_BASE%.APPL}
AD="${APP%/*}/%${APP_BASE}.ad"
[ -f "$AD" ] || die "AppleDouble $AD is missing; run make build (it copies build/%<App>.ad to dist/)"

[ -d "$DISK" ] || die "boot volume $DISK is not a directory-based image (sparsebundle expected)"
DISK_SIZE=$(du -sm "$DISK" 2>/dev/null | awk '{print $1}')
if [ -z "$DISK_SIZE" ] || [ "$DISK_SIZE" -lt 100 ]; then
    die "boot volume $DISK looks too small (${DISK_SIZE:-0} MB); refusing to clone"
fi

echo "make_vm: rom   = $ROM"
echo "make_vm: disk  = $DISK (${DISK_SIZE} MB on disk)"
echo "make_vm: app   = $APP"
[ -f "$AD" ] && echo "make_vm: ad    = $AD"

# ---------------------------------------------------------------------------
# Clone the boot volume.  `cp -c` uses clonefile(2): the copy shares blocks
# with the original, costs no extra space, and is instantaneous.
# ---------------------------------------------------------------------------

detach_clone
rm -rf "$CLONE" "$VM_DIR" "$STAGE"
mkdir -p "$SHARE"
# A desktop alias on this volume points at "Unix:SendToMacOS9"; providing it
# stops the Finder raising "File ... wasn't found" and blocking startup.
mkdir -p "$SHARE/SendToMacOS9" "$WORK/stage"

if ! cp -cR "$DISK" "$CLONE" 2>/dev/null; then
    echo "make_vm: cp -c is unavailable here; falling back to a full copy" >&2
    rm -rf "$CLONE"
    cp -R "$DISK" "$CLONE"
fi

# A stale lock from an unclean shutdown makes DiskImages refuse to attach.
rm -f "$CLONE/lock"

# DiskImages rewrites a version-1 sparsebundle's Info.plist to
# `bundle-backingstore-version 2` (and adds a uuid) as soon as the bundle is
# attached read-write. SheepShaver only understands version 1 and refuses the
# volume with "sparsebundle: Bad version", so keep the original bytes and put
# them back after the clone is detached.
cp "$CLONE/Info.plist" "$WORK/Info.plist.v1"
[ -f "$CLONE/Info.bckup" ] && cp "$CLONE/Info.bckup" "$WORK/Info.bckup.v1"

# ---------------------------------------------------------------------------
# Stage the real file: data fork + resource fork + Finder info.
# ---------------------------------------------------------------------------

"$PYTHON" "$SCRIPT_DIR/stage_app.py" "$APP" "$AD" "$STAGE" \
    || die "could not stage the application"

mkdir -p "$MNT"
# On current macOS `hdiutil attach` prompts "There may be a problem with this
# disk image" for this sparsebundle (it has no checksum) and cancels when
# stdin is not a terminal.  diskutil's attach is the non-interactive form.
if ! diskutil image attach --mountOptions nobrowse --mountPoint "$MNT" "$CLONE" >/dev/null 2>&1; then
    hdiutil attach -noverify -nobrowse -readwrite -noautoopen -mountpoint "$MNT" "$CLONE" >/dev/null 2>&1 \
        || die "could not attach the cloned volume"
fi

if [ ! -d "$MNT/System Folder" ]; then
    die "$CLONE did not mount a Mac OS 9 volume (no 'System Folder' at $MNT)"
fi

STARTUP="$MNT/System Folder/Startup Items"
mkdir -p "$STARTUP"
rm -rf "$STARTUP/MacCraft"
# ditto preserves the resource fork and the APPL/'Mcft' Finder metadata.
if ! ditto "$STAGE" "$STARTUP/MacCraft"; then
    die "ditto failed to install $STAGE into the clone"
fi

# ditto normally carries both forks onto HFS; if the resource fork did not
# survive, write the xattrs straight onto the HFS file (same bytes).
if ! /usr/bin/xattr "$STARTUP/MacCraft" 2>/dev/null | grep -q 'com.apple.ResourceFork'; then
    "$PYTHON" - "$STAGE" "$STARTUP/MacCraft" <<'PY'
import ctypes
import subprocess
import sys

src, dest = sys.argv[1:3]
libc = ctypes.CDLL(None, use_errno=True)
libc.setxattr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                          ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
libc.setxattr.restype = ctypes.c_int


def read_xattr(path, name):
    raw = subprocess.run(["/usr/bin/xattr", "-px", name, path],
                         check=False, capture_output=True).stdout
    if not raw.strip():
        return None
    return bytes.fromhex(raw.decode().replace("\n", ""))


def set_xattr(path, name, value):
    buf = ctypes.create_string_buffer(value, len(value))
    if libc.setxattr(path.encode(), name.encode(), buf, len(value), 0, 0) != 0:
        errno = ctypes.get_errno()
        raise OSError(errno, os.strerror(errno), path)


for attr in ("com.apple.ResourceFork", "com.apple.FinderInfo"):
    value = read_xattr(src, attr)
    if value:
        set_xattr(dest, attr, value)
PY
fi

/usr/bin/xattr "$STARTUP/MacCraft" 2>/dev/null | grep -q 'com.apple.ResourceFork' \
    || die "resource fork did not survive the copy to $STARTUP/MacCraft"

TYPE=$(/usr/bin/GetFileInfo -t "$STARTUP/MacCraft" 2>/dev/null | tr -d '"' || true)
CREATOR=$(/usr/bin/GetFileInfo -c "$STARTUP/MacCraft" 2>/dev/null | tr -d '"' || true)
[ "$TYPE" = "APPL" ] || die "installed file has type '$TYPE', expected APPL"
[ "$CREATOR" = "MCft" ] || die "installed file has creator '$CREATOR', expected MCft"
echo "make_vm: installed MacCraft (APPL/MCft) into 'System Folder/Startup Items'"

# In-guest self-test: the game looks for this file at startup and, if it is
# there, drives its own handlers, writes a log beside it and quits. No input
# events, so the harness never touches the real mouse or keyboard.
# Either the environment or a marker file asks for it, so a scenario runner that
# builds the VM in a subprocess with its own environment can still request it.
SELFTEST_NAME=${APP_NAME:-$(basename "$APP" .APPL)}
if [ "${MAC_EMU_SELFTEST:-0}" = "1" ] || [ -f "$WORK/request-selftest" ]; then
    PREFSDIR="$MNT/System Folder/Preferences"
    mkdir -p "$PREFSDIR"
    : > "$PREFSDIR/$SELFTEST_NAME selftest"
    [ -f "$PREFSDIR/$SELFTEST_NAME selftest" ] || die "could not create the selftest trigger"
    echo "make_vm: selftest trigger in System Folder:Preferences"
else
    rm -f "$MNT/System Folder/Preferences/$SELFTEST_NAME selftest" 2>/dev/null || true
fi

trap - EXIT HUP INT TERM
diskutil unmount force "$MNT" >/dev/null 2>&1 \
    || hdiutil detach "$MNT" -force >/dev/null 2>&1 \
    || die "could not detach $MNT"

# Undo the DiskImages format upgrade so SheepShaver can read the clone.
cp "$WORK/Info.plist.v1" "$CLONE/Info.plist"
[ -f "$WORK/Info.bckup.v1" ] && cp "$WORK/Info.bckup.v1" "$CLONE/Info.bckup"
rm -f "$CLONE/lock"
rmdir "$MNT" 2>/dev/null || true

# ---------------------------------------------------------------------------
# Render the .sheepvm bundle.
# ---------------------------------------------------------------------------

mkdir -p "$VM_DIR"
ln -s "$ROM" "$VM_DIR/Mac OS ROM"
ln -s "$CLONE" "$VM_DIR/MacOS9Test.sparsebundle"

# awk substitutes the paths without sed delimiter escaping; & is doubled so it
# is not treated as "the matched text".
awk -v disk="$CLONE" -v share="$SHARE" '
    function esc(s) { gsub(/&/, "\\\\&", s); return s }
    {
        line = $0
        gsub(/@DISK@/, esc(disk), line)
        gsub(/@SHARE@/, esc(share), line)
        print line
    }
' "$SCRIPT_DIR/prefs.tmpl" > "$WORK/prefs"
mv "$WORK/prefs" "$VM_DIR/prefs"

echo "make_vm: vm    = $VM_DIR"
echo "make_vm: share = $SHARE"
