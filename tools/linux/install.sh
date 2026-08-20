#!/bin/sh
# AutoHotkey v2 Linux port - installer (CLI).
#
# Usage:
#   install.sh [options]
#
# Options:
#   --prefix DIR      install under DIR (default: /usr/local, or ~/.local
#                     when not root)
#   --bin DIR         directory for the ahk launcher (default: $prefix/bin)
#   --lib DIR         directory for the library/inc files (default:
#                     $prefix/share/autohotkey)
#   --doc DIR         directory for documentation (default:
#                     $prefix/share/doc/autohotkey)
#   --version VER     release to stamp into the launcher (e.g. 2.0.26-linux.14;
#                     default: from AHK_VERSION env or "unknown")
#   --uninstall       remove an existing installation (needs the same
#                     --prefix it was installed with)
#   --gnome-extension yes: also install the optional GNOME Shell extension
#                     (global hotkeys on GNOME Wayland) now, or -- with
#                     --uninstall -- remove it as well.  Default (auto):
#                     ask interactively on a detected GNOME session, and
#                     never touch the extension otherwise.
#   --yes             do not prompt
#   --help            show this help
#
# The installer copies the ahk_core interpreter, a small "ahk" wrapper
# script, the AutoHotkey v2 include files and this README.  It can be
# used both from a release tarball (run from the unpacked tree) and from
# a source checkout after `cmake --build build-core`.
set -u

PREFIX="${PREFIX:-}"
UNINSTALL=0
YES=0
GNOME_EXT=auto
AHK_VERSION="${AHK_VERSION:-}"
BIN_SUB=bin
LIB_SUB=share/autohotkey
DOC_SUB=share/doc/autohotkey

usage() { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --bin) BIN_SUB="$2"; shift 2 ;;
    --lib) LIB_SUB="$2"; shift 2 ;;
    --doc) DOC_SUB="$2"; shift 2 ;;
    --version) AHK_VERSION="$2"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    --gnome-extension) GNOME_EXT=yes; shift ;;
    --yes) YES=1; shift ;;
    --help|-h) usage 0 ;;
    *) echo "install.sh: unknown option: $1" >&2; usage 1 ;;
  esac
done

# Locate the unpacked tree: either the tarball layout (ahk_core beside the
# installer) or a source checkout (tools/linux/install.sh -> repo root).
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR="$SELF_DIR"
case "$REPO_DIR" in
  */tools/linux) REPO_DIR=$(CDPATH= cd -- "$REPO_DIR/../.." && pwd) ;;
esac
CORE="$SELF_DIR/ahk_core"
if [ ! -x "$CORE" ]; then
  # Tarball layout: autohotkey-linux/{ahk_core, tools/linux/install.sh}.
  CORE="$REPO_DIR/ahk_core"
fi
if [ ! -x "$CORE" ]; then
  # Source checkout layout: build-core/source/linux/core/ahk_core.
  CORE="$REPO_DIR/build-core/source/linux/core/ahk_core"
fi
if [ ! -x "$CORE" ]; then
  echo "install.sh: cannot find the ahk_core binary (run this from a release" >&2
  echo "            tarball or build first: cmake --build build-core)" >&2
  exit 1
fi

if [ -z "$PREFIX" ]; then
  if [ "$(id -u)" = 0 ]; then
    PREFIX=/usr/local
  else
    PREFIX="$HOME/.local"
  fi
fi

# --- optional GNOME Shell extension -----------------------------------
# The extension is a separate user/system-scoped component that never lives
# inside the install prefix; --gnome-extension makes the installer handle
# it (first install / removal), while the default only asks interactively
# on a live GNOME session and otherwise leaves it untouched.  Updates of
# the core never reach it -- that promise is independent of this option.
EXT_UUID="ahk-global-hotkeys@autohotkey.org"
EXT_SRC="$REPO_DIR/extension/$EXT_UUID"
EXT_USER_DIR="$HOME/.local/share/gnome-shell/extensions"
EXT_SYS_DIR="/usr/share/gnome-shell/extensions"

