#!/usr/bin/env python3
"""SheepShaver driver for the MacCraft emulator tests.

Two modes:

  driver.py --scenario tests/emulator/scenarios/01-launch.sh [--record] [--keep]
      Build the throwaway VM (tests/emulator/vm/make_vm.sh), boot SheepShaver,
      wait for its window, run the shell scenario, then tear everything down.

  driver.py <command> [args...]
      Thin client used by tests/emulator/lib.sh from inside a scenario.  It
      talks to the running emulator described by tests/emulator/.work/session.json.

Exit codes (both modes):
  0  success / assertion held
  1  assertion failed (the observed value is printed)
  2  environment missing (ROM, boot disk, app, tesseract, TCC permission, ...)

Capture uses Quartz.CGWindowListCreateImage on the SheepShaver window and input
uses Quartz.CGEventPost.  Coordinates in scenarios are guest-screen coordinates
(1024x768) and are translated through the window's position, so they do not
depend on the host display.

Nothing here writes outside tests/emulator/.work (plus tests/emulator/baselines
when --record is used, which is the point of --record).
"""

import atexit
import json
import os
import signal
import subprocess
import sys
import time

EMU_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(EMU_DIR))
WORK = os.path.join(EMU_DIR, ".work")
SESSION_PATH = os.path.join(WORK, "session.json")
SHOTS_DIR = os.path.join(WORK, "shots")
OCR_DIR = os.path.join(WORK, "ocr")
BASELINES_DIR = os.path.join(EMU_DIR, "baselines")
PREFS_TMPL = os.path.join(EMU_DIR, "vm", "prefs.tmpl")
MAKE_VM = os.path.join(EMU_DIR, "vm", "make_vm.sh")
COORDS_PATH = os.path.join(EMU_DIR, "scenarios", "coords.json")

GUEST_W = 1024
GUEST_H = 768

TITLE = "Mac OS 9 Test"

DEFAULT_SHEEPSHAVER = (
    "/Applications/Mac OS 9.app/Contents/Resources/Files/"
    "SheepShaver.app/Contents/MacOS/SheepShaver"
)
DEFAULT_ROM = os.path.expanduser("~/Library/Application Support/MacOS9/MacOS.sheepvm/Mac OS ROM")
DEFAULT_DISK = (
    "/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle"
)

# Pixels whose grayscale value differs by more than this count as changed.
PIXEL_DELTA = 16


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------

class EnvError(Exception):
    """Something the operator has to fix (exit code 2)."""


class CheckFailed(Exception):
    """An assertion did not hold (exit code 1)."""


# ---------------------------------------------------------------------------
# Lazy imports: --help and command parsing should not require pyobjc.
# ---------------------------------------------------------------------------

_Quartz = None
_Image = None
_ImageChops = None


def quartz():
    global _Quartz
    if _Quartz is None:
        try:
            import Quartz
        except ImportError:
            raise EnvError(
                "pyobjc-framework-Quartz is not installed for %s.\n"
                "  python3 -m pip install -r %s\n"
                "  (pyobjc 10.3.1 needs Python <= 3.12; see tests/emulator/README.md)"
                % (sys.executable, os.path.join(EMU_DIR, "requirements.txt"))
            )
        _Quartz = Quartz
    return _Quartz


def pillow():
    global _Image, _ImageChops
    if _Image is None:
        try:
            from PIL import Image, ImageChops
        except ImportError:
            raise EnvError(
                "Pillow is not installed for %s.\n"
                "  python3 -m pip install -r %s"
                % (sys.executable, os.path.join(EMU_DIR, "requirements.txt"))
            )
        _Image = Image
        _ImageChops = ImageChops
    return _Image, _ImageChops


def tesseract_binary():
    exe = os.environ.get("TESSERACT")
    if exe:
        if not os.path.isfile(exe) or not os.access(exe, os.X_OK):
            raise EnvError("TESSERACT=%s is not executable" % exe)
        return exe
    for cand in ("/opt/homebrew/bin/tesseract", "/usr/local/bin/tesseract"):
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    from shutil import which

    found = which("tesseract")
    if found:
        return found
    raise EnvError(
        "tesseract not found; install it with `brew install tesseract` or set TESSERACT=<path>"
    )


# ---------------------------------------------------------------------------
# Files and regions
# ---------------------------------------------------------------------------

def ensure_work_dirs():
    for d in (WORK, SHOTS_DIR, OCR_DIR):
        os.makedirs(d, exist_ok=True)


