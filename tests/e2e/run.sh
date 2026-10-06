#!/bin/bash
# End-to-end test: drives the real app through a real drawing session in
# its own hidden sway (never the desktop) and checks real outcomes — the
# files on disk, the pixels in them, a clean exit, no QML warnings.
#
#   tests/e2e/run.sh <path-to-omasketch>     (ctest passes the binary)
#
# Needs sway, wtype, grim, wl-paste, imagemagick, python3, gcc and
# wayland-client.h; exits 77 (ctest skips) when one is missing. Screenshots
# for debugging land in a /tmp folder named in the output. Exits non-zero
# when a check fails.
set -u
D=$(dirname "$(readlink -f "$0")")
BIN=$(readlink -f "${1:?usage: run.sh <omasketch binary>}")

missing=
for tool in sway wtype grim wl-paste magick python3 gcc; do
  command -v "$tool" > /dev/null || missing="$missing $tool"
done
echo '#include <wayland-client.h>' | gcc -E - > /dev/null 2>&1 \
  || missing="$missing wayland-client.h"
if [ -n "$missing" ]; then
  echo "SKIP: e2e needs${missing}; not found"
  exit 77
fi

eval "$("$D/sway.sh" start)"
trap '"$D/sway.sh" stop' EXIT
if [ -z "${OMA_WAYLAND:-}" ]; then
  echo "FAIL the hidden sway never came up (see /tmp/omasketch-e2e-sway.*/sway.log)"
  exit 1
fi

T=$(mktemp -d /tmp/omasketch-e2e-run.XXXXXX)
mkdir -p "$T/home" "$T/shots"
export OMA_SHOTS=$T/shots
gcc -O2 -o "$T/vptr" "$D/vptr.c" "$D/vp.c" -I"$D" -lwayland-client \
  || { echo "FAIL building the virtual pointer"; exit 1; }
export OMA_VPTR=$T/vptr

# Counts ink in a screenshot or saved PNG. Prints: width height black red
# non-white, sampled every third pixel. "centre" restricts to the middle
# 60% both ways (where a reopened drawing is placed).
cat > "$T/pngstats.py" <<'EOF'
import subprocess, sys
path, mode = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "")
zeros = "0 0 0 0 0"
try:
    w, h = map(int, subprocess.run(
        ["magick", "identify", "-format", "%w %h", path],
        capture_output=True, text=True, check=True).stdout.split())
    raw = subprocess.run(["magick", path, "-depth", "8", "rgb:-"],
                         capture_output=True, check=True).stdout
except Exception as e:
    print(f"pngstats: {e}", file=sys.stderr)
    print(zeros); sys.exit(0)
