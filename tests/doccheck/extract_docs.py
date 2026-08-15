#!/usr/bin/env python3
"""Extract function/variable documentation entries from the official v2 docs.

Parses docs/lib/*.htm into a TSV index: name, description, syntax, params,
return type, return description, and the first simple example (code + result
comment if present).
"""
import os
import re
import html as html_mod
import sys

DOCS = "/mnt/f/AI/Codex/Autohotkey_Linux/docs-v2/docs/lib"
OUT = "/mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/doc_index.tsv"

def strip_tags(s):
    s = re.sub(r"<[^>]+>", "", s)
    s = html_mod.unescape(s)
    return s

def extract(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    name = os.path.basename(path)[:-4]
    # Description: first <p> after <h1>
    m = re.search(r"<h1>[^<]*</h1>\s*<p>(.*?)</p>", src, re.S)
    desc = strip_tags(m.group(1)).strip() if m else ""
    # Syntax block
    m = re.search(r'<pre class="Syntax">(.*?)</pre>', src, re.S)
    syntax = strip_tags(m.group(1)).strip() if m else ""
    syntax = " ".join(syntax.split())
    # Parameters
    params = []
    pm = re.search(r'<h2 id="Parameters">.*?</h2>(.*?)(<h2|</body>)', src, re.S)
    if pm:
        for dm in re.finditer(r"<dt>(.*?)</dt>\s*<dd>(.*?)</dd>", pm.group(1), re.S):
            pname = strip_tags(dm.group(1)).strip()
            pdesc = strip_tags(dm.group(2)).strip()
            pdesc = " ".join(pdesc.split())[:200]
            params.append(f"{pname}: {pdesc}")
    # Return value
    ret = ""
    rm = re.search(r'<h2 id="Return_Value">.*?</h2>(.*?)(<h2|</body>)', src, re.S)
    if rm:
        ret = " ".join(strip_tags(rm.group(1)).split())[:200]
    # First example block with its code
    ex_code = ""
    ex_result = ""
    em = re.search(r'<div class="ex"[^>]*>.*?<pre>(.*?)</pre>', src, re.S)
    if em:
        code = em.group(1)
        # Keep <em> content: it often holds "; Result: 43" expectations.
        code = strip_tags(code)
        lines = [l for l in code.splitlines() if l.strip()]
        ex_code = " | ".join(lines)[:300]
        r = re.search(r";\s*Result:\s*(.+)$", code, re.M)
        if r:
            ex_result = r.group(1).strip()[:100]
    return name, desc, syntax, " || ".join(params), ret, ex_code, ex_result

def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rows = []
    for fn in sorted(os.listdir(DOCS)):
        if not fn.endswith(".htm"):
            continue
        if fn.startswith("A_"):
            continue  # variables handled separately
        rows.append(extract(os.path.join(DOCS, fn)))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("name\tdesc\tsyntax\tparams\treturns\texample_code\texample_result\n")
        for r in rows:
            f.write("\t".join(r).replace("\n", " ").replace("\r", " ") + "\n")
    print(f"Wrote {len(rows)} function entries to {OUT}")

if __name__ == "__main__":
    main()
