#!/usr/bin/env python3
"""Inject Linux parity badges into function doc pages (check_detail0821 §13 / R2).

Reads tests/doccheck/parity.tsv and inserts a small badge line under each
matching function's <h1> in docs-v2/docs/lib/<name>.htm, so the four-level
classification (P2 adapted / P3 simulated / P4 unavailable) is visible on the
function's own documentation page.  Idempotent: a page already carrying the
badge marker is left untouched.  --check fails when a table entry with a doc
page has no badge (CI gate against drift).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "tests", "doccheck", "parity.tsv")
LIB = os.path.join(ROOT, "docs-v2", "docs", "lib")
MARKER = 'id="linux-parity"'

LABELS = {2: "adapted", 3: "simulated", 4: "unavailable"}


def _esc(s):
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;").replace(">", "&gt;")


def _entries():
    out = []
    for line in open(TSV, encoding="utf-8"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        name, level, note = parts[0], parts[1].strip(), parts[2]
        try:
            level_i = int(level.lstrip("P"))  # "P2" -> 2
        except ValueError:
            continue
        if level_i >= 2:
            out.append((name, level_i, note))
    return out


def _badge(name, level, note):
    label = LABELS.get(level, "")
    return ('<p class="parity-badge parity-p%d" %s title="%s">'
            "Linux parity: P%d %s</p>" % (level, MARKER, _esc(note), level, label))


def main():
    check = "--check" in sys.argv
    missing = []
    injected = 0
    for name, level, note in _entries():
        path = os.path.join(LIB, name + ".htm")
        if not os.path.exists(path):
            continue
        html = open(path, encoding="utf-8").read()
        if MARKER in html:
            continue
        if check:
            missing.append(name)
            continue
        m = re.search(r"(<h1>.*?</h1>\s*)", html, re.S)
        if not m:
            print("warning: no <h1> in %s" % path)
            continue
        badge = _badge(name, level, note)
        html = html[:m.end()] + badge + "\n" + html[m.end():]
        open(path, "w", encoding="utf-8").write(html)
        injected += 1
    if check:
        if missing:
            print("parity badges MISSING for: %s" % ", ".join(missing))
            sys.exit(1)
        print("parity badges: up to date")
    else:
        print("parity badges injected: %d" % injected)


if __name__ == "__main__":
    main()
