"""Cross-check every pcvr_* cvar against the places a user could find it.

A setting that exists in the code and nowhere else is the built-but-never-
switched-on failure: finished work hiding behind a default nobody can see.

Two instrument faults in the first version of this, both of which produced
confident nonsense:
  - the autoexec.cfg check wanted the cvar at the start of a line; the file
    writes `// seta pcvr_x "1"`, so it reported all 20 missing.
  - "read" was counted per FILE excluding the declaring one, so any cvar read
    only in the file that declares it - bloomScale, bloomRange - read as DEAD.
Both now count sites, and the self-test at the bottom fails if a cvar known to
be live or known to be present reads the wrong way.
"""
import io
import os
import re
import sys

REPO = 'E:/Tools/Games/PreyVR-PCVR'
NEO = os.path.join(REPO, 'app/src/main/jni/d3es-multithread-master/neo')
VRC = os.path.join(REPO, 'app/src/main/jni/Doom3Quest')

DECL = re.compile(
    r'idCVar\s+([A-Za-z0-9_:]+)\s*\(\s*"(pcvr_[A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*,'
    r'\s*([^,]+),\s*"([^"]*)"', re.S)

# diagnostics and harness knobs: deliberately not user settings
DIAGNOSTIC = {
    'pcvr_autoQuit', 'pcvr_autoQuitCeiling', 'pcvr_dumpClearTest',
    'pcvr_dumpFrames', 'pcvr_freezeRepeat', 'pcvr_freezeSeconds',
    'pcvr_freezeTest', 'pcvr_layerMode', 'pcvr_padInfo',
    'pcvr_flatWeaponOffset', 'pcvr_evenTics', 'pcvr_mouseInfo',
}

decls = {}
reads = {}
declsites = {}


def scan(path):
    for root, dirs, files in os.walk(path):
        dirs[:] = [d for d in dirs if d != '.git']
        for f in files:
            if not f.endswith(('.cpp', '.h', '.c')):
                continue
            p = os.path.join(root, f)
            try:
                t = io.open(p, encoding='latin-1').read()
            except Exception:
                continue
            rel = os.path.relpath(p, REPO).replace('\\', '/')
            for m in DECL.finditer(t):
                decls[m.group(2)] = (m.group(3), ' '.join(m.group(4).split()),
                                     m.group(5), rel, m.group(1))
                declsites.setdefault(m.group(2), []).append((rel, m.start(), m.end()))
            for m in re.finditer(r'\b(pcvr_[A-Za-z0-9_]+)\b', t):
                reads.setdefault(m.group(1), []).append((rel, m.start()))


scan(NEO)
scan(VRC)

# a mention is a read unless it falls inside an idCVar declaration, and the C
# layer reaches cvars by string through Android_GetCVar*
for name, sites in list(reads.items()):
    spans = declsites.get(name, [])
    live = [s for s in sites
            if not any(s[0] == d[0] and d[1] <= s[1] < d[2] for d in spans)]
    reads[name] = live

# the C++ variable name is what the code actually reads (pcvr_bloomScale.GetFloat)
for name, (_, _, _, _, var) in decls.items():
    short = var.split('::')[-1]
    if short != name:
        for root, dirs, files in os.walk(NEO):
            for f in files:
                if f.endswith(('.cpp', '.h')):
                    t = io.open(os.path.join(root, f), encoding='latin-1').read()
                    if re.search(r'\b' + re.escape(short) + r'\s*\.\s*Get', t):
                        reads.setdefault(name, []).append((f, -1))

cfg = io.open(os.path.join(REPO, 'release/autoexec.cfg'), encoding='latin-1').read()
readme = io.open(os.path.join(REPO, 'README.md'), encoding='utf-8').read()
gui = io.open(os.path.join(
    REPO, 'app/src/main/pk4/guis/mainmenu/mainmenu_options.guifragment'),
    encoding='latin-1').read()

rows = []
for name in sorted(decls):
    dflt, flags, desc, where, var = decls[name]
    rows.append((
        name, dflt,
        'Y' if re.search(r'\b' + name + r'\b', cfg) else '.',
        'Y' if ('"' + name + '"') in gui else '.',
        'Y' if name in readme else '.',
        len(reads.get(name, ())),
        name in DIAGNOSTIC, desc, where))

# --- self-test: an instrument that cannot fail proves nothing ---------------
by = {r[0]: r for r in rows}
fail = []
if by['pcvr_portalDistance'][2] != 'Y':
    fail.append('portalDistance IS in autoexec.cfg and the check missed it')
if by['pcvr_bloomScale'][5] == 0:
    fail.append('bloomScale IS read in game_playerview.cpp and the check missed it')
if by['pcvr_freezeTest'][2] == 'Y':
    fail.append('freezeTest is NOT in autoexec.cfg and the check claims it is')
if fail:
    print('INSTRUMENT FAILED ITS OWN CONTROLS:')
    for f in fail:
        print('  ' + f)
    sys.exit(1)

print('%-24s %-8s cfg menu doc reads' % ('cvar', 'default'))
print('-' * 58)
for name, dflt, c, g, d, n, diag, desc, where in rows:
    print('%-24s %-8s  %s   %s    %s   %-3d %s' %
          (name, dflt, c, g, d, n, 'diagnostic' if diag else ''))

print()
gaps = [r for r in rows if not r[6] and r[2] == '.']
dead = [r for r in rows if r[5] == 0]
if gaps:
    print('USER SETTINGS NOT IN autoexec.cfg')
    for name, dflt, c, g, d, n, diag, desc, where in gaps:
        print('  %-22s default %-8s %s%s' %
              (name, dflt, 'in a menu, ' if g == 'Y' else '', desc))
if dead:
    print()
    print('DECLARED AND NEVER READ')
    for r in dead:
        print('  %-22s %s' % (r[0], r[8]))
if not gaps and not dead:
    print('clean: every user setting is in autoexec.cfg, nothing is dead')
print()
print('%d pcvr cvars, %d user settings, %d diagnostics' %
      (len(rows), len([r for r in rows if not r[6]]), len([r for r in rows if r[6]])))
