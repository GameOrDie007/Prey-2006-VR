"""Every cvar a menu row writes must be CVAR_ARCHIVE, or the setting evaporates.

A menu control on a non-archived cvar works for exactly as long as the game is
running and then silently forgets. That is the same failure as a control on a
dead cvar, one restart later, and it is invisible in testing unless somebody
quits and comes back.

Checks every cvar written by mainmenu_options.guifragment, ours and lvonasek's.
"""
import io
import os
import re

REPO = 'E:/Tools/Games/PreyVR-PCVR'
NEO = os.path.join(REPO, 'app/src/main/jni/d3es-multithread-master/neo')
GUI = os.path.join(REPO, 'app/src/main/pk4/guis/mainmenu/mainmenu_options.guifragment')

gui = io.open(GUI, encoding='latin-1', newline='').read()

# which rows are ours
ours = set()
for m in re.finditer(r'buttonDef\s+(PCVRO_\w+)_Row\s*\{(.*?)\n\t{7}\}', gui, re.S):
    for c in re.findall(r'cvar\s+"([A-Za-z0-9_]+)"', m.group(2)):
        ours.add(c)

wanted = []
seen = set()
for m in re.finditer(r'cvar\s+"([A-Za-z0-9_]+)"', gui):
    if m.group(1) not in seen:
        seen.add(m.group(1))
        wanted.append(m.group(1))

# find each declaration
decl = {}
for root, dirs, files in os.walk(NEO):
    dirs[:] = [d for d in dirs if d != '.git']
    for f in files:
        if not f.endswith(('.cpp', '.h')):
            continue
        p = os.path.join(root, f)
        try:
            t = io.open(p, encoding='latin-1').read()
        except Exception:
            continue
        for m in re.finditer(
                r'idCVar\s+([A-Za-z0-9_:]+)\s*\(\s*"([A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*,\s*([^,]+),',
                t, re.S):
            name = m.group(2)
            if name.lower() in [w.lower() for w in wanted]:
                decl.setdefault(name.lower(), []).append(
                    (m.group(3), ' '.join(m.group(4).split()),
                     os.path.relpath(p, REPO).replace('\\', '/')))

print('%-24s %-9s %-8s %-8s %s' % ('cvar', 'whose row', 'default', 'archived', 'where'))
print('-' * 96)
problems = []
for name in wanted:
    d = decl.get(name.lower())
    whose = 'ours' if name in ours else "theirs"
    if not d:
        print('%-24s %-9s %s' % (name, whose, 'NOT DECLARED ANYWHERE'))
        problems.append((name, whose, 'no declaration - the row moves nothing'))
        continue
    if len(d) > 1:
        problems.append((name, whose, 'declared %d times - see two-spellings-one-cvar'
                         % len(d)))
    dflt, flags, where = d[0]
    arch = 'yes' if 'ARCHIVE' in flags else 'NO'
    print('%-24s %-9s %-8s %-8s %s' % (name, whose, dflt, arch, where))
    if arch == 'NO':
        problems.append((name, whose, 'not archived - forgets on restart'))

print()
if problems:
    print('PROBLEMS')
    for name, whose, why in problems:
        print('  %-22s (%s) %s' % (name, whose, why))
else:
    print('every menu row writes an archived cvar that exists')
