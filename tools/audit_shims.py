#!/usr/bin/env python3
# audit_shims.py -- census of the Win32 shims in source/linux/stdafx_linux.h
# (check_detail0821 §11).
#
# stdafx_linux.h carries Win32 types, macros AND behavioral shims in one
# header.  This script classifies every `inline` function it defines and
# reports which upstream sources call each one, so the "no-op looks like a
# real implementation" traps (e.g. AddClipboardFormatListener, fixed in
# round R1-1) are visible and countable.
#
# Classification (check_detail0821 §11.2):
#   real        -> has a substantive body (loops, calls, variables, math)
#   adapt       -> a single forwarding call with a trivial wrapper
#   noop-const  -> body is only `return <constant>;` or `{}`
#   noop-risky  -> noop-const BUT the declaration is not obviously safe to
#                  stub (heuristic: name suggests side effects, or returns a
#                  pointer/HANDLE that callers dereference).  (Manual review
#                  is still required; this is a triage aid.)
# Output: SHIM_CENSUS.tsv (tab-separated) on stdout (or a given path).
#
# Usage:
#   python3 tools/audit_shims.py            # print to stdout
#   python3 tools/audit_shims.py -o FILE    # write TSV

import re
import os
import sys
import argparse
import collections

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(REPO, "source", "linux", "stdafx_linux.h")

# Heuristic: names whose no-op stub is likely unsafe because callers rely on
# a real side effect.  Kept deliberately short; full list belongs to human
# review (the TSV exposes every candidate).
RISKY_SUBSTR = [
    "Clipboard", "Send", "Post", "GetWindow", "SetWindow", "Register",
    "Unregister", "Create", "Open", "Close", "Connect", "Load", "Find",
    "Wait", "Hook", "Keybd", "Mouse", "Draw", "Grab", "Release", "Alloc",
    "Free", "Query", "SetFocus", "GetFocus", "Destroy", "ShowWindow",
]


def parse_shims(path):
    """Yield (name, body) for each inline function definition in the header.

    Handles one-line signatures, multi-line signatures terminated by the
    opening brace, and free-standing declarations ending in ';' (skipped,
    they are externs, not shims)."""
    text = open(path, encoding="utf-8", errors="replace").read()
    # Normalize: collapse each physical line to strip leading ws; we match on
    # the token stream, so keep it simple with a state machine.
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].rstrip()
        m = re.match(r'\s*inline\s+([A-Za-z_:][\w:]*)\s+([A-Za-z_]\w*)\s*\(', line)
        if not m:
            i += 1
            continue
        # Signature may continue over multiple lines until the first '{' or ';'.
        sig = line
        j = i
        while ('{' not in sig) and (';' not in sig) and j + 1 < len(lines):
            j += 1
            sig += " " + lines[j].strip()
        if '{' not in sig:
            # Declaration only (ends with ';') -- not a shim definition.
            i = j + 1
            continue
        # Collect body from the '{' to its matching '}'.
        body_start = sig.index('{')
        body = sig[body_start:]
        k = j + 1
        depth = body.count('{') - body.count('}')
        while depth > 0 and k < len(lines):
            body += "\n" + lines[k]
            depth += lines[k].count('{') - lines[k].count('}')
            k += 1
        name = m.group(2)
        yield (name, body)
        i = k  # skip past this definition's body; continue scanning after it


def classify(name, body):
    body_stripped = " ".join(body.split())
    # A no-op stub: empty body, or a single return of a constant/expression
    # cast, or a bare negation.
    m = re.match(r'\{(\s*(return\s+[^;]*;)?\s*)\}', body_stripped)
    if m:
        ret = m.group(2) or ""
        is_constant = re.match(r'return\s+(FALSE|TRUE|0|1|-1|NULL|nullptr|OK|FAIL|""|\{\})', ret.strip())
        if is_constant:
            if any(s in name for s in RISKY_SUBSTR):
                return "noop-risky"
            return "noop-const"
        # Return of a variable or expression but empty otherwise: maybe a
        # stub that evaluates an arg (rare).  Triage as noop-const.
        if not ret:
            return "noop-const"
        return "noop-risky" if any(s in name for s in RISKY_SUBSTR) else "noop-const"
    # Forwarding single call: { return Fn(...); }
    if re.search(r'\{[^{}]*return\s+\w+\s*\([^{}]*\);.*\}', body_stripped):
        return "adapt"
    return "real"


def find_callers(name):
    needle = name
    callers = collections.Counter()
    src_dirs = [
        os.path.join(REPO, "source"),
        os.path.join(REPO, "lib"),
    ]
    for d in src_dirs:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for f in files:
                if not f.endswith(".cpp") and not f.endswith(".h"):
                    continue
                p = os.path.join(root, f)
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        txt = fh.read()
                except OSError:
                    continue
                # Only count as a caller if it appears as an identifier, but
                # exclude the definition itself (stdafx_linux.h).
                if p == HDR:
                    continue
                if re.search(r'\b' + re.escape(name) + r'\s*\(', txt):
                    rel = os.path.relpath(p, REPO).replace(os.sep, "/")
                    # one mark per file (a file either uses it or not)
                    if rel not in callers:
                        callers[rel] = 1
    return sorted(callers.keys())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", help="write TSV to this file (else stdout)")
    ap.add_argument("--header", default=HDR, help="header to scan")
    args = ap.parse_args()

    shims = list(parse_shims(args.header))
    out = ["shim_name\tclassification\tcaller_count\tcallers\tsignature"]
    counts = collections.Counter()
    for name, body in shims:
        cls = classify(name, body)
        counts[cls] += 1
        callers = find_callers(name)
        sig = " ".join(body.split('\n')[0].split())[:120]
        out.append("\t".join([
            name, cls, str(len(callers)), ",".join(callers[:12]), sig,
        ]))
    # Summary
    total = len(shims)
    summary = [
        "#" * 60,
        "# SHIM CENSUS: %d inline shims in %s" % (total, os.path.basename(args.header)),
        "#   " + ", ".join("%s=%d" % (k, v) for k, v in sorted(counts.items())),
        "# NOTE: classification is heuristic triage, not an audited verdict.",
        "# 'noop-risky' entries need human review (do they break callers?).",
        "#" * 60,
    ]
    text = "\n".join(summary + out) + "\n"
    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("wrote %s (%d shims, %d noop-risky)"
              % (args.output, total, counts.get("noop-risky", 0)),
              file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
