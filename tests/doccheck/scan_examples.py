import re, os, glob

# Replace inline-styled note-linux banners with theme classes:
#   - DllCall note (blue-ish): use .note
#   - COM notes (red-ish): use .warning
# Both are styled in theme.css + dark.css, so dark mode works.
replacements = [
    # DllCall Linux note (light blue)
    ('<div class="note-linux" style="background:#eef4fb;border-left:4px solid #3f5770;padding:.5em .8em;margin:.6em 0">',
     '<div class="note" style="padding:.5em .8em;margin:.6em 0">'),
    # COM notes (light red)
    ('<div class="note-linux" style="background:#fdf0ef;border-left:4px solid #a04040;padding:.5em .8em;margin:.6em 0">',
     '<div class="warning" style="padding:.5em .8em;margin:.6em 0">'),
]

changed = 0
for f in glob.glob("docs-v2/docs/**/*.htm", recursive=True):
    if "static" in f:
        continue
    s = open(f, encoding="utf-8", errors="ignore").read()
    orig = s
    for old, new in replacements:
        s = s.replace(old, new)
    if s != orig:
        open(f, "w", encoding="utf-8").write(s)
        changed += 1
print("patched pages:", changed)