is_gnome_session() {
  case "${XDG_CURRENT_DESKTOP:-}" in
    *GNOME*|*gnome*) return 0 ;;
  esac
  if command -v pgrep >/dev/null 2>&1 && pgrep -x gnome-shell >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

install_extension() {
  [ -d "$EXT_SRC" ] || { echo "install.sh: extension source not found: $EXT_SRC" >&2; return 1; }
  if [ "$(id -u)" = 0 ]; then
    T="$EXT_SYS_DIR"
  else
    T="$EXT_USER_DIR"
  fi
  if [ -d "$T/$EXT_UUID" ]; then
    echo "GNOME extension already installed ($T/$EXT_UUID); kept as-is."
    return 0
  fi
  mkdir -p "$T" || return 1
  cp -r "$EXT_SRC" "$T/" || { echo "install.sh: cannot copy the extension to $T" >&2; return 1; }
  echo "GNOME extension installed: $T/$EXT_UUID"
  if command -v gnome-extensions >/dev/null 2>&1; then
    if gnome-extensions enable "$EXT_UUID" >/dev/null 2>&1; then
      echo "GNOME extension enabled."
    else
      echo "The extension is installed; enable it after logging back in:"
      echo "    gnome-extensions enable $EXT_UUID"
    fi
  else
    echo "gnome-extensions not available; enable it from the GNOME Extensions"
    echo "app or with: gnome-extensions enable $EXT_UUID"
  fi
  # A freshly-placed extension is only guaranteed to load at the next
  # GNOME Shell start (GNOME scans the extensions dir at startup).  Tell
  # the user explicitly -- the enable command alone registers the choice,
  # it does not hot-load a brand-new extension.
  if pgrep -x gnome-shell >/dev/null 2>&1; then
    echo "GNOME Shell is running now. Restart it so the new extension loads:"
    echo "    log out and back in, or press Alt+F2 and type 'r'"
  else
    echo "No GNOME Shell is running now; the extension loads at the next login."
  fi
  return 0
}

remove_extension() {
  for T in "$EXT_USER_DIR" "$EXT_SYS_DIR"; do
    if [ -d "$T/$EXT_UUID" ]; then
      if command -v gnome-extensions >/dev/null 2>&1; then
        gnome-extensions disable "$EXT_UUID" >/dev/null 2>&1 || true
      fi
      rm -rf "$T/$EXT_UUID"
      # Prune now-empty parent dirs (extensions/ and gnome-shell/); rmdir
      # refuses non-empty dirs, so other extensions are never touched.
      rmdir "$T" 2>/dev/null || true
      rmdir "$(dirname "$T")" 2>/dev/null || true
      echo "GNOME extension removed: $T/$EXT_UUID"
    fi
  done
}

BINDIR="$PREFIX/$BIN_SUB"
LIBDIR="$PREFIX/$LIB_SUB"
DOCDIR="$PREFIX/$DOC_SUB"

# Existing install?  The wrapper embeds the prefix; locate it.  The
# launcher line is AHK_PREFIX="<path>" (double quotes are sh syntax, not
# part of the value), so strip them here -- a literal OLD_PREFIX with
# quotes would make --uninstall remove nothing.
if [ -f "$BINDIR/ahk" ]; then
  OLD_PREFIX=$(sed -n 's/^AHK_PREFIX=//p' "$BINDIR/ahk" 2>/dev/null | head -1 | tr -d '"')
  [ -n "$OLD_PREFIX" ] || OLD_PREFIX="$PREFIX"
else
  OLD_PREFIX=
fi

