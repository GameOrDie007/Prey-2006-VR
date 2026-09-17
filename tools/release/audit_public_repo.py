"""Audit the public tree before it is pushed. A push cannot be taken back.

Checks the repository as a stranger would clone it: what files are there, what
the history contains, and whether anything in it is about the people who made
it rather than the port.
"""
import io
import os
import re
import subprocess
import sys

PUB = r'E:\Tools\Games\PreyVR-PCVR-public'
ok = True


def chk(label, good, detail=''):
    global ok
    ok &= bool(good)
    print('  %-48s %s %s' % (label, 'PASS' if good else '*** FAIL ***', detail))


def git(*a):
    return subprocess.check_output(['git'] + list(a), cwd=PUB, text=True)


files = [f.strip() for f in git('ls-files').split('\n') if f.strip()]

# 1. the things that must not be there
for name in ('PROGRESS.md', 'local.properties', '2ms',
             'doc/headset-test-session.md'):
    chk('excluded: %s' % name, name not in files)
chk('excluded: tools/headless/', not [f for f in files if f.startswith('tools/headless/')])
chk('excluded: tools/build/', not [f for f in files if f.startswith('tools/build/')])

# 2. one commit, nothing to mine
n = git('rev-list', '--count', 'HEAD').strip()
chk('exactly one commit (no history to mine)', n == '1', n)
chk('author email is not a personal address',
    '@users.noreply.github.com' in git('log', '--format=%ae', '-1'))

# 3. nothing personal in text, anywhere
PERSON = re.compile(r'(?<![A-Za-z])(he|his|him|He|His|Him)(?![A-Za-z])')
TEXT = ('.c', '.cpp', '.h', '.py', '.ps1', '.bat', '.md', '.cfg', '.txt',
        '.gui', '.guifragment', '.mtr', '.json')


# These define the detector, so they contain the pattern by necessity.
DETECTORS = ('tools/release/audit_release_zip.py',
             'tools/release/audit_public_repo.py',
             'tools/release/audit_public_text.py',
             'tools/release/verify_staged.py')


def ours(path, lines, i):
    if path in DETECTORS:
        return False
    if path.startswith(('tools/', 'release/')):
        return True
    if 'SupportLibs' in path or 'licenses/' in path:
        return False
    return 'PCVR' in '\n'.join(lines[max(0, i - 25):i + 3])


def is_comment(path, line):
    t = line.strip()
    if path.lower().endswith(('.c', '.cpp', '.h')):
        return t.startswith(('//', '*', '/*'))
    return True


bad = []
for f in files:
    if not f.lower().endswith(TEXT) or 'SupportLibs' in f:
        continue
    p = os.path.join(PUB, f.replace('/', os.sep))
    try:
        lines = io.open(p, encoding='latin-1', newline='').read().splitlines()
    except Exception:
        continue
    for i, l in enumerate(lines):
        if PERSON.search(l) and is_comment(f, l) and ours(f, lines, i):
            bad.append('%s:%d %s' % (f, i + 1, l.strip()[:72]))
chk('no personal references in our own text', not bad)
for b in bad[:8]:
    print('        ' + b)

# 4. no path that only exists on this machine
# Patterns rather than examples: a detector that carries a real developer
# path is the thing it exists to find. B is a backslash, kept out of the
# source as a literal for the same reason.
B = chr(92)
DEVRE = re.compile('|'.join([
    # any user profile's scratch space, without naming a user
    '[A-Za-z]:[' + re.escape(B) + '/]Users[' + re.escape(B) + '/][^'
        + re.escape(B) + '/]+[' + re.escape(B) + '/]AppData',
    # any UNC share, without naming a host
    re.escape(B + B) + '[A-Za-z0-9_-]+' + re.escape(B) + 'VR Games',
    # an Android SDK path from whoever's machine wrote local.properties
    'sdk' + re.escape('.') + 'dir' + re.escape('='),
]))
dev = []
for f in files:
    # Vendored upstream files are excluded here for the same reason they are
    # excluded from the personal-reference check above: they are not ours and
    # we do not edit them. SDL's own Android build doc explains
    # local.properties and says "sdk.dir=" in the course of doing so, which is
    # the pattern this looks for.
    # DETECTORS for the same reason the personal check exempts them: a file
    # that defines a pattern necessarily contains it.
    if (not f.lower().endswith(TEXT) or 'SupportLibs' in f
            or f.startswith('licenses/') or f in DETECTORS):
        continue
    p = os.path.join(PUB, f.replace('/', os.sep))
    try:
        t = io.open(p, encoding='latin-1', newline='').read()
    except Exception:
        continue
    for i, l in enumerate(t.splitlines()):
        if DEVRE.search(l):
            dev.append('%s:%d %s' % (f, i + 1, l.strip()[:72]))
chk('no machine-specific or personal paths', not dev)
for d in dev[:8]:
    print('        ' + d)

# 5. the things that MUST be there
# tools/release/pk4patch/ is ignored on purpose (tools/release/.gitignore) -
# it is generated from retail data by make_pk4_patches.py. What has to ship is
# the generator, so a fork can rebuild a release from their own copy of Prey.
for need in ('README.md', 'LICENSE', 'licenses/README.md',
             'release/tools/setup.ps1', 'tools/release/make_pk4_patches.py',
             'release/tools/buildpk4.ps1'):
    chk('present: %s' % need, need in files)
chk('no game data committed', not [f for f in files if f.lower().endswith('.pk4')])

# control
chk('CONTROL: the personal test can fire', bool(PERSON.search('that was his call')))

print()
print('  %d files, 1 commit' % len(files))
print()
print('VERDICT:', 'safe to push' if ok else 'DO NOT PUSH')
sys.exit(0 if ok else 1)
