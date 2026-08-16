#!/bin/bash
cd /mnt/f/AI/Codex/Autohotkey_Linux || exit 1
for p in "$@"; do
  echo "== $p =="
  python3 -u tests/doccheck/verify_examples_xvfb.py build-core/source/linux/core/ahk_core "$p" 2>&1 | tail -2
done
