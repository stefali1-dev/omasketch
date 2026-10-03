#!/bin/bash
# Plays a steps file to omasketch inside the hidden sway, then waits for the
# app to exit (a `close` step ends the session) and exits with its code.
# Usage: drive.sh <omasketch binary> <steps file> [app args...]
# Needs the env from `eval "$(sway.sh start)"`, plus:
#   OMA_APP_HOME  the app's HOME (a temp folder; saves land there)
#   OMA_VPTR      path to the built virtual pointer (vptr.c)
#   OMA_SHOTS     folder for screenshot steps
#   OMA_APP_LOG   app log name in $OMA_SHOTS (default app.log)
#   OMA_APP_ENV   extra env for the app, e.g. OMASKETCH_TIMING=1
# Steps, one per line (pointer coords are logical px in the 1280x800 window):
#   abs X Y | press | release   mouse move / left button
#   drag X1 Y1 X2 Y2            interpolated press-drag-release: a real hand
#                              moves in steps, and events that arrive in one
#                              batch collapse, so erasing needs the pacing
#   tap <key>                   press and release a named key (Escape, d, 2)
#   key <wtype args>            raw wtype syntax, e.g. -M ctrl -k s -m ctrl
#   type <text>                 types the rest of the line
#   wait <ms>                   pause
#   shot <name.png>             screenshot of the whole hidden screen
#   clip <name.png>             the clipboard's PNG, saved in $OMA_SHOTS
#   close                       ask the window to close, like a WM would
set -u
D=$(dirname "$(readlink -f "$0")")
: "${OMA_WAYLAND:?eval \"\$(tests/e2e/sway.sh start)\" first}"
: "${OMA_VPTR:?built by run.sh: gcc -O2 -o vptr vptr.c vp.c -lwayland-client}"
: "${OMA_APP_HOME:?OMA_APP_HOME must point at a temp HOME for the app}"
: "${OMA_SHOTS:?OMA_SHOTS must be a screenshot folder}"
BIN=$1; STEPS=$2; shift 2
export WAYLAND_DISPLAY=$OMA_WAYLAND

F=$OMA_SWAY_DIR/ptr.fifo; rm -f "$F"; mkfifo "$F"
"$OMA_VPTR" 1280 800 < "$F" & VP=$!
exec 7>"$F"
wtype -s 600000 x >/dev/null 2>&1 & KB=$!   # keeps a virtual keyboard plugged in
ok=0
for i in $(seq 100); do
  swaymsg -t get_seats 2>/dev/null | grep -q '"capabilities": 3' && ok=1 && break
  sleep 0.05
done
[ $ok = 1 ] || { echo "e2e: seat never got pointer and keyboard" >&2; exit 125; }

LOG=$OMA_SHOTS/${OMA_APP_LOG:-app.log}
env QT_QPA_PLATFORM=wayland QT_FORCE_STDERR_LOGGING=1 HOME="$OMA_APP_HOME" \
    ${OMA_APP_ENV:-} "$BIN" "$@" > "$LOG" 2>&1 & APP=$!

# Wait until the window exists and has keyboard focus.
ok=0
for i in $(seq 300); do
  if swaymsg -t get_tree 2>/dev/null | python3 -c 'import json,sys
def walk(n):
    if n.get("app_id")=="omasketch" and n.get("focused"): sys.exit(0)
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): walk(c)
walk(json.load(sys.stdin)); sys.exit(1)'; then ok=1; break; fi
  kill -0 $APP 2>/dev/null || break # died early; the log says why
  sleep 0.05
done
if [ $ok != 1 ]; then
  echo "e2e: app window never got focus (log: $LOG)" >&2
  kill $APP 2>/dev/null
  kill $KB $VP 2>/dev/null; exec 7>&-
  exit 124
fi
sleep 0.5

while read -r cmd rest; do
  case "$cmd" in
    abs|press|release|scroll) echo "$cmd $rest" >&7 ;;
    drag)
      set -- $rest
      echo "abs $1 $2" >&7; sleep 0.02
      echo "press" >&7; sleep 0.02
      n=$(awk "BEGIN{print int(sqrt(($3-$1)^2 + ($4-$2)^2) / 15)}")
      for i in $(seq 1 "$n"); do
        echo "abs $(awk "BEGIN{printf \"%.0f\", $1 + ($3-$1) * $i / $n}") \
$(awk "BEGIN{printf \"%.0f\", $2 + ($4-$2) * $i / $n}")" >&7
        sleep 0.01
      done
      echo "abs $3 $4" >&7; sleep 0.03
      echo "release" >&7
      ;;
    tap) wtype -k "$rest" ;;
    key) wtype $rest ;;
    type) wtype "$rest" ;;
    wait) sleep "$(awk "BEGIN{print $rest/1000}")" ;;
    shot) sleep 0.2; grim -o HEADLESS-1 "$OMA_SHOTS/$rest" ;;
    clip) timeout 2 wl-paste -t image/png > "$OMA_SHOTS/$rest" ;;
    close) swaymsg -q '[app_id="omasketch"] kill' ;;
  esac
done < "$STEPS"

# The session's last step usually closed the window; allow 15 s, then kill.
for i in $(seq 150); do kill -0 $APP 2>/dev/null || break; sleep 0.1; done
if kill -0 $APP 2>/dev/null; then
  echo "e2e: app did not exit; killing it" >&2
  kill $APP; wait $APP; CODE=$?
else
  wait $APP; CODE=$?
fi
kill $KB $VP 2>/dev/null; exec 7>&-
exit $CODE