x0, x1 = (w * 2 // 10, w * 8 // 10) if mode == "centre" else (0, w)
y0, y1 = (h * 2 // 10, h * 8 // 10) if mode == "centre" else (0, h)
if mode == "toast":  # the toast's strip at the bottom centre of the window
    x0, x1, y0, y1 = w // 4, 3 * w // 4, h * 92 // 100, h * 99 // 100
black = red = ink = 0
row = w * 3
for y in range(y0, y1, 3):
    for x in range(x0, x1, 3):
        i = y * row + x * 3
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        if r < 100 and g < 100 and b < 100: black += 1
        if r > 150 and g < 100 and b < 100: red += 1
        if r < 245 or g < 245 or b < 245: ink += 1
print(w, h, black, red, ink)
EOF

fails=0
check() { # check <name> <command...>: prints PASS/FAIL with the name
  local name=$1; shift
  if "$@" > /dev/null; then
    echo "PASS $name"
  else
    echo "FAIL $name"
    fails=$((fails + 1))
  fi
}
stats() { python3 "$T/pngstats.py" "$@"; }

# --- session 1: draw, edit, save, save as, close -----------------------------
cat > "$T/session.steps" <<'EOF'
# the key hint on the blank page; `?` opens the keys card, Esc closes it
shot hint.png
type ?
wait 300
shot keys.png
tap Escape
wait 300
shot keys-closed.png
# two black strokes
tap d
drag 250 250 600 250
drag 250 600 600 600
# red arrow
tap 2
tap a
drag 900 300 900 550
# a text box (red: the ink is still 2)
tap t
abs 350 400
press
release
type i = 0, j = 4
wait 300
tap Escape
wait 200
# box-select everything, move it, grow it by the corner handle
tap v
drag 200 180 1000 700
wait 200
drag 700 400 900 450
wait 200
drag 1110 660 1200 760
wait 200
shot session1.png
# erase the bottom stroke, then undo that
tap e
drag 440 711 870 711
wait 300
shot erased.png
key -M ctrl -k z -m ctrl
wait 300
shot undone.png
# copy the whole drawing (the eraser cleared the selection)
key -M ctrl -k c -m ctrl
wait 300
clip copied.png
# the first save asks: Enter takes the default name; then save as auto.png
# through the path bar (Ctrl+U clears the line)
key -M ctrl -k s -m ctrl
wait 300
tap Return
wait 400
key -M ctrl -M shift -k s -m shift -m ctrl
wait 300
key -M ctrl -k u -m ctrl
type ~/Pictures/Dr
tap Tab
wait 200
type auto
tap Return
wait 500
# one more stroke, so closing has an unsaved change to autosave
tap d
drag 100 100 160 100
wait 300
close
EOF
OMA_APP_HOME=$T/home OMA_APP_LOG=app.log OMA_APP_ENV="OMASKETCH_TIMING=1" \
  "$D/drive.sh" "$BIN" "$T/session.steps"
code=$?
DRAWINGS=$T/home/Pictures/Drawings

check "the app window appeared and took focus" test "$code" != 124
check "the app exited cleanly on close" test "$code" = 0
check "first-frame time was logged" \
  grep -q 'omasketch: first frame [0-9]* ms' "$OMA_SHOTS/app.log"
echo "     first frame: $(grep -o 'first frame [0-9]* ms' "$OMA_SHOTS/app.log" || echo '?')"
check "no QML warnings on stderr" \
  bash -c "test -f '$OMA_SHOTS/app.log' && ! grep -qE 'qrc:|\.qml:' '$OMA_SHOTS/app.log'"

read -r _ _ _ _ HN < <(stats "$OMA_SHOTS/hint.png" centre)
read -r _ _ _ _ KN < <(stats "$OMA_SHOTS/keys.png" centre)
read -r _ _ _ _ CN < <(stats "$OMA_SHOTS/keys-closed.png" centre)
check "a blank launch shows the key hint" test "$HN" -gt 20
check "? opens the keys card" test "$KN" -gt $((HN * 5))
check "Esc closes the keys card" test "$CN" -le "$HN"

read -r W1 _ B1 R1 I1 < <(stats "$OMA_SHOTS/session1.png")
echo "     session1.png: ${W1} wide, black=$B1 red=$R1"
check "the strokes show in black" test "$B1" -gt 200
check "the arrow shows in red" test "$R1" -gt 100
read -r _ _ B2 _ _ < <(stats "$OMA_SHOTS/erased.png")
check "the eraser removed the bottom stroke" test $((B2 * 10)) -lt $((B1 * 9))
read -r _ _ B3 _ _ < <(stats "$OMA_SHOTS/undone.png")
check "undo brought the stroke back" test "$B3" -ge $((B2 + 100))

read -r CW CH CB _ _ < <(stats "$OMA_SHOTS/copied.png")
echo "     copied.png: ${CW}x${CH}, black=$CB"
check "Ctrl+C put the cropped drawing on the clipboard" \
  test "$CW" -gt 0 -a "$CW" -lt 1580 -a "$CB" -gt 100

saved=$(ls "$DRAWINGS"/[0-9][0-9][0-9][0-9]-*.png 2>/dev/null | wc -l)
check "Ctrl+S, Enter wrote one timestamped PNG" test "$saved" = 1
check "Ctrl+Shift+S wrote auto.png via Tab completion" test -f "$DRAWINGS/auto.png"

# The saved drawing: cropped to the ink plus a margin, with red and black.
read -r AW AH AB AR AN < <(stats "$DRAWINGS/auto.png")
echo "     auto.png: ${AW}x${AH}, black=$AB red=$AR non-white=$AN"
check "the saved PNG is cropped, not the whole page" test "$AW" -lt 1580 -a "$AH" -lt 980
check "the saved PNG is not blank" test "$AN" -gt 300
check "the saved PNG has red and black ink" test "$AB" -gt 100 -a "$AR" -gt 100

# The close-time autosave rewrote auto.png: the extra stroke widens the
# crop by a few hundred pixels (about 890 without it, 1319 with it).
check "closing autosaved the extra stroke to auto.png" test "$AW" -ge 1200 -a "$AW" -lt 1580

# --- session 2: reopen the saved PNG -----------------------------------------
printf 'wait 300\nshot reopened.png\nclose\n' > "$T/reopen.steps"
OMA_APP_HOME=$T/home OMA_APP_LOG=app2.log OMA_APP_ENV="OMASKETCH_TIMING=1" \
  "$D/drive.sh" "$BIN" "$T/reopen.steps" "$DRAWINGS/auto.png"
code2=$?
read -r _ _ _ _ RW < <(stats "$OMA_SHOTS/reopened.png" centre)
check "the reopened drawing is on the page" test "$RW" -gt 100
check "the reopen exited cleanly" test "$code2" = 0
check "reopen: no QML warnings on stderr" \
  bash -c "test -f '$OMA_SHOTS/app2.log' && ! grep -qE 'qrc:|\.qml:' '$OMA_SHOTS/app2.log'"
check "a clean close wrote no autosave duplicate" \
  test "$(ls "$DRAWINGS"/*.png | wc -l)" = 2

# --- session 3: a leading - argument is a flag, not a file --------------------
# --help used to open a window and toast "no image at …/--help". The window
# fills the hidden screen, so the toast's strip must stay blank.
printf 'wait 300\nshot flag.png\nclose\n' > "$T/flag.steps"
OMA_APP_HOME=$T/home OMA_APP_LOG=app3.log \
  "$D/drive.sh" "$BIN" "$T/flag.steps" --help
code3=$?
check "the flag-only launch exited cleanly" test "$code3" = 0
check "flag-only launch: no QML warnings on stderr" \
  bash -c "test -f '$OMA_SHOTS/app3.log' && ! grep -qE 'qrc:|\.qml:' '$OMA_SHOTS/app3.log'"
read -r _ _ _ _ FT < <(stats "$OMA_SHOTS/flag.png" toast)
echo "     flag.png toast strip: non-white=$FT"
check "a - argument never toasts a missing file" test "$FT" = 0

echo "e2e shots and the app's saved files: $T"
if [ "$fails" -eq 0 ]; then
  echo "e2e: PASS"
else
  echo "e2e: $fails FAILED"
fi
exit $((fails > 0))
