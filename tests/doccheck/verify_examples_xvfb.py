#!/usr/bin/env python3
"""Extract doc examples and run them under Xvfb (MsgBox autoclose enabled)
so window/control/mouse examples run for real; report failures to
tests/doccheck/out/examples_xvfb_report.txt."""
import re, os, sys, glob, subprocess, concurrent.futures, time

BIN = sys.argv[1] if len(sys.argv) > 1 else 'build-core/source/linux/core/ahk_core'
GLOB = sys.argv[2] if len(sys.argv) > 2 else 'docs-v2/docs/lib/*.htm'


def ensure_xvfb():
    for d in ('99', '98', '97'):
        try:
            r = subprocess.run(['timeout', '3', 'xdpyinfo'],
                               env=dict(os.environ, DISPLAY=':' + d),
                               capture_output=True, timeout=5)
            if r.returncode == 0:
                return ':' + d
        except Exception:
            pass
    for d in ('99', '98', '97'):
        for f in ('/tmp/.X%s-lock' % d, '/tmp/.X11-unix/X%s' % d):
            try:
                os.remove(f)
            except OSError:
                pass
        p = subprocess.Popen(['Xvfb', ':' + d, '-screen', '0', '1024x768x24'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(10):
            time.sleep(0.4)
            try:
                r = subprocess.run(['timeout', '2', 'xdpyinfo'],
                                   env=dict(os.environ, DISPLAY=':' + d),
                                   capture_output=True, timeout=4)
                if r.returncode == 0:
                    return ':' + d
            except Exception:
                pass
        p.kill()
    return None


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


SKIP_RE = [re.compile(p) for p in [
    r'\bGui\b', r'\bSendMessage\b', r'\bPostMessage\b', r'\bOnMessage\b',
    r'\bDllCall\b', r'\bComObject\b', r'\bComObj', r'\bComCall\b', r'\bInputHook\b',
    r'\bSound', r'\bTrayTip\b', r'\bWinSetRegion\b', r'\bImageSearch\b', r'\bRunAs\b',
    r'\bDriveEject\b', r'\bDriveLock\b', r'\bFileInstall\b', r'\bRegRead\b', r'\bRegWrite\b',
    r'::', r'\w+::', r'\bHotkey\b', r'\bHotstring\b', r'A_LoopFile', r'\bLoop\s+Files\b',
    r'\bRun\b', r'\bRunWait\b', r'\bSetTimer\b', r'\bOnExit\b', r'\bOnError\b',
    r'\bClipboard\b', r'\bFileSelect\b', r'\bDirSelect\b', r'\bInputBox\b',
]]


def main():
    xdisp = ensure_xvfb()
    print('Xvfb display:', xdisp, flush=True)
    env = dict(os.environ)
    env['DISPLAY'] = xdisp or ''
    env['AHK_MSGBOX_AUTOCLOSE_MS'] = '500'
    env['AHK_INPUTBOX_AUTOCLOSE_MS'] = '300'
    env['AHK_FILESELECT_AUTOCLOSE_MS'] = '300'
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('DBUS_SESSION_BUS_ADDRESS', None)

    tasks = []
    skipped = 0
    os.makedirs('/tmp/ahk_doc_ex_x', exist_ok=True)
    for f in sorted(glob.glob(GLOB)):
        for i, b in enumerate(blocks(f)):
            if any(r.search(b) for r in SKIP_RE):
                skipped += 1
                continue
            tasks.append((f, i, b))

    def run_one(args):
        f, i, b = args
        name = os.path.basename(f).replace('.htm', '')
        ahk = '/tmp/ahk_doc_ex_x/%s_%d.ahk' % (name, i)
        with open(ahk, 'w', encoding='utf-8') as fh:
            fh.write(b + '\n')
        try:
            r = subprocess.run([BIN, ahk], capture_output=True, text=True, timeout=6, env=env)
            if r.returncode != 0 or 'Error' in r.stderr or 'Exception' in r.stderr:
                raw = (r.stderr or r.stdout or '').strip().split('\n')
                filtered = [x for x in raw if x.strip()]
                reason = filtered[-1][:150] if filtered else 'exit=%d' % r.returncode
                return (f, i, reason)
        except subprocess.TimeoutExpired:
            return (f, i, 'TIMEOUT')
        return None

    report = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        for res in ex.map(run_one, tasks):
            if res:
                report.append(res)

    lines = ['ran=%d skipped=%d failed=%d' % (len(tasks), skipped, len(report))]
    for f, i, why in report:
        lines.append('FAIL %s#%d: %s' % (os.path.basename(f), i, why))
    with open('tests/doccheck/out/examples_xvfb_report.txt', 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines) + '\n')
    print('ran=%d skipped=%d failed=%d' % (len(tasks), skipped, len(report)), flush=True)
    print('report: tests/doccheck/out/examples_xvfb_report.txt', flush=True)


if __name__ == '__main__':
    main()
