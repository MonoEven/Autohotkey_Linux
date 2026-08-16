#!/bin/bash
cd /mnt/f/AI/Codex/Autohotkey_Linux || exit 1
REPORT=tests/doccheck/out/examples_failures.tsv
for p in "$@"; do
  python3 -u tests/doccheck/verify_append.py build-core/source/linux/core/ahk_core "$p" "$REPORT" 2>&1 | tail -1
done
echo "TOTAL: $(wc -l < "$REPORT") failures"
