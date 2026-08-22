#!/bin/bash
# gh_ci_logs2.sh -- download failed job logs and grep for FAIL lines.
set -u
RUN=${1:-32540435374}
curl -s -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs/$RUN/jobs" \
  | python3 -c "
import sys, json
d = json.load(sys.stdin)
for j in d.get('jobs', []):
    print(j['id'], j['name'], j.get('conclusion'))
" > /tmp/ci_jobs.txt
cat /tmp/ci_jobs.txt
# For each failed job, download logs and grep.
while read -r JID JNAME JCONC; do
  [ "$JCONC" = "failure" ] || continue
  echo "=== downloading job $JID ($JNAME) ==="
  curl -s -L -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/jobs/$JID/logs" -o "/tmp/ci_${JID}.log"
  echo "size=$(wc -c < /tmp/ci_${JID}.log)"
  echo "--- FAIL lines ---"
  grep -E '^(FAIL|DOC-CHECK)|FAIL:|want=' "/tmp/ci_${JID}.log" | head -40
done < /tmp/ci_jobs.txt
echo "done=1"
