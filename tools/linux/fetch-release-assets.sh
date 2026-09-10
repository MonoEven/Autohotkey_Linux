#!/bin/sh
# Fetch the published release assets of one released version into a target
# directory, verifying the tarball against that release's CKSUMS.txt.
#
# Usage: fetch-release-assets.sh <VER> <TARGET_DIR>
#        (VER like 2.0.26-linux.22)
#
# Why this exists: the cross-version upgrade/downgrade matrix needs the REAL
# N-1 artifacts, but a packaging job's dist/ only ever contains the version
# being built.  CI therefore downloads the previous release here, and the
# matrix oracle consumes the verified bytes -- not a rebuilt lookalike.
#
# GITHUB_TOKEN/GH_TOKEN are used when present (release downloads are subject
# to per-IP rate limits on shared runners).
set -u

VER="${1:-}"
DIR="${2:-}"
if [ -z "$VER" ] || [ -z "$DIR" ]; then
  echo "fetch-release-assets: usage: fetch-release-assets.sh <VER> <TARGET_DIR>" >&2
  exit 1
fi

command -v curl >/dev/null 2>&1 \
  || { echo "fetch-release-assets: curl is required" >&2; exit 1; }
command -v sha256sum >/dev/null 2>&1 \
  || { echo "fetch-release-assets: sha256sum is required" >&2; exit 1; }

BASE="https://github.com/MonoEven/Autohotkey_Linux/releases/download/v$VER"
TOKEN="${GITHUB_TOKEN:-${GH_TOKEN:-}}"
mkdir -p "$DIR" || exit 1

fetch() { # <asset-name>
  if [ -n "$TOKEN" ]; then
    curl -fsSL --connect-timeout 20 --max-time 300 \
      -H "Authorization: Bearer $TOKEN" -o "$DIR/$1" "$BASE/$1"
  else
    curl -fsSL --connect-timeout 20 --max-time 300 -o "$DIR/$1" "$BASE/$1"
  fi
}

# Mandatory: the checksum manifest and the tarball the matrix installs.
fetch CKSUMS.txt \
  || { echo "fetch-release-assets: cannot download CKSUMS.txt for v$VER" >&2; exit 1; }
TAR="autohotkey-linux-$VER-amd64.tar.gz"
fetch "$TAR" \
  || { echo "fetch-release-assets: cannot download $TAR" >&2; exit 1; }

# The manifest records "  <sha256>  <filename>" lines; match the filename
# exactly instead of pattern matching the version into a regex.
EXPECTED=$(awk -v n="$TAR" '$2 == n { print $1 }' "$DIR/CKSUMS.txt" | head -1)
if [ -z "$EXPECTED" ]; then
  echo "fetch-release-assets: CKSUMS.txt for v$VER has no entry for $TAR" >&2
  exit 1
fi
ACTUAL=$(sha256sum "$DIR/$TAR" | awk '{ print $1 }')
if [ "$ACTUAL" != "$EXPECTED" ]; then
  echo "fetch-release-assets: checksum mismatch for $TAR" >&2
  echo "  expected $EXPECTED" >&2
  echo "  actual   $ACTUAL" >&2
  exit 1
fi

# Companions are genuinely optional for the matrix; their absence is reported
# on stderr rather than silently ignored.
for name in "autohotkey-linux-$VER-amd64.deb" UNSIGNED.txt; do
  fetch "$name" \
    || echo "fetch-release-assets: note: optional asset unavailable: $name" >&2
done

echo "RELEASE_ASSETS_FETCHED ver=$VER dir=$DIR"
