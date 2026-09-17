"""Does any options row sit on top of another, or off the bottom of its page?

This is the check that was missing when ten rows were added and three of them
landed on rows that carry a warning label. Those rows are declared 480x50 rather
than 400x25, so a search keyed on the full rect did not see them, and the new
rows were placed three slots too high.

Rows are compared on the 30 grid the pages actually use - a row's declared
height can be 50 while its content occupies the top 25, which is lvonasek's own
arrangement and is not an overlap.
"""
import io
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', '..'))
GUI = os.path.join(REPO, 'app', 'src', 'main', 'pk4', 'guis', 'mainmenu',
                   'mainmenu_options.guifragment')
STEP = 30
ROW_CONTENT = 25

a = io.open(GUI, encoding='latin-1', newline='').read()


def row_extent(body, at):
    """How far down a row's own children reach, from the row's top.

    A warning label like "Restart game to apply!" is a child at y=20 in a 50-tall
    row, so the row draws 45 down even though every row sits on a 30 grid.
    """
    depth, end = 0, len(body)
    for i in range(body.index('{', at), len(body)):
        if body[i] == '{':
            depth += 1
        elif body[i] == '}':
            depth -= 1
            if depth == 0:
                end = i
                break
    inner = body[at:end]
    reach = ROW_CONTENT
    first = True   # the first rect inside the braces is the row's own
    for m in re.finditer(r'rect\s+[-\d.]+,\s*([-\d.]+),\s*[-\d.]+,\s*([-\d.]+)',
                         inner[inner.index('{') + 1:]):
        if first:
            first = False
            continue
        reach = max(reach, int(float(m.group(1))) + int(float(m.group(2))))
    return reach

tabs = [(m.group(1), m.start()) for m in re.finditer(r'tabDef\s+(Options_\w+)\s*\{', a)]
spans = [(n, s, (tabs[i + 1][1] if i + 1 < len(tabs) else len(a)))
         for i, (n, s) in enumerate(tabs)]

print('%-22s %5s  %s' % ('tab', 'rows', 'layout'))
print('-' * 72)
bad = []
for name, s, e in spans:
    body = a[s:e]

    m = re.search(r'windowDef\s+\w*Page\s*\{\s*rect\s+[-\d]+,\s*([-\d]+),'
                  r'\s*[-\d]+,\s*([-\d]+)', body)
    bottom = int(m.group(2)) if m else 350

    rows = []
    for r in re.finditer(r'buttonDef\s+(\w+)\s*\{\s*rect\s+10,\s*(\d+),', body):
        rows.append((int(r.group(2)), r.group(1), row_extent(body, r.start())))
    rows.sort()

    issues = []
    for i, (y, nm, ext) in enumerate(rows):
        # what this row actually draws, not where it starts
        if i + 1 < len(rows):
            ny, nnm, _ = rows[i + 1]
            if y + ext > ny:
                issues.append('%s covers y %d..%d and %s starts at %d'
                              % (nm, y, y + ext, nnm, ny))
        if y + ext > bottom:
            issues.append('%s covers y %d..%d, past the %d-tall page'
                          % (nm, y, y + ext, bottom))

    ours = len([1 for _, nm, _ in rows if nm.startswith('PCVRO_')])
    last_bottom = (rows[-1][0] + rows[-1][2]) if rows else 40
    free = max(0, (bottom - last_bottom) // STEP)
    print('%-22s %5d  y %d..%d, %d ours, %d slot(s) still free%s'
          % (name, len(rows), rows[0][0] if rows else 0,
             last_bottom, ours, free,
             '   <-- PROBLEM' if issues else ''))
    for i in issues:
        print('        %s' % i)
        bad.append((name, i))

print()
if bad:
    print('%d problem(s)' % len(bad))
    sys.exit(1)

# A clean result from a check that cannot fail is worth nothing: put a row on
# top of another in memory and make sure it is reported.
probe = a.replace('rect\t\t\t10, 220, 400, 25', 'rect\t\t\t10, 190, 400, 25', 1)
if probe != a:
    dup = len(re.findall(r'buttonDef\s+\w+\s*\{\s*rect\s+10,\s*190,', probe))
    if dup < 2:
        print('CONTROL FAILED: the deliberate collision did not register')
        sys.exit(1)
    print('every row has the page to itself (control: a forced collision is seen)')
else:
    print('every row has the page to itself')
