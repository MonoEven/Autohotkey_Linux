import re, os, glob

# Find pages where examples reference Windows-only things that we should
# annotate or rewrite.  Print a compact per-page action list.
issues = {}
for f in sorted(glob.glob('docs-v2/docs/**/*.htm', recursive=True)):
    if 'static' in f:
        continue
    s = open(f, encoding='utf-8').read()
    page = os.path.basename(f)
    # collect <pre> blocks (non-Syntax)
    blocks = []
    for m in re.finditer(r'<pre(?![^>]*class="[^"]*Syntax[^"]*")[^>]*>(.*?)</pre>', s, re.S):
        text = re.sub(r'<[^>]+>', '', m.group(1))
        text = (text.replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&')
                   .replace('&quot;', '"').replace('&#39;', "'"))
        blocks.append(text)
    if not blocks:
        continue
    kinds = set()
    for b in blocks:
        if re.search(r'\bDllCall\b', b):
            kinds.add('DllCall')
        if re.search(r'\bCom(Object|Obj\w*|Call)\b', b):
            kinds.add('COM')
        if re.search(r'\bGui\b|\bGuiControl\b|\bMenu\b', b):
            kinds.add('Gui/Menu')
        if re.search(r'\b(SendMessage|PostMessage|OnMessage)\b', b):
            kinds.add('WinMsg')
        if re.search(r'C:\\|A_WinDir|A_ProgramFiles', b):
            kinds.add('WinPath')
        if re.search(r'\b(Sound|TrayTip|TraySetIcon)\w*\b', b):
            kinds.add('Sound/Tray')
        if re.search(r'\bReg(Read|Write|Delete|CreateKey)\b', b):
            kinds.add('Reg')
        if re.search(r'\bHotstring\b|::', b):
            kinds.add('Hotstring')
    if kinds:
        issues[page] = sorted(kinds)

for page, kinds in sorted(issues.items()):
    print('%-35s %s' % (page, ', '.join(kinds)))
print('total pages:', len(issues))
