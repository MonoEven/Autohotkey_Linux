#!/bin/bash
# gh_ci_artifact.sh -- list and download CI artifacts for a failed run.
set -u
RUN=${1:-32540435374}
echo "--- artifacts ---"
curl -s -H 'Accept: application/vnd.github+json' -H 'User-Agent: dsh-debug' \
  "https://api.github.com/repos/MonoEven/Autohotkey_Linux/actions/runs/$RUN/artifacts" \
  | python3 -c "
import sys, json
d = json.load(sys.stdin)
for a in d.get('artifacts', []):
    print(a['id'], a['name'], a.get('archive_download_url'))
"
