#!/bin/sh
# pack-finalize.sh -- regenerate CKSUMS.txt (covering ALL artifacts in
# dist/, including AppImage + RPM produced by the sibling scripts) and
# re-sign it.  Run AFTER pack.sh + pack-appimage.sh + pack-rpm.sh.
#
# Usage: pack-finalize.sh [version]
#   (version is read from dist/* names if not given)
set -u

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$REPO_DIR" || exit 1

VER="${1:-}"
if [ -z "$VER" ]; then
  VER=$(ls dist/autohotkey-linux-*-amd64.tar.gz 2>/dev/null \
        | sed -n 's/.*linux-\([0-9.]*-linux\.[0-9]*\)-amd64.tar.gz/\1/p' | head -1)
fi
if [ -z "$VER" ]; then
  echo "pack-finalize.sh: no version (pass it or build first)" >&2
  exit 1
fi

CKSUMS=dist/CKSUMS.txt
{
  echo "AutoHotkey v2 Linux port release v$VER (built $(date -u +%Y-%m-%dT%H:%MZ))"
  echo "SHA-256 (one per line: '<hash>  <filename>'):"
  # Every release artifact that exists in dist/ (tar.gz, deb, AppImage, rpm).
  for f in dist/autohotkey-linux-$VER-*; do
    [ -f "$f" ] || continue
    case "$f" in
      *.tar.gz|*.deb|*.AppImage|*.rpm) ;;
      *) continue ;;
    esac
    name=$(basename "$f")
    printf '  %s  %s\n' "$(sha256sum "$f" | awk '{print $1}')" "$name"
  done
  echo
  echo "These hashes are computed from the files as packaged.  Verify a"
  echo "downloaded artifact with:  sha256sum -c <(grep '<filename>' CKSUMS.txt)"
  echo "CKSUMS.txt.sig is an OpenPGP detached signature (ASC) of this file."
  echo "Verify with:  gpg --verify CKSUMS.txt.sig CKSUMS.txt"
  echo "The public key is tools/linux/ahk-release.pub (AutoHotkey Linux"
  echo "Release <release@autohotkey-linux.invalid>)."
} > "$CKSUMS"
echo "built: dist/CKSUMS.txt"

if command -v gpg >/dev/null 2>&1; then
  KEYID="release@autohotkey-linux.invalid"
  if ! gpg --batch --list-secret-keys "$KEYID" >/dev/null 2>&1; then
    echo "AHK sign: generating a release-signing key (ephemeral) ..."
    gpg --batch --generate-key <<GPGEOF 2>/dev/null
%no-protection
Key-Type: RSA
Key-Length: 2048
Name-Real: AutoHotkey Linux Release
Name-Email: $KEYID
Expire-Date: 0
%commit
GPGEOF
  fi
  gpg --batch --yes --detach-sign --armor "$CKSUMS" 2>/dev/null
  gpg --armor --export "$KEYID" > dist/ahk-release.pub 2>/dev/null
  [ -s "$CKSUMS.asc" ] && echo "built: dist/CKSUMS.txt.asc" \
    || echo "AHK sign: warning: gpg signature failed (CKSUMS.txt.asc missing)"
fi