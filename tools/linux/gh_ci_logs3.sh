#!/bin/bash
# gh_ci_logs3.sh -- download job logs with verbose status + auth headers.
set -u
JID=${1:-96949266900}
URL="https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/jobs/$JID/logs"
echo "URL=$URL"
curl -sS -L -D - -H 'Accept: application/vnd.github+json' -H 'User-Agent: dsh-debug' \
  -o "/tmp/ci_${JID}.log" "$URL" 2>&1 | grep -iE 'HTTP/|content-type|location' | head -10
echo "size=$(wc -c < /tmp/ci_${JID}.log 2>/dev/null || echo 0)"
echo "--- FAIL lines ---"
grep -E 'FAIL:|want=|DOC-CHECK|runner exit' "/tmp/ci_${JID}.log" 2>/dev/null | head -50
echo "done=1"