if [ "$UNINSTALL" = 1 ]; then
  [ -n "$OLD_PREFIX" ] || { echo "install.sh: no installation found at $BINDIR" >&2; exit 1; }
  echo "Removing AutoHotkey v2 from $OLD_PREFIX ..."
  rm -f "$OLD_PREFIX/$BIN_SUB/ahk" "$OLD_PREFIX/$BIN_SUB/ahk_core" \
        "$OLD_PREFIX/$LIB_SUB/ahk_core" "$OLD_PREFIX/$LIB_SUB/ahk.ahk"
  rm -rf "$OLD_PREFIX/$DOC_SUB"
  rmdir "$OLD_PREFIX/$BIN_SUB" 2>/dev/null || true
  if [ "$GNOME_EXT" = yes ]; then
    remove_extension
  else
    echo "Note: the GNOME Shell extension (if installed) was left in place;"
    echo "      re-run with --gnome-extension to remove it too."
  fi
  echo "Done."
  exit 0
fi

echo "AutoHotkey v2.0.26 Linux port installer"
echo "  prefix : $PREFIX"
echo "  binary : $BINDIR/ahk"
echo "  lib    : $LIBDIR"
echo "  docs   : $DOCDIR"
if [ "$YES" != 1 ]; then
  printf "Continue? [y/N] "
  read -r ans || exit 1
  case "$ans" in y|Y|yes) ;; *) echo "Aborted."; exit 1 ;; esac
fi

mkdir -p "$BINDIR" "$LIBDIR" "$DOCDIR" || { echo "install.sh: cannot create directories (need root for $PREFIX?)" >&2; exit 1; }

# The interpreter binary.
install -m 0755 "$CORE" "$LIBDIR/ahk_core" || exit 1

# A tiny wrapper so scripts can run `ahk script.ahk`; rendered from the
# shared launcher template (--update/--uninstall/--check live in it).
# Stamp the release: --version wins, then AHK_VERSION, then a VERSION file
# shipped in release tarballs, else "unknown".
[ -n "$AHK_VERSION" ] || AHK_VERSION=$(cat "$SELF_DIR/VERSION" 2>/dev/null || true)
[ -n "$AHK_VERSION" ] || AHK_VERSION=$(cat "$REPO_DIR/tools/linux/VERSION" 2>/dev/null || true)
[ -n "$AHK_VERSION" ] || AHK_VERSION="unknown"
sed -e "s|@PREFIX@|$PREFIX|g" \
    -e "s|@LIB_SUB@|$LIB_SUB|g" \
    -e "s|@BIN_SUB@|$BIN_SUB|g" \
    -e "s|@DOC_SUB@|$DOC_SUB|g" \
    -e "s|@AHK_VERSION@|$AHK_VERSION|g" \
    "$REPO_DIR/tools/linux/ahk-launcher.in" > "$BINDIR/ahk"
chmod 0755 "$BINDIR/ahk"

# Optional: a second name matching the interpreter's argv[0] conventions.
ln -sf "ahk" "$BINDIR/ahk_core" 2>/dev/null || true

# Include files (Lib/ for #Include <...>).
if [ -d "$REPO_DIR/Lib" ]; then
  cp -r "$REPO_DIR/Lib" "$LIBDIR/" 2>/dev/null || true
fi

# Docs.
if [ -d "$REPO_DIR/docs-v2" ]; then
  cp -r "$REPO_DIR/docs-v2" "$DOCDIR/" 2>/dev/null || true
fi
[ -f "$REPO_DIR/README.md" ] && install -m 0644 "$REPO_DIR/README.md" "$DOCDIR/README.md"
[ -f "$REPO_DIR/LICENSE" ] && install -m 0644 "$REPO_DIR/LICENSE" "$DOCDIR/LICENSE"

echo
echo "Installed.  Try:"
echo "    $BINDIR/ahk --version"
echo "Add $BINDIR to your PATH if it is not already there."

# Optional GNOME Shell extension (first-install convenience).  The core
# install/update/uninstall never touches it; this is the one-time hook.
case "$GNOME_EXT" in
  yes)
    echo
    install_extension
    ;;
  auto)
    if is_gnome_session && [ "$YES" != 1 ]; then
      echo
      printf "Install the GNOME Shell extension for global hotkeys on GNOME Wayland? [y/N] "
      read -r ans
      case "$ans" in y|Y|yes) install_extension ;; *) echo "Skipped." ;; esac
    fi
    ;;
esac
