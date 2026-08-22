#!/bin/bash
# gh_ci_annotations.sh -- fetch check-run annotations for a failed run.
set -u
RUN=${1:-32540435374}
# Get check runs for the run.
curl -s -H 'Accept: application/vnd.github+json' -H 'User-Agent: dsh-debug' \
  "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs/$RUN/check-runs" \
  | python3 -c "
import sys, json
d = json.load(sys.stdin)
for cr in d.get('check_runs', []):
    print('check=%s status=%s conclusion=%s id=%s' % (cr.get('name'), cr.get('status'), cr.get('conclusion'), cr.get('id')))
"
