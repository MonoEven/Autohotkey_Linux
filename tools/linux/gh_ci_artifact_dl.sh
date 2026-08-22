#!/bin/bash
# gh_ci_artifact_dl.sh -- download a CI artifact and show failing assertions.
set -u
AID=${1:-9466892727}
OUT=/tmp/dc_artifact_${AID}.zip
curl -sS -L -H 'Accept: application/vnd.github+json' -H 'User-Agent: dsh-debug' \
  -o "$OUT" "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/artifacts/$AID/zip" 2>&1 | head -5
echo "size=$(wc -c < "$OUT" 2>/dev/null || echo 0)"
rm -rf /tmp/dc_artifact_${AID}
mkdir -p /tmp/dc_artifact_${AID}
cd /tmp/dc_artifact_${AID} || exit 1
unzip -q -o "$OUT" 2>/dev/null
echo "--- files ---"
ls | head -20
echo "--- all .txt outputs (tail) ---"
for f in *.txt; do echo "== $f =="; tail -3 "$f"; done 2>/dev/null | head -80
