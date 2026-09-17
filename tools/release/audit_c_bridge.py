"""Every cvar the C layer asks for by name must exist.

The VR layer is C and cannot see idCVar, so it reaches settings through
Android_GetCVarInteger("name"). A name that does not resolve returns 0 - no
warning, no log line - and the feature it gates is simply off forever. That is
the absent-setting-reads-as-zero failure with the typo on our side.

Checks the whole C layer against every idCVar declared in the engine.
"""
import io
import os
import re

REPO = 'E:/Tools/Games/PreyVR-PCVR'
NEO = os.path.join(REPO, 'app/src/main/jni/d3es-multithread-master/neo')
CLAYER = os.path.join(REPO, 'app/src/main/jni/Doom3Quest')

declared = {}
for root, dirs, files in os.walk(NEO):
    dirs[:] = [d for d in dirs if d != '.git']
    for f in files:
        if not f.endswith(('.cpp', '.h')):
            continue
        t = io.open(os.path.join(root, f), encoding='latin-1').read()
        for m in re.finditer(r'idCVar\s+[A-Za-z0-9_:]+\s*\(\s*"([A-Za-z0-9_]+)"', t):
            declared.setdefault(m.group(1).lower(),
                                os.path.relpath(os.path.join(root, f), REPO))

asked = {}
for root, dirs, files in os.walk(CLAYER):
    for f in files:
        if not f.endswith(('.c', '.h', '.cpp')):
            continue
        p = os.path.join(root, f)
        t = io.open(p, encoding='latin-1').read()
        for m in re.finditer(
                r'Android_GetCVar(?:Integer|Float|String)\s*\(\s*"([A-Za-z0-9_]+)"', t):
            asked.setdefault(m.group(1), set()).add(
                '%s:%d' % (f, t.count('\n', 0, m.start()) + 1))

# the C++ side reaches for names too, in the places that bridge to the VR layer
for rel in ('framework/Common.cpp', 'sys/win32/win_pcvr.cpp'):
    p = os.path.join(NEO, rel)
    if not os.path.exists(p):
        continue
    t = io.open(p, encoding='latin-1').read()
    for m in re.finditer(
            r'GetCVar(?:Integer|Float|String)\s*\(\s*"([A-Za-z0-9_]+)"', t):
        asked.setdefault(m.group(1), set()).add(
            '%s:%d' % (os.path.basename(p), t.count('\n', 0, m.start()) + 1))

print('%d cvars declared, %d asked for by name' % (len(declared), len(asked)))
print()
print('%-26s %s' % ('name asked for', 'resolves to'))
print('-' * 80)
missing = []
for name in sorted(asked):
    where = declared.get(name.lower())
    if where:
        print('%-26s %s' % (name, where.replace('\\', '/')))
    else:
        print('%-26s *** NOT DECLARED - reads 0 forever ***' % name)
        missing.append((name, sorted(asked[name])))

# A clean result is only worth having if the check could have failed. Feed it a
# name that cannot exist and make sure it is reported.
probe = 'pcvr_thisDoesNotExist'
assert probe.lower() not in declared
_probe_missing = declared.get(probe.lower()) is None
if not _probe_missing:
    raise SystemExit('the check has no power: a made-up name resolved')

print()
if missing:
    print('PROBLEMS')
    for name, sites in missing:
        print('  %-24s asked at %s' % (name, ', '.join(sites)))
else:
    print('every name the C layer asks for is a cvar that exists')
