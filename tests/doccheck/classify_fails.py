import re
from collections import Counter

c = Counter()
detail = {}
for line in open('tests/doccheck/out/examples_failures.tsv', encoding='utf-8'):
    line = line.strip()
    if not line:
        continue
    parts = line.split('\t', 1)
    if len(parts) < 2:
        continue
    page, why = parts
    # categorize
    if 'exit=1' in why:
        cat = 'exit=1 (runtime error)'
    elif 'exit=2' in why:
        cat = 'exit=2 (parse error)'
    elif 'TIMEOUT' in why:
        cat = 'TIMEOUT'
    elif 'global variable has not been assigned' in why:
        cat = 'fragment (undefined var)'
    elif 'does not contain a recognized action' in why or 'Unexpected' in why:
        cat = 'fragment (syntax)'
    elif 'Missing' in why:
        cat = 'fragment (incomplete)'
    elif 'No X display' in why:
        cat = 'needs X (should be fixed by Xvfb)'
    elif 'cannot find the file' in why or 'does not exist' in why:
        cat = 'missing file/resource'
    elif 'Failed' in why or why.startswith('('):
        cat = 'runtime Failed'
    else:
        cat = 'other: ' + why[:60]
    c[cat] += 1
    detail.setdefault(cat, []).append((page, why[:80]))

for k, v in c.most_common():
    print('%4d  %s' % (v, k))
print()
print('=== sample details ===')
for cat in ['exit=1 (runtime error)', 'exit=2 (parse error)', 'TIMEOUT', 'runtime Failed', 'missing file/resource']:
    print('---', cat, '---')
    for p, w in detail.get(cat, [])[:12]:
        print('   ', p, '|', w)
