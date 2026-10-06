# omasketch

A simple, fast, minimal scratchpad for sketching and notes on Wayland.
Keyboard-driven: one key per tool, and `?` shows them all.

![omasketch: draw, type, arrows, the keys card, select, erase, save](docs/demo.gif)

## Install

On Arch or Omarchy, in a terminal (it asks for your password once):

    git clone https://github.com/stefali1-dev/omasketch
    cd omasketch/aur && makepkg -si

This builds the tagged release (a few seconds) and installs it as a normal
pacman package: `pacman -R omasketch` removes it. To update, run
`git pull && makepkg -si` in the same folder.

To open it with Super+D, add this line to `~/.config/hypr/bindings.lua`:

    o.bind("SUPER + D", "Sketch", { launch = "omasketch" })

## Build

    cmake -B build
    cmake --build build
    cmake --install build --prefix ~/.local

Installs the `omasketch` binary, `omasketch.desktop` and its icon.

## Test

    ctest --test-dir build           # logic and scene tests, offscreen
    ctest --test-dir build -L e2e    # a real drawing session in a hidden sway

The e2e test (`tests/e2e/run.sh`) drives the real app inside its own hidden
sway — never the desktop you are on — and checks the outcomes: the saved
files, the pixels in them, a clean exit, no QML warnings. It skips itself
when sway, wtype, grim or imagemagick are missing. Run it by hand with
`tests/e2e/run.sh build/omasketch`; screenshots and logs land in the /tmp
folder it names.

## Keys

| Key | Does |
|---|---|
| `d` `t` `v` `e` `a` | Draw, text, select, eraser, arrow |
| `?` | All the keys (or the `?` in the corner) |
| `1` `2` `3` | Black, red, blue |
| Space + drag, scroll | Pan |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+A | Select all |
| Ctrl+C / Ctrl+V | Copy the selection (or the whole drawing) as a PNG / paste an image |
| Ctrl+S | Save: the first time asks in the path bar (Enter takes `~/Pictures/Drawings/<timestamp>.png`), then overwrites that file |
| Ctrl+Shift+S | Save as, via the path bar (Tab completes, `~` works) |
| Ctrl+O | Open a PNG, via the path bar |
| Ctrl+N | Fresh page |
| Ctrl+= / Ctrl+- / Ctrl+0 | Zoom in / out / back to the drawing |
| Esc | Stop typing (back to select) · deselect → select tool |
| Del / Backspace | Delete the selection |

Closing with unsaved changes quietly auto-saves to the current target
(`~/Pictures/Drawings` if never saved). Saves crop to what is drawn, plus a
margin. Files are plain PNG.

The author's extra Hyprland bindings (Super+Shift+S / Super+O) are not part of this
repo; they live in the desktop dotfiles.
