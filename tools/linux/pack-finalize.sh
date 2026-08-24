#!/bin/sh
# Regenerate CKSUMS.txt after all release packages have been built.
#
# Trust policy (check_detail0824 §13): GitHub Artifact Attestations are the
# primary provenance chain. OpenPGP is optional and is produced only from a
# configured long-term key; this script NEVER creates an ephemeral identity.
#
# Optional signing environment:
#   AHK_RELEASE_SIGNING_KEY          ASCII-armored private key
#   AHK_RELEASE_SIGNING_FINGERPRINT  pinned full fingerprint (required with key)
set -eu

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

mkdir -p dist
rm -f dist/CKSUMS.txt.asc dist/ahk-release.pub \
      dist/SIGNING_KEY_FINGERPRINT.txt dist/UNSIGNED.txt

if [ -n "${AHK_RELEASE_SIGNING_KEY:-}" ]; then
  [ -n "${AHK_RELEASE_SIGNING_FINGERPRINT:-}" ] || {
    echo "pack-finalize.sh: AHK_RELEASE_SIGNING_FINGERPRINT is required with the signing key" >&2
    exit 1
  }
  SIGN_STATUS="SIGNED with pinned OpenPGP key ${AHK_RELEASE_SIGNING_FINGERPRINT}"
else
  SIGN_STATUS="UNSIGNED (no long-term OpenPGP key configured; no ephemeral key was generated)"
fi

CKSUMS=dist/CKSUMS.txt
{
  echo "AutoHotkey v2 Linux port package set v$VER (built $(date -u +%Y-%m-%dT%H:%MZ))"
  echo "SHA-256 (one per line: '<hash>  <filename>'):"
  for f in dist/autohotkey-linux-$VER-* dist/*.vsix; do
    [ -f "$f" ] || continue
    case "$f" in
      *.tar.gz|*.deb|*.AppImage|*.rpm|*.vsix) ;;
      *) continue ;;
    esac
    name=$(basename "$f")
    printf '  %s  %s\n' "$(sha256sum "$f" | awk '{print $1}')" "$name"
  done
  echo
  echo "Verify a downloaded artifact hash with:"
  echo "  sha256sum -c <(grep '<filename>' CKSUMS.txt)"
  echo "Verify build provenance for a published artifact with:"
  echo "  gh attestation verify <artifact> --repo MonoEven/Autohotkey_Linux"
  echo "OpenPGP status: $SIGN_STATUS"
  echo "See SECURITY.md for the trust policy."
} > "$CKSUMS"
echo "built: dist/CKSUMS.txt"

if [ -z "${AHK_RELEASE_SIGNING_KEY:-}" ]; then
  {
    echo "This package set is not OpenPGP-signed."
    echo "No long-term AHK_RELEASE_SIGNING_KEY was configured, and the build"
    echo "correctly refused to generate a throwaway identity. Verify each"
    echo "published package with GitHub Artifact Attestations instead:"
    echo "  gh attestation verify <artifact> --repo MonoEven/Autohotkey_Linux"
  } > dist/UNSIGNED.txt
  echo "built: dist/UNSIGNED.txt"
  exit 0
fi

command -v gpg >/dev/null 2>&1 || {
  echo "pack-finalize.sh: gpg is required when AHK_RELEASE_SIGNING_KEY is set" >&2
  exit 1
}
GNUPGHOME_AHK=$(mktemp -d /tmp/ahk-release-gpg.XXXXXX)
chmod 700 "$GNUPGHOME_AHK"
cleanup_signing() { rm -rf "$GNUPGHOME_AHK"; }
trap cleanup_signing EXIT HUP INT TERM
printf '%s\n' "$AHK_RELEASE_SIGNING_KEY" \
  | gpg --homedir "$GNUPGHOME_AHK" --batch --import >/dev/null 2>&1
ACTUAL_FPR=$(gpg --homedir "$GNUPGHOME_AHK" --batch --with-colons \
  --list-secret-keys 2>/dev/null | awk -F: '$1 == "fpr" { print $10; exit }')
EXPECTED_FPR=$(printf '%s' "$AHK_RELEASE_SIGNING_FINGERPRINT" | tr -d '[:space:]' | tr '[:lower:]' '[:upper:]')
ACTUAL_FPR=$(printf '%s' "$ACTUAL_FPR" | tr '[:lower:]' '[:upper:]')
[ -n "$ACTUAL_FPR" ] && [ "$ACTUAL_FPR" = "$EXPECTED_FPR" ] || {
  echo "pack-finalize.sh: imported key fingerprint does not match the pinned fingerprint" >&2
  exit 1
}
gpg --homedir "$GNUPGHOME_AHK" --batch --yes --armor --detach-sign \
  --local-user "$ACTUAL_FPR" "$CKSUMS"
gpg --homedir "$GNUPGHOME_AHK" --batch --verify "$CKSUMS.asc" "$CKSUMS" >/dev/null 2>&1
printf '%s\n' "$ACTUAL_FPR" > dist/SIGNING_KEY_FINGERPRINT.txt
echo "built: dist/CKSUMS.txt.asc (fingerprint $ACTUAL_FPR)"
