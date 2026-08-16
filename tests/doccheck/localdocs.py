#!/usr/bin/env python3
"""Print compact sections of local docs-v2 pages for the given function names."""
import sys, re, html

def fetch(docsdir, name):
    path = f"{docsdir}/docs/lib/{name}.htm"
    try:
        t = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"== {name}: MISSING ({e})")
        return
    t = re.sub(r"<(script|style)[^>]*>.*?</\1>", " ", t, flags=re.S)
    parts = re.split(r"<h2[^>]*>", t)
    print(f"===== {name} =====")
    for p in parts:
        m = re.match(r"(.*?)</h2>(.*)", p, re.S)
        if not m:
            continue
        title = re.sub(r"<[^>]+>", "", m.group(1)).strip()
        body = re.sub(r"<[^>]+>", " ", m.group(2))
        body = html.unescape(body)
        body = re.sub(r"\s+", " ", body).strip()
        if title and body:
            print(f"--- {title}: {body[:700]}")

docsdir = "/mnt/f/AI/Codex/Autohotkey_Linux/docs-v2"
for name in sys.argv[1:]:
    fetch(docsdir, name)
