#!/bin/bash
# gh_ci_status.sh -- check latest CI runs on the fork's linux-port branch.
set -u
curl -s -H 'Accept: application/vnd.github+json' \
  'https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs?per_page=5&branch=linux-port' \
  | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
except Exception as e:
    print('parse error:', e); sys.exit(1)
for r in d.get('workflow_runs', []):
    print('sha=%s status=%s conclusion=%s name=%s' % (r['head_sha'][:8], r['status'], r.get('conclusion'), r.get('name')))
"