def load_coords():
    try:
        with open(COORDS_PATH, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except OSError as exc:
        raise EnvError("cannot read %s: %s" % (COORDS_PATH, exc))
    except ValueError as exc:
        raise EnvError("%s is not valid JSON: %s" % (COORDS_PATH, exc))
    regions = data.get("regions") or {}
    points = data.get("points") or {}
    if not regions:
        raise EnvError("%s has no \"regions\" object" % COORDS_PATH)
    return regions, points


def region_rect(name_or_rect, coords=None):
    regions, _ = coords or load_coords()
    if isinstance(name_or_rect, str):
        if name_or_rect not in regions:
            raise EnvError("unknown region %r (known: %s)" % (name_or_rect, ", ".join(sorted(regions))))
        rect = regions[name_or_rect]
    else:
        rect = name_or_rect
    if not isinstance(rect, (list, tuple)) or len(rect) != 4:
        raise EnvError("region %r must be [x, y, width, height]" % (name_or_rect,))
    x, y, w, h = (int(v) for v in rect)
    if w <= 0 or h <= 0:
        raise EnvError("region %r has non-positive size" % (name_or_rect,))
    if x < 0 or y < 0 or x + w > GUEST_W or y + h > GUEST_H:
        raise EnvError(
            "region %r (%d,%d %dx%d) is outside the %dx%d guest screen"
            % (name_or_rect, x, y, w, h, GUEST_W, GUEST_H)
        )
    return (x, y, w, h)


# ---------------------------------------------------------------------------
# Window discovery and capture
# ---------------------------------------------------------------------------

def find_window():
    """Return {'id', 'x', 'y', 'w', 'h', 'title'} for the SheepShaver window."""
    Q = quartz()
    options = Q.kCGWindowListOptionOnScreenOnly | Q.kCGWindowListExcludeDesktopElements
    windows = Q.CGWindowListCopyWindowInfo(options, Q.kCGNullWindowID) or []
    best = None
    fallback = None
    for win in windows:
        try:
            wid = int(win[Q.kCGWindowNumber])
            owner = win.get(Q.kCGWindowOwnerName) or ""
            name = win.get(Q.kCGWindowName) or ""
            bounds = win.get(Q.kCGWindowBounds) or {}
        except (KeyError, TypeError):
            continue
        try:
            x = int(round(bounds["X"]))
            y = int(round(bounds["Y"]))
            w = int(round(bounds["Width"]))
            h = int(round(bounds["Height"]))
        except (KeyError, TypeError, ValueError):
            continue
        if w < 320 or h < 240:
            continue
        info = {"id": wid, "x": x, "y": y, "w": w, "h": h, "title": name, "owner": owner}
        if name == TITLE:
            best = info
            break
        if fallback is None and "SheepShaver" in owner:
            fallback = info
    return best or fallback


def wait_for_window(timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        win = find_window()
        if win is not None:
            return win
        time.sleep(0.5)
    return None


def _capture_quartz(wid):
    Q = quartz()
    if not hasattr(Q, "CGWindowListCreateImage"):
        return None
    image = Q.CGWindowListCreateImage(
        Q.CGRectMake(0, 0, 0, 0),
        Q.kCGWindowListOptionIncludingWindow,
        wid,
        Q.kCGWindowImageBoundsIgnoreFraming | Q.kCGWindowImageNominalResolution,
    )
    if image is None:
        raise EnvError(
            "CGWindowListCreateImage returned nil.\n"
            "Grant Screen Recording (and Accessibility) to the terminal or agent host in\n"
            "System Settings > Privacy & Security, then re-run."
        )
    width = int(Q.CGImageGetWidth(image))
    height = int(Q.CGImageGetHeight(image))
    stride = int(Q.CGImageGetBytesPerRow(image))
    provider = Q.CGImageGetDataProvider(image)
    data = Q.CGDataProviderCopyData(provider)
    raw = bytes(data)
    if len(raw) < stride * height:
        raise EnvError("short window capture (%d bytes, expected %d)" % (len(raw), stride * height))
    Image, _ = pillow()
    img = Image.frombuffer("RGBA", (width, height), raw, "raw", "BGRA", stride, 1)
    return img.copy()


def _capture_screencapture(wid):
    path = os.path.join(WORK, "capture.png")
    proc = subprocess.run(
        ["/usr/sbin/screencapture", "-x", "-o", "-l%d" % wid, path],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0 or not os.path.exists(path):
        raise EnvError(
            "screencapture failed for window %d: %s\n"
            "Grant Screen Recording to the terminal or agent host in System Settings >\n"
            "Privacy & Security, then re-run." % (wid, (proc.stderr or proc.stdout).strip())
        )
    Image, _ = pillow()
    img = Image.open(path)
    img.load()
    return img


def capture_window():
    """Capture the emulator window and return (guest_image, win, offset_y)."""
    win = find_window()
    if win is None:
        raise EnvError("SheepShaver window not found; is the emulator still running?")

    img = _capture_quartz(win["id"])
    if img is None:
        img = _capture_screencapture(win["id"])

    Image, _ = pillow()
    if img.mode not in ("RGB", "RGBA"):
        img = img.convert("RGBA")

    # Normalise to the 1024x768 guest framebuffer.  The capture includes the
    # host title bar, so the guest frame is the bottom 768 rows.
    if img.width != GUEST_W:
        if img.width < GUEST_W:
            raise EnvError("window capture is %dx%d, narrower than the %dx%d guest screen"
                           % (img.width, img.height, GUEST_W, GUEST_H))
        img = img.resize((GUEST_W, int(round(img.height * GUEST_W / img.width))), Image.NEAREST)
    if img.height < GUEST_H:
        raise EnvError("window capture is %dx%d, shorter than the %dx%d guest screen"
                       % (img.width, img.height, GUEST_W, GUEST_H))

    offset_y = img.height - GUEST_H
    guest = img.crop((0, offset_y, GUEST_W, GUEST_H + offset_y))
    return guest, win, offset_y


def window_origin(win):
    """Return the host point corresponding to guest (0,0) and the guest scale."""
    # CGWindowListCreateImage with NominalResolution returns 1 pixel per point,
    # so the captured height equals the window's point height.  Guest pixels
    # map 1:1 to points unless the user resized the window.
    scale_x = win["w"] / float(GUEST_W)
    scale_y = scale_x
    title_pts = win["h"] - GUEST_H * scale_y
    if title_pts < 0:
        title_pts = 0.0
    return win["x"], win["y"] + title_pts, scale_x


def guest_to_host(win, gx, gy):
    # Recompute from a capture so the title bar height is measured, not assumed.
    _, win2, _ = capture_window()
    origin_x, origin_y, scale = window_origin(win2)
    return origin_x + gx * scale, origin_y + gy * scale


# ---------------------------------------------------------------------------
# Input
# ---------------------------------------------------------------------------

KEYCODES = {
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9,
    "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17, "1": 18, "2": 19,
    "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25, "7": 26, "-": 27, "8": 28,
    "0": 29, "]": 30, "o": 31, "u": 32, "[": 33, "i": 34, "p": 35, "return": 36,
    "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42, ",": 43, "/": 44, "n": 45,
    "m": 46, ".": 47, "tab": 48, "space": 49, "`": 50, "delete": 51, "backspace": 51,
    "esc": 53, "escape": 53, "left": 123, "right": 124, "down": 125, "up": 126,
    "home": 115, "end": 119, "pageup": 116, "pagedown": 121, "forwarddelete": 117,
}

SHIFTED = {
    ":": ";", "_": "-", '"': "'", "?": "/", "!": "1", "@": "2", "#": "3", "$": "4",
    "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0", "+": "=", "{": "[",
    "}": "]", "|": "\\", "<": ",", ">": ".", "~": "`",
}

MODIFIERS = {
    "cmd": "kCGEventFlagMaskCommand",
    "command": "kCGEventFlagMaskCommand",
    "shift": "kCGEventFlagMaskShift",
    "opt": "kCGEventFlagMaskAlternate",
    "option": "kCGEventFlagMaskAlternate",
    "alt": "kCGEventFlagMaskAlternate",
    "ctrl": "kCGEventFlagMaskControl",
    "control": "kCGEventFlagMaskControl",
}


def _event_source():
    Q = quartz()
    return Q.CGEventSourceCreate(Q.kCGEventSourceStateHIDSystemState)


def _flags(names):
    Q = quartz()
    value = 0
    for name in names:
        attr = MODIFIERS.get(name.lower())
        if attr is None:
            raise EnvError("unknown modifier %r in key combination" % name)
        value |= getattr(Q, attr)
    return value


def post_key_code(code, flags=0):
    Q = quartz()
    source = _event_source()
    down = Q.CGEventCreateKeyboardEvent(source, code, True)
    up = Q.CGEventCreateKeyboardEvent(source, code, False)
    if flags:
        Q.CGEventSetFlags(down, flags)
        Q.CGEventSetFlags(up, flags)
    Q.CGEventPost(Q.kCGHIDEventTap, down)
    time.sleep(0.03)
    Q.CGEventPost(Q.kCGHIDEventTap, up)


def key(combo):
    """key("cmd+,"), key("return"), key("right"), key("shift+t")."""
    parts = [p for p in combo.split("+") if p != ""]
    if not parts:
        raise EnvError("empty key combination")
    # "cmd++" is not used; the remainder is the key name.
    name = parts[-1]
    flags = _flags(parts[:-1])
    key_name = name.lower()
    if key_name not in KEYCODES:
        raise EnvError("unknown key %r (known names: %s)" % (name, ", ".join(sorted(KEYCODES))))
    post_key_code(KEYCODES[key_name], flags)


def type_text(text):
    """Type printable ASCII using the US layout."""
    for ch in text:
        flags = 0
        base = ch
        if "A" <= ch <= "Z":
            base = ch.lower()
            flags = _flags(["shift"])
        elif ch in SHIFTED:
            base = SHIFTED[ch]
            flags = _flags(["shift"])
        elif ch == " ":
            base = "space"
        elif not ("!" <= ch <= "~" or ch == " "):
            raise EnvError("cannot type character %r (printable ASCII only)" % ch)
        if base not in KEYCODES:
            raise EnvError("no keycode for character %r" % ch)
        post_key_code(KEYCODES[base], flags)
        time.sleep(0.02)


def _focus(win):
    """Click the host title bar so the emulator window is frontmost."""
    Q = quartz()
    title_pts = win["h"] - GUEST_H
    if title_pts < 6:
        return
    x = win["x"] + win["w"] // 2
    y = win["y"] + title_pts // 2
    source = _event_source()
    for etype in (Q.kCGEventMouseMoved, Q.kCGEventLeftMouseDown, Q.kCGEventLeftMouseUp):
        ev = Q.CGEventCreateMouseEvent(source, etype, (x, y), Q.kCGMouseButtonLeft)
        Q.CGEventPost(Q.kCGHIDEventTap, ev)
        time.sleep(0.03)


def click_at(win, gx, gy, shift=False):
    Q = quartz()
    hx, hy = guest_to_host(win, gx, gy)
    flags = _flags(["shift"]) if shift else 0
    source = _event_source()
    move = Q.CGEventCreateMouseEvent(source, Q.kCGEventMouseMoved, (hx, hy), Q.kCGMouseButtonLeft)
    Q.CGEventPost(Q.kCGHIDEventTap, move)
    time.sleep(0.05)
    for etype in (Q.kCGEventLeftMouseDown, Q.kCGEventLeftMouseUp):
        ev = Q.CGEventCreateMouseEvent(source, etype, (hx, hy), Q.kCGMouseButtonLeft)
        if flags:
            Q.CGEventSetFlags(ev, flags)
        Q.CGEventPost(Q.kCGHIDEventTap, ev)
        time.sleep(0.05)


def dblclick_at(win, gx, gy):
    Q = quartz()
    hx, hy = guest_to_host(win, gx, gy)
    source = _event_source()
    Q.CGEventPost(
        Q.kCGHIDEventTap,
        Q.CGEventCreateMouseEvent(source, Q.kCGEventMouseMoved, (hx, hy), Q.kCGMouseButtonLeft),
    )
    time.sleep(0.05)
    for count in (1, 2):
        for etype in (Q.kCGEventLeftMouseDown, Q.kCGEventLeftMouseUp):
            ev = Q.CGEventCreateMouseEvent(source, etype, (hx, hy), Q.kCGMouseButtonLeft)
            Q.CGEventSetIntegerValueField(ev, Q.kCGMouseEventClickState, count)
            Q.CGEventPost(Q.kCGHIDEventTap, ev)
            time.sleep(0.03)


# ---------------------------------------------------------------------------
# Images: shots, diffs, OCR
# ---------------------------------------------------------------------------

def load_shot(path):
    Image, _ = pillow()
    img = Image.open(path)
    img.load()
    if img.mode not in ("RGB", "RGBA"):
        img = img.convert("RGBA")
    if img.width != GUEST_W or img.height != GUEST_H:
        raise EnvError(
            "baseline %s is %dx%d, expected the %dx%d guest frame"
            % (path, img.width, img.height, GUEST_W, GUEST_H)
        )
    return img


def resolve_baseline(arg, for_write=False):
    """Resolve a baseline/shot argument to a path.

    Order: absolute path, tests/emulator/<arg>, .work/shots/<arg>,
    tests/emulator/baselines/<arg>, cwd/<arg>.
    """
    if os.path.isabs(arg):
        return arg
    candidates = [
        os.path.join(EMU_DIR, arg),
        os.path.join(SHOTS_DIR, arg),
        os.path.join(BASELINES_DIR, arg),
        os.path.join(os.getcwd(), arg),
    ]
    for cand in candidates:
        if os.path.exists(cand):
            return cand
    if for_write:
        # Recording always writes into tests/emulator/, preserving the
        # directory given by the scenario (normally baselines/NAME.png).
        target = arg if arg.startswith("baselines/") else os.path.join("baselines", arg)
        return os.path.join(EMU_DIR, target)
    return candidates[0]


def diff_fraction(img_a, img_b):
    _, ImageChops = pillow()
    if img_a.size != img_b.size:
        raise EnvError("image size mismatch: %s vs %s" % (img_a.size, img_b.size))
    diff = ImageChops.difference(img_a.convert("L"), img_b.convert("L"))
    hist = diff.histogram()
    changed = sum(hist[PIXEL_DELTA + 1:])
    total = img_a.size[0] * img_a.size[1]
    return changed / float(total)


def crop_region(img, rect, label):
    x, y, w, h = region_rect(rect)
    return img.crop((x, y, x + w, y + h))


def ocr_image(img):
    exe = tesseract_binary()
    ensure_work_dirs()
    path = os.path.join(OCR_DIR, "ocr-%d.png" % int(time.time() * 1000))
    # Classic Mac bitmap fonts are small; nearest-neighbour 2x helps tesseract.
    Image, _ = pillow()
    big = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    big.save(path)
    try:
        proc = subprocess.run(
            [exe, path, "stdout", "--psm", "6", "-l", "eng"],
            capture_output=True,
            text=True,
        )
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    if proc.returncode != 0:
        raise EnvError("tesseract failed: %s" % (proc.stderr or proc.stdout).strip())
    return proc.stdout.strip()


# ---------------------------------------------------------------------------
# Session handling (command mode)
# ---------------------------------------------------------------------------

def load_session():
    try:
        with open(SESSION_PATH, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except OSError:
        raise EnvError(
            "no active emulator session (%s missing).\n"
            "Scenarios must be run through tests/emulator/run.sh." % SESSION_PATH
        )
    except ValueError as exc:
        raise EnvError("corrupt session file %s: %s" % (SESSION_PATH, exc))


def save_session(session):
    ensure_work_dirs()
    tmp = SESSION_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(session, fh, indent=2, sort_keys=True)
    os.replace(tmp, SESSION_PATH)


def session_window(session):
    win = find_window()
    if win is None:
        raise EnvError("SheepShaver window disappeared (emulator exited?)")
    return win


def resolve_target(args, usage):
    """Resolve `NAME`, `POINT`, `REGION` or `X Y` to guest coordinates."""
    if len(args) == 1:
        regions, points = load_coords()
        if args[0] in points:
            return tuple(int(v) for v in points[args[0]])
        if args[0] in regions:
            x, y, w, h = region_rect(args[0])
            return x + w // 2, y + h // 2
        raise EnvError("unknown point or region %r" % args[0])
    if len(args) == 2:
        return int(args[0]), int(args[1])
    raise EnvError("usage: %s" % usage)


def cmd_click(args, session):
    shift = "--shift" in args
    args = [a for a in args if not a.startswith("--")]
    win = session_window(session)
    gx, gy = resolve_target(args, "click <region|point|X Y> [--shift]")
    click_at(win, gx, gy, shift=shift)
    print("click %d,%d%s" % (gx, gy, " shift" if shift else ""))


def cmd_dblclick(args, session):
    win = session_window(session)
    gx, gy = resolve_target(args, "dblclick <region|point|X Y>")
    dblclick_at(win, gx, gy)
    print("dblclick %d,%d" % (gx, gy))


def cmd_move(args, session):
    """Move the guest cursor without clicking (keeps it out of diff regions)."""
    Q = quartz()
    win = session_window(session)
    gx, gy = resolve_target(args, "move <region|point|X Y>")
    hx, hy = guest_to_host(win, gx, gy)
    source = _event_source()
    Q.CGEventPost(
        Q.kCGHIDEventTap,
        Q.CGEventCreateMouseEvent(source, Q.kCGEventMouseMoved, (hx, hy), Q.kCGMouseButtonLeft),
    )
    time.sleep(0.2)
    print("move %d,%d" % (gx, gy))


def cmd_key(args, session):
    if len(args) != 1:
        raise EnvError("usage: key <combo>")
    win = session_window(session)
    # Keyboard events go to the frontmost window; make sure that is the guest.
    _focus(win)
    key(args[0])
    print("key %s" % args[0])


def cmd_type(args, session):
    if len(args) != 1:
        raise EnvError("usage: type <text>")
    win = session_window(session)
    _focus(win)
    type_text(args[0])
    print("type %r" % args[0])


def cmd_shot(args, session):
    if len(args) != 1:
        raise EnvError("usage: shot <name>")
    ensure_work_dirs()
    img, _, _ = capture_window()
    safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in args[0])
    path = os.path.join(SHOTS_DIR, safe + ".png")
    img.save(path)
    print(path)


def cmd_ocr(args, session):
    if len(args) not in (1, 2):
        raise EnvError("usage: ocr <region> [pattern]")
    img, _, _ = capture_window()
    text = ocr_image(crop_region(img, args[0], "ocr"))
    print(text)
    if len(args) == 2:
        if args[1].lower() not in text.lower():
            raise CheckFailed("OCR did not contain %r; saw: %r" % (args[1], text))
        print("contains: %s" % args[1])


def cmd_wait_ocr(args, session):
    if len(args) != 3:
        raise EnvError("usage: wait_ocr <region> <pattern> <timeout-seconds>")
    region, pattern, timeout = args[0], args[1], float(args[2])
    deadline = time.time() + timeout
    seen = ""
    while time.time() < deadline:
        img, _, _ = capture_window()
        seen = ocr_image(crop_region(img, region, "wait_ocr"))
        if pattern.lower() in seen.lower():
            print("wait_ocr %s: found %r" % (region, pattern))
            return
        time.sleep(2)
    raise CheckFailed(
        "OCR did not contain %r within %.0fs; last text: %r" % (pattern, timeout, seen)
    )


def cmd_wait_no_ocr(args, session):
    if len(args) != 3:
        raise EnvError("usage: wait_no_ocr <region> <pattern> <timeout-seconds>")
    region, pattern, timeout = args[0], args[1], float(args[2])
    deadline = time.time() + timeout
    seen = ""
    while time.time() < deadline:
        img, _, _ = capture_window()
        seen = ocr_image(crop_region(img, region, "wait_no_ocr"))
        if pattern.lower() not in seen.lower():
            print("wait_no_ocr %s: %r is gone" % (region, pattern))
            return
        time.sleep(2)
    raise CheckFailed(
        "OCR still contained %r after %.0fs; last text: %r" % (pattern, timeout, seen)
    )


def _compare(session, region, baseline_arg, threshold, kind):
    image, _, _ = capture_window()
    region_img = crop_region(image, region, kind)
    path = resolve_baseline(baseline_arg, for_write=bool(session.get("record")))
    if session.get("record"):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        image.save(path)
        print("recorded %s (%s)" % (os.path.relpath(path, EMU_DIR), region))
        return
    if not os.path.exists(path):
        raise EnvError(
            "baseline %s does not exist; record it first with\n"
            "  tests/emulator/run.sh %s --record"
            % (path, os.path.basename(session.get("scenario", "SCENARIO")))
        )
    baseline = load_shot(path)
    base_region = crop_region(baseline, region, kind)
    fraction = diff_fraction(region_img, base_region)
    if kind == "changed":
        ok = fraction >= threshold
        relation = ">="
    else:
        ok = fraction <= threshold
        relation = "<="
    print("%s %s: %.4f (%s %.4f)" % (kind, region, fraction, relation, threshold))
    if not ok:
        raise CheckFailed(
            "%s %s: changed fraction %.4f is not %s %.4f (baseline %s)"
            % (kind, region, fraction, relation, threshold, os.path.relpath(path, EMU_DIR))
        )


def cmd_changed(args, session):
    if len(args) != 3:
        raise EnvError("usage: changed <region> <baseline> <min-fraction>")
    _compare(session, args[0], args[1], float(args[2]), "changed")


def cmd_stable(args, session):
    if len(args) != 3:
        raise EnvError("usage: stable <region> <baseline> <max-fraction>")
    _compare(session, args[0], args[1], float(args[2]), "stable")


def cmd_wait_stable(args, session):
    if len(args) not in (2, 3):
        raise EnvError("usage: wait_stable <region> <timeout-seconds> [max-fraction]")
    region, timeout = args[0], float(args[1])
    max_fraction = float(args[2]) if len(args) == 3 else 0.002
    deadline = time.time() + timeout
    previous = None
    fraction = 1.0
    while time.time() < deadline:
        image, _, _ = capture_window()
        current = crop_region(image, region, "wait_stable")
        if previous is not None:
            fraction = diff_fraction(current, previous)
            if fraction <= max_fraction:
                print("wait_stable %s: %.4f (<= %.4f)" % (region, fraction, max_fraction))
                return
        previous = current
        time.sleep(0.4)
    raise CheckFailed(
        "region %s did not become stable within %.0fs (last frame-to-frame change %.4f > %.4f)"
        % (region, timeout, fraction, max_fraction)
    )


def cmd_sleep(args, session):
    if len(args) != 1:
        raise EnvError("usage: sleep <seconds>")
    time.sleep(float(args[0]))


def cmd_focus(args, session):
    win = session_window(session)
    _focus(win)
    print("focus ok")


def cmd_window(args, session):
    win = session_window(session)
    print(json.dumps(win, sort_keys=True))


COMMANDS = {
    "click": cmd_click,
    "dblclick": cmd_dblclick,
    "move": cmd_move,
    "key": cmd_key,
    "type": cmd_type,
    "text": cmd_type,
    "shot": cmd_shot,
    "ocr": cmd_ocr,
    "wait_ocr": cmd_wait_ocr,
    "wait_no_ocr": cmd_wait_no_ocr,
    "changed": cmd_changed,
    "stable": cmd_stable,
    "wait_stable": cmd_wait_stable,
    "sleep": cmd_sleep,
    "focus": cmd_focus,
    "window": cmd_window,
}


def run_command(command, args):
    try:
        session = load_session()
        COMMANDS[command](args, session)
    except CheckFailed as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        return 1
    except EnvError as exc:
        print("ENVIRONMENT: %s" % exc, file=sys.stderr)
        return 2
    return 0


# ---------------------------------------------------------------------------
# VM lifecycle
# ---------------------------------------------------------------------------

def find_app():
    env_app = os.environ.get("EMU_APP")
    if env_app:
        if not os.path.exists(env_app):
            raise EnvError("EMU_APP=%s does not exist" % env_app)
        return env_app
    dist = os.path.join(REPO, "dist")
    for name in ("MacCraft.APPL", "App.APPL"):
        cand = os.path.join(dist, name)
        if os.path.exists(cand):
            return cand
    if os.path.isdir(dist):
        applies = sorted(
            os.path.join(dist, n) for n in os.listdir(dist) if n.endswith(".APPL")
        )
        if len(applies) == 1:
            return applies[0]
    raise EnvError(
        "dist/MacCraft.APPL not found - run make build APP_NAME=MacCraft first\n"
        "  (tests/emulator/run.sh never builds; it only tests a built app)"
    )


def check_environment():
    if sys.platform != "darwin":
        raise EnvError("the emulator harness only runs on macOS")
    for tool in ("/usr/bin/hdiutil", "/usr/bin/ditto"):
        if not os.path.exists(tool):
            raise EnvError("%s not found" % tool)
    sheep = os.environ.get("SHEEPSHAVER", DEFAULT_SHEEPSHAVER)
    if not os.path.exists(sheep):
        raise EnvError(
            "SheepShaver not found at %s\n"
            "Install `Mac OS 9.app` or set SHEEPSHAVER=<binary>." % sheep
        )
    rom = os.environ.get("MAC_ROM", DEFAULT_ROM)
    if not os.path.exists(rom):
        raise EnvError("Mac OS ROM not found at %s; set MAC_ROM=<path>." % rom)
    if not os.path.exists(MAKE_VM):
        raise EnvError("%s is missing" % MAKE_VM)
    if not os.path.exists(PREFS_TMPL):
        raise EnvError("%s is missing" % PREFS_TMPL)
    quartz()  # fail early with the pip instructions
    pillow()
    tesseract_binary()
    return {
        "sheepshaver": sheep,
        "rom": rom,
        "app": find_app(),
    }


def run_make_vm(assets, extra=()):
    env = os.environ.copy()
    env.setdefault("MAC_ROM", assets["rom"])
    env["EMU_APP"] = assets["app"]
    env["PYTHON"] = sys.executable
    proc = subprocess.run(
        ["/bin/sh", MAKE_VM] + list(extra),
        cwd=EMU_DIR,
        env=env,
        capture_output=True,
        text=True,
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        if proc.stderr:
            sys.stderr.write(proc.stderr)
        raise EnvError("vm/make_vm.sh failed (exit %d)" % proc.returncode)


def kill_stray_emulator(vm_dir):
    """Kill a SheepShaver left over from an aborted run of *this* VM only."""
    try:
        proc = subprocess.run(
            ["/usr/bin/pgrep", "-f", vm_dir], capture_output=True, text=True
        )
    except OSError:
        return
    for line in proc.stdout.split():
        if line.isdigit() and int(line) != os.getpid():
            try:
                os.kill(int(line), signal.SIGTERM)
            except OSError:
                pass


def launch_emulator(assets, vm_dir):
    sheep = assets["sheepshaver"]
    log_path = os.path.join(WORK, "sheepshaver.log")
    prefs = os.path.join(vm_dir, "prefs")

    kill_stray_emulator(vm_dir)

    for args, form in (
        ([sheep, vm_dir], "bundle"),
        ([sheep, "--config", prefs], "config"),
    ):
        log = open(log_path, "ab")
        log.write(("\n=== launch form %s: %s ===\n" % (form, " ".join(args))).encode())
        log.flush()
        proc = subprocess.Popen(
            args,
            cwd=WORK,
            stdout=log,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        win = wait_for_window(timeout=25.0)
        if win is not None:
            with open(os.path.join(WORK, "launch-form"), "w", encoding="utf-8") as fh:
                fh.write(form + "\n")
            print("SheepShaver launched (form %s, window %d)" % (form, win["id"]))
            return proc, win, log
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except OSError:
                pass
        log.close()
    raise EnvError(
        "SheepShaver did not create a window; see %s\n"
        "Check that the ROM and the cloned boot disk are valid." % log_path
    )


def stop_emulator(proc):
    if proc is None:
        return
    try:
        if proc.poll() is None:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                proc.wait(timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        pass


def detach_work_mount():
    if not os.path.exists(MAKE_VM):
        return
    subprocess.run(["/bin/sh", MAKE_VM, "--detach"], cwd=EMU_DIR, capture_output=True)


def cleanup(session, keep):
    stop_emulator(session.get("proc"))
    detach_work_mount()
    try:
        os.unlink(SESSION_PATH)
    except OSError:
        pass
    if not keep:
        for path in (
            os.path.join(WORK, "Test.sheepvm"),
            os.path.join(WORK, "MacOS9Test.sparsebundle"),
            os.path.join(WORK, "mnt"),
        ):
            if os.path.isdir(path) and not os.path.islink(path):
                import shutil

                shutil.rmtree(path, ignore_errors=True)
            elif os.path.exists(path):
                try:
                    os.unlink(path)
                except OSError:
                    pass


def run_scenario(args):
    ensure_work_dirs()
    assets = check_environment()
    run_make_vm(assets)

    vm_dir = os.path.join(WORK, "Test.sheepvm")
    if not os.path.isdir(vm_dir):
        raise EnvError("%s was not created by vm/make_vm.sh" % vm_dir)

    session = {
        "record": bool(args.record),
        "keep": bool(args.keep),
        "scenario": os.path.abspath(args.scenario),
        "proc": None,
    }
    # Create the session before boot so progress is visible to `driver.py window`.
    save_session({k: v for k, v in session.items() if k != "proc"})

    cleaned = {"done": False}

    def do_cleanup():
        if cleaned["done"]:
            return
        cleaned["done"] = True
        cleanup(session, args.keep)
        if args.keep:
            print("kept VM under %s" % WORK)

    atexit.register(do_cleanup)

    def on_signal(signum, _frame):
        raise SystemExit(128 + signum)

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        try:
            signal.signal(sig, on_signal)
        except (ValueError, OSError):
            pass

    proc = None
    rc = 1
    try:
        proc, win, _log = launch_emulator(assets, vm_dir)
        session["proc"] = proc
        session["window_id"] = win["id"]
        save_session({k: v for k, v in session.items() if k != "proc"})

        # Boot happens after the window appears; scenarios wait on OCR.
        _focus(win)

        env = os.environ.copy()
        env["EMU_DRIVER"] = os.path.abspath(__file__)
        env["EMU_PYTHON"] = sys.executable
        env["EMU_ROOT"] = EMU_DIR
        env["EMU_SESSION"] = SESSION_PATH
        env["EMU_RECORD"] = "1" if args.record else "0"

        rc = subprocess.call(["/bin/sh", "-eu", os.path.abspath(args.scenario)], env=env)
    finally:
        do_cleanup()
    return rc


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def usage():
    print(
        "usage:\n"
        "  driver.py --scenario <scenario.sh> [--record] [--keep]\n"
        "  driver.py <%s> [args...]\n" % "|".join(sorted(COMMANDS)),
        file=sys.stderr,
    )


def main(argv):
    if len(argv) >= 2 and argv[1] in COMMANDS:
        return run_command(argv[1], argv[2:])

    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        usage()
        return 2

    scenario = None
    record = False
    keep = False
    driver_choice = "sheepshaver"
    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == "--scenario":
            i += 1
            if i >= len(argv):
                usage()
                return 2
            scenario = argv[i]
        elif arg.startswith("--scenario="):
            scenario = arg.split("=", 1)[1]
        elif arg == "--record":
            record = True
        elif arg == "--keep":
            keep = True
        elif arg == "--driver":
            i += 1
            if i >= len(argv):
                usage()
                return 2
            driver_choice = argv[i]
        elif arg.startswith("--driver="):
            driver_choice = arg.split("=", 1)[1]
        elif arg in ("-h", "--help"):
            usage()
            return 0
        else:
            print("unknown argument: %s" % arg, file=sys.stderr)
            usage()
            return 2
        i += 1

    if scenario is None:
        usage()
        return 2
    if not os.path.isfile(scenario):
        print("no such scenario: %s" % scenario, file=sys.stderr)
        return 2
    if driver_choice != "sheepshaver":
        print(
            "ENVIRONMENT: the %r driver is not available on this machine.\n"
            "Basilisk II needs a 68k ROM (MAC_ROM) and a 7.x-8.x boot disk\n"
            "(MAC_BOOT_DISK); no such assets exist here. Use the default\n"
            "SheepShaver driver." % driver_choice,
            file=sys.stderr,
        )
        return 2

    class Args:
        pass

    args = Args()
    args.scenario = scenario
    args.record = record
    args.keep = keep

    try:
        return run_scenario(args)
    except CheckFailed as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        return 1
    except EnvError as exc:
        print("ENVIRONMENT: %s" % exc, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv))
