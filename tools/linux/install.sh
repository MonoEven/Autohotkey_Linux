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
#   --version VER     release to stamp into the launcher (e.g. 2.0.26-linux.12;
#                     default: from AHK_VERSION env or "unknown")
#   --uninstall       remove an existing installation (needs the same
#                     --prefix it was installed with)
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

BINDIR="$PREFIX/$BIN_SUB"
LIBDIR="$PREFIX/$LIB_SUB"
DOCDIR="$PREFIX/$DOC_SUB"

# Existing install?  The wrapper embeds the prefix; locate it.
if [ -f "$BINDIR/ahk" ]; then
  OLD_PREFIX=$(sed -n 's/^AHK_PREFIX=//p' "$BINDIR/ahk" 2>/dev/null | head -1)
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
