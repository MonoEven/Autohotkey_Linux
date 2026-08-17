#!/usr/bin/env python3
"""Coverage of worklist IMPL functions by reference in the assert_*.ahk test
sources (round-1 refresh).

Two metrics are reported:
  * 'code'     - the function name appears outside comments (a real
                 invocation / usage in executable code).
  * 'all'      - the name appears anywhere in the source, including the
                 documentation comments (e.g. the misc-cov header that
                 documents the 4 non-automatable functions).

Usage: python3 _cov4.py [worklist.tsv]
"""
import os
import re
import sys

BASE = "/mnt/f/AI/Codex/Autohotkey_Linux"
DCHK = os.path.join(BASE, "tests/doccheck")

def load_impl(path):
    impl = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2 and parts[1] == "IMPL":
                impl.add(parts[0])
    return impl

def strip_comments(src):
    # Block comments.
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    # Line comments (; to EOL).  Handles `;` in strings only by heuristic:
    # none of the assert scripts use `;` inside string literals.
    out = []
    for line in src.splitlines():
        out.append(line.split(";", 1)[0])
    return "\n".join(out)

def load_sources():
    text = {}      # raw (incl comments)
    text_nc = {}   # comments stripped
    for fn in sorted(os.listdir(DCHK)):
        if fn.endswith(".ahk"):
            p = os.path.join(DCHK, fn)
            with open(p, encoding="utf-8") as f:
                t = f.read()
            text[fn] = t
            text_nc[fn] = strip_comments(t)
    p = os.path.join(DCHK, "run_check.sh")
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            t = f.read()
        text["run_check.sh"] = t
        text_nc["run_check.sh"] = t  # shell comments not a coverage source.
    return text, text_nc

def refs(name, sources):
    pat = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])")
    return any(pat.search(t) for t in sources.values())

def main():
    wl = sys.argv[1] if len(sys.argv) > 1 else os.path.join(DCHK, "worklist.tsv")
    impl = load_impl(wl)
    text, text_nc = load_sources()
    code_ref = sorted(n for n in impl if refs(n, text_nc))
    all_ref = sorted(n for n in impl if refs(n, text))
    print(f"IMPL total: {len(impl)}")
    print(f"referenced in executable code   : {len(code_ref)}")
    print(f"referenced incl. doc comments   : {len(all_ref)}")
    ncode = sorted(set(impl) - set(code_ref))
    print(f"NOT referenced in code: {len(ncode)}")
    for n in ncode:
        print(f"  code-missing: {n}")
    nall = sorted(set(impl) - set(all_ref))
    print(f"NOT referenced anywhere (incl. docs): {len(nall)}")
    for n in nall:
        print(f"  doc-missing too: {n}")

if __name__ == "__main__":
    main()
