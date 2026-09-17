"""Does the config in a staged build actually carry the defaults we just changed?

Test builds carry the player's saves/preybase forward so they do not have to
re-run Setup, and preyconfig.cfg travels with it. Every pcvr_/vr_ setting we
ship is CVAR_ARCHIVE, so a value already in that file BEATS the default in the
source. Change a default, stage the build, and the game runs the old value.

That is not hypothetical. pcvr_mirrorHz was raised from 30 to 60, the build was
packaged, staged and tested, and the mirror still ran at 30 because the carried
config said 30. The report said so plainly - 2011 presents in 30.4 seconds -
and the answer to "are you sure you fixed it" was no.

    python tools/release/audit_staged_config.py <a staged build folder>

Differences are not automatically wrong: most are the packager's own PC
settings, or something the player chose. The point is to SEE them before
asking somebody to test, instead of after.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))

# Where archived pcvr_/vr_ cvars are declared.
SOURCES = [
    'app/src/main/jni/d3es-multithread-master/neo/framework/Common.cpp',
    'app/src/main/jni/d3es-multithread-master/neo/renderer/RenderSystem_init.cpp',
    'app/src/main/jni/d3es-multithread-master/neo/game/gamesys/SysCvar.cpp',
    'app/src/main/jni/d3es-multithread-master/neo/game/Vr.cpp',
]

DECL = re.compile(r'idCVar\s+\w+\(\s*"((?:pcvr|vr)_\w+)"\s*,\s*"([^"]*)"([^;]*)')
SETA = re.compile(r'seta\s+((?:pcvr|vr)_\w+)\s+"([^"]*)"')


def shipped_defaults():
    out = {}
    for rel in SOURCES:
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            continue
        s = io.open(p, encoding='latin-1', newline='').read()
        for m in DECL.finditer(s):
            if 'CVAR_ARCHIVE' in m.group(3):
                out[m.group(1)] = m.group(2)
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip().splitlines()[0] + '\n\n  usage: '
                 'audit_staged_config.py <staged build directory>')
    staged = sys.argv[1]
    cfg = os.path.join(staged, 'saves', 'preybase', 'preyconfig.cfg')
    if not os.path.exists(cfg):
        sys.exit('no preyconfig.cfg under %s - nothing carried forward, so the '
                 'defaults will apply' % staged)

    defaults = shipped_defaults()
    if not defaults:
        sys.exit('found no archived pcvr_/vr_ cvars - the source paths are wrong')

    have = dict(SETA.findall(io.open(cfg, encoding='latin-1', newline='').read()))

    rows = [(k, defaults[k], have[k]) for k in sorted(defaults)
            if k in have and have[k] != defaults[k]]

    print('%-26s %-12s %-12s' % ('archived cvar', 'source default', 'staged config'))
    print('-' * 56)
    for k, d, v in rows:
        print('%-26s %-12s %-12s' % (k, d, v))
    print()
    print('%d archived cvars, %d overridden by the carried config'
          % (len(defaults), len(rows)))

    # A check that cannot fail is worth nothing: prove it sees an override.
    probe = dict(have)
    for k in defaults:
        probe[k] = defaults[k] + '_x'
        break
    seen = len([1 for k in defaults if k in probe and probe[k] != defaults[k]])
    if seen < 1:
        print('CONTROL FAILED: a forced mismatch was not reported')
        return 1

    if rows:
        print()
        print('Check each one is deliberate. If you changed a default and it is')
        print('listed here, the person testing will NOT get your change.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
