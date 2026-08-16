#!/usr/bin/env python3
"""Extract doc examples and run them against ahk_core (parallel) to find
non-runnable ones.  Forces headless: DISPLAY/WAYLAND_DISPLAY cleared so
MsgBox prints to stdout instead of opening a window."""
import re, os, sys, glob, subprocess, concurrent.futures

BIN = sys.argv[1] if len(sys.argv) > 1 else 'build-core/source/linux/core/ahk_core'
GLOB = sys.argv[2] if len(sys.argv) > 2 else 'docs-v2/docs/lib/*.htm'

HEADLESS_ENV = dict(os.environ)
HEADLESS_ENV['DISPLAY'] = ''
HEADLESS_ENV.pop('WAYLAND_DISPLAY', None)
HEADLESS_ENV.pop('DBUS_SESSION_BUS_ADDRESS', None)

def blocks(path):
    s = open(path, encoding='utf-8').read()
    out = []
    for m in re.finditer(r'<pre(?![^>]*class="[^"]*Syntax[^"]*")[^>]*>(.*?)</pre>', s, re.S):
        text = re.sub(r'<[^>]+>', '', m.group(1))
        text = (text.replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&')
                   .replace('&quot;', '"').replace('&#39;', "'").replace('&nbsp;', ' '))
        text = text.strip()
        if text:
            out.append(text)
    return out

SKIP_PATTERNS = [
    r'\bGui\b', r'\bSendMessage\b', r'\bPostMessage\b', r'\bOnMessage\b',
    r'\bDllCall\b', r'\bComObject\b', r'\bComObj', r'\bComCall\b', r'\bInputHook\b',
    r'\bSound', r'\bTrayTip\b', r'\bWinSetRegion\b', r'\bImageSearch\b', r'\bRunAs\b',
    r'\bDriveEject\b', r'\bDriveLock\b', r'\bFileInstall\b', r'\bRegRead\b', r'\bRegWrite\b',
    r'::', r'\w+::', r'\bHotkey\b', r'\bHotstring\b', r'A_LoopFile', r'\bLoop\s+Files\b',
    r'\bRun\b', r'\bRunWait\b', r'\bSetTimer\b', r'\bOnExit\b', r'\bOnError\b',
    r'\bClipboard\b', r'\bFileSelect\b', r'\bDirSelect\b', r'\bInputBox\b',
    r'\bClick\b', r'\bSend\b', r'\bSendInput\b', r'\bMouseMove\b', r'\bKeyWait\b',
]
SKIP_RE = [re.compile(p) for p in SKIP_PATTERNS]

def should_skip(text):
    for r in SKIP_RE:
        if r.search(text):
            return True
    return False

os.makedirs('/tmp/ahk_doc_ex', exist_ok=True)

def run_one(args):
    f, i, b = args
    name = os.path.basename(f).replace('.htm', '')
    ahk = '/tmp/ahk_doc_ex/%s_%d.ahk' % (name, i)
    with open(ahk, 'w', encoding='utf-8') as fh:
        fh.write(b + '\n')
    try:
        r = subprocess.run([BIN, ahk], capture_output=True, text=True, timeout=6,
                           env=HEADLESS_ENV)
        if r.returncode != 0 or 'Error' in r.stderr or 'Exception' in r.stderr:
            raw = (r.stderr or r.stdout or '').strip().split('\n')
            filtered = [x for x in raw if x.strip()]
            reason = filtered[-1][:150] if filtered else 'exit=%d' % r.returncode
            return (f, i, reason)
    except subprocess.TimeoutExpired:
        return (f, i, 'TIMEOUT')
    return None

tasks = []
skipped = 0
for f in sorted(glob.glob(GLOB)):
    for i, b in enumerate(blocks(f)):
        if should_skip(b):
            skipped += 1
            continue
        tasks.append((f, i, b))

report = []
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
    for res in ex.map(run_one, tasks):
        if res:
            report.append(res)

print('ran=%d skipped=%d failed=%d' % (len(tasks), skipped, len(report)))
for f, i, why in report:
    print('FAIL %s#%d: %s' % (os.path.basename(f), i, why))
