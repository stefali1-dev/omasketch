#!/bin/bash
# A hidden sway for the e2e test: headless backend, its own WAYLAND_DISPLAY,
# no window on the real desktop. Everything driven from run.sh dies with it.
#   eval "$(tests/e2e/sway.sh start)"   then, when done:
#   tests/e2e/sway.sh stop
case "$1" in
start)
  D=$(dirname "$(readlink -f "$0")")
  S=$(mktemp -d /tmp/omasketch-e2e-sway.XXXXXX)
  WLR_BACKENDS=headless WLR_RENDERER=gles2 WLR_LIBINPUT_NO_DEVICES=1 WAYLAND_DISPLAY= \
    setsid sway -d -c "$D/sway.conf" > "$S/sway.log" 2>&1 < /dev/null &
  PID=$!
  SOCK=/run/user/$(id -u)/sway-ipc.$(id -u).$PID.sock
  for i in $(seq 100); do
    grep -q 'Running compositor on wayland display' "$S/sway.log" 2>/dev/null && [ -S "$SOCK" ] && break
    sleep 0.1
  done
  WD=$(grep -o "Running compositor on wayland display '[^']*" "$S/sway.log" | sed "s/.*'//")
  echo "export OMA_SWAY_PID=$PID SWAYSOCK=$SOCK OMA_WAYLAND=$WD OMA_SWAY_DIR=$S"
  ;;
stop)
  kill "$OMA_SWAY_PID" 2>/dev/null
  rm -rf "$OMA_SWAY_DIR"
  ;;
esac
