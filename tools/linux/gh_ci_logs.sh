#!/bin/bash
# gh_ci_logs.sh -- fetch the failing CI job's logs for a commit.
set -u
SHA=${1:-51f3013138b6e3527cd0e793aa82214903fe6470}
RUN=$(curl -s -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs?head_sha=$SHA" \
  | python3 -c "
import sys, json
d = json.load(sys.stdin)
for r in d.get('workflow_runs', []):
    if r.get('name') == 'CI':
        print(r['id']); break
")
echo "run_id=$RUN"
[ -z "$RUN" ] && { echo "no run found"; exit 1; }
curl -s -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs/$RUN/jobs" \
  | python3 -c "
import sys, json
d = json.load(sys.stdin)
for j in d.get('jobs', []):
    print('job=%s status=%s conclusion=%s' % (j['name'], j['status'], j.get('conclusion')))
    for s in j.get('steps', []):
        if s.get('conclusion') == 'failure':
            print('  FAILED_STEP: %s' % s['name'])
"
