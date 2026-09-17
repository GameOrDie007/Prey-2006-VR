"""Audit the release zip the way a stranger receives it.

Not "did the packager run" - what is actually inside the file that gets
downloaded: no game data, nothing personal, no developer paths, and a README
that answers the first three questions anyone will have.
"""
import os
import re
import sys
import zipfile

ZIP = sys.argv[1] if len(sys.argv) > 1 else 'dist/PreyVR-PCVR-0.9.49.zip'

z = zipfile.ZipFile(ZIP)
names = z.namelist()
root = names[0].split('/')[0]
ok = True


def chk(label, good, detail=''):
    global ok
    ok &= bool(good)
    print('  %-46s %s %s' % (label, 'PASS' if good else '*** FAIL ***', detail))


chk('no .pk4 anywhere in the zip', not [n for n in names if n.lower().endswith('.pk4')])
chk('no .pdb', not [n for n in names if n.lower().endswith('.pdb')])
chk('no "TEST - even world motion.bat"',
    not [n for n in names if 'even world motion' in n])

TEXT = ('.txt', '.md', '.cfg', '.bat', '.ps1')
PERSON = re.compile(r'(?<![A-Za-z])(he|his|him|He|His|Him)(?![A-Za-z])')
bad = []
for n in names:
    if not n.lower().endswith(TEXT):
        continue
    if 'licenses/' in n or n.endswith(('COPYING.txt', 'LICENSE')):
        continue                      # upstream licence text, not ours
    for ln in z.read(n).decode('latin-1').splitlines():
        if PERSON.search(ln):
            bad.append('%s: %s' % (n.split('/')[-1], ln.strip()[:70]))
chk('no personal references in shipped text', not bad)
for b in bad[:8]:
    print('        ' + b)

# Patterns, not one developer's actual paths - otherwise this file is the
# thing it exists to find. B is a backslash; writing it this way keeps the
# literal out of the source as well.
B = chr(92)
DEVRE = re.compile('|'.join([
    re.escape('E:' + B + 'Tools' + B + 'Games'),
    re.escape('E:/Tools/Games'),
    # any user profile, without naming one
    '[A-Za-z]:[' + re.escape(B) + '/]Users[' + re.escape(B) + '/][^'
        + re.escape(B) + '/]+[' + re.escape(B) + '/]AppData',
    # any UNC share, without naming a host
    re.escape(B + B) + '[A-Za-z0-9_-]+' + re.escape(B) + 'VR Games',
]))
dev = []
for n in names:
    if not n.lower().endswith(TEXT + ('.py', '.json')):
        continue
    for ln in z.read(n).decode('latin-1', 'replace').splitlines():
        if DEVRE.search(ln):
            dev.append('%s: %s' % (n.split('/')[-1], ln.strip()[:74]))
chk('no developer paths', not dev)
for d in dev[:8]:
    print('        ' + d)

v = z.read(root + '/version.txt').decode('latin-1').strip()
# Two or three components, both are real versions. 1.x is allowed here:
# package.py is the gate for that policy and refuses a 1.x version without
# --release. Duplicating the rule here only made it fire on the deliberate
# 1.0 release, and the old pattern wanted three components so 1.0 could
# never have passed anyway.
import re as _re
_m = _re.search(r'(\d+)\.(\d+)(?:\.(\d+))?', v)
chk('version.txt present and well formed', bool(_m), v.splitlines()[0])

rd = z.read(root + '/README.txt').decode('latin-1')
first = [l for l in rd.splitlines() if l.startswith('## ')][:1]
chk('README leads with Install', first == ['## Install'], str(first))
chk('README explains the desktop mirror', 'Desktop mirror' in rd)
chk('README explains supersampling', 'vr_supersampling' in rd)
chk('README says MSAA does nothing here', 'vr_msaa' in rd)

# A check that cannot fail proves nothing.
probe = PERSON.search('this is his fault')
chk('CONTROL: the personal-reference test can fire', bool(probe))

print()
print('  %d entries, %.1f MB' % (len(names), os.path.getsize(ZIP) / 1e6))
print()
print('VERDICT:', 'clean' if ok else 'NOT CLEAN')
sys.exit(0 if ok else 1)
