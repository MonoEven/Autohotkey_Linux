#!/bin/sh
# AutoHotkey v2 Linux port - GUI installer.
#
# A small graphical wizard (zenity or yad) that installs the port into the
# user's prefix (default: ~/.local, or /usr/local when run as root).  It
# mirrors the role of the official Windows setup GUI: choose a prefix,
# confirm, install, report done.  Falls back to the CLI installer when no
# dialog tool is available.
set -u

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# --- pick a dialog tool -----------------------------------------------
DIALOG=
for d in zenity yad; do
  if command -v "$d" >/dev/null 2>&1; then
    DIALOG="$d"
    break
  fi
done

if [ -z "$DIALOG" ]; then
  echo "install-gui.sh: no zenity or yad found; falling back to install.sh" >&2
  exec "$SELF_DIR/install.sh" "$@"
fi

if [ "$(id -u)" = 0 ]; then
  DEF_PREFIX=/usr/local
else
  DEF_PREFIX="$HOME/.local"
fi

msg() { # title, text
  "$DIALOG" --title "$1" --info --text "$2" --width 480 >/dev/null 2>&1
}
ask() { # title, text, default -> answer on stdout
  "$DIALOG" --title "$1" --entry --text "$2" --entry-text "$3" --width 520 2>/dev/null
}
confirm() { # title, text
  "$DIALOG" --title "$1" --question --text "$2" --width 520 >/dev/null 2>&1
}

if ! confirm "AutoHotkey v2 Linux port" \
     "This will install the AutoHotkey v2.0.26 Linux port\n(preferred prefix: $DEF_PREFIX).\n\nContinue?"; then
  exit 1
fi

PREFIX=$(ask "Installation prefix" \
  "Installation prefix (bin/ahk, share/autohotkey/... are placed under it):" \
  "$DEF_PREFIX")
[ -n "$PREFIX" ] || PREFIX="$DEF_PREFIX"

if ! confirm "Confirm installation" \
     "Installing into:\n  $PREFIX\n\nbin : $PREFIX/bin/ahk\nlib : $PREFIX/share/autohotkey\ndocs: $PREFIX/share/doc/autohotkey\n\nProceed?"; then
  exit 1
fi

# Optional GNOME Shell extension (zero-confirmation global hotkeys on
# GNOME Wayland).  It is a separate user/system-scoped component: the core
# install and --yes path never touch it, so this is the one-time hook.
EXT_ARGS=
if confirm "GNOME Shell extension" \
     "Also install the optional GNOME Shell extension\nzero-confirmation global hotkeys on GNOME Wayland?\n\nIt is placed under ~/.local/share/gnome-shell/extensions\n(or /usr/share/gnome-shell/extensions as root) and enabled.\nLog out and back in (or restart GNOME Shell) afterwards.\n\nInstall the extension?"; then
  EXT_ARGS="--gnome-extension"
fi

if "$SELF_DIR/install.sh" --prefix "$PREFIX" --yes $EXT_ARGS; then
  if [ -n "$EXT_ARGS" ]; then
    msg "AutoHotkey v2 Linux port" \
        "Installation complete.\n\nThe GNOME Shell extension was installed and enabled.\nRestart GNOME now (log out/in or press Alt+F2 and type 'r')\nso the new extension loads.\n\nTry: $PREFIX/bin/ahk --version"
  else
    msg "AutoHotkey v2 Linux port" \
        "Installation complete.\n\nTry: $PREFIX/bin/ahk --version\n\n(Add $PREFIX/bin to your PATH if needed.)"
  fi
else
  msg "AutoHotkey v2 Linux port" "Installation failed (see the terminal for details)."
  exit 1
fi
