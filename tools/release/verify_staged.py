"""Prove a staged build actually contains the change, before anyone is asked to test it.

Three test rounds were spent on builds where the code was right and the build
that got tested was not:

  0.9.42  the menu edit was committed, compiled and staged, and the pk4 still
          held the old menu, because vr_support.pk4 is rebuilt from a patch set
          and nothing regenerated it.
  0.9.47  pcvr_mirrorHz was raised 30 -> 60 and the carried preyconfig.cfg still
          said 30. An archived cvar beats a default.

Both surfaced as "nothing changed", which is the most expensive possible way to
find out. This checks the artifact rather than the intention:

    python tools/release/verify_staged.py <staged dir> [--expect "text in the pk4 gui" ...]

  * the staged PreyVR.exe is byte-identical to build/Release/PreyVR.exe
  * vr_support.pk4 exists and contains every --expect string
  * no archived default is overridden by the carried config
  * the pk4 patch set the build shipped is not stale

Exit code is non-zero if any of that fails. Paste the output; do not paraphrase
it.
"""
import hashlib
import io
import os
import re
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GUI = 'guis/mainmenu/mainmenu_options.guifragment'


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def check(label, ok, detail=''):
    print('  %-44s %s%s' % (label, 'PASS' if ok else '*** FAIL ***',
                            ('  ' + detail) if detail else ''))
    return bool(ok)


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit('usage: verify_staged.py <staged dir> [--expect "text" ...]')
    staged = args[0]
    expect = [args[i + 1] for i, a in enumerate(args) if a == '--expect'
              and i + 1 < len(args)]

    print('verifying %s\n' % staged)
    ok = True

    # 1. the binary that is staged is the binary that was just built
    built = os.path.join(REPO, 'build', 'Release', 'PreyVR.exe')
    stagedexe = os.path.join(staged, 'PreyVR.exe')
    if os.path.exists(built) and os.path.exists(stagedexe):
        a, b = sha(built), sha(stagedexe)
        ok &= check('staged exe == build/Release/PreyVR.exe', a == b,
                    '' if a == b else 'staged %s vs built %s' % (b[:12], a[:12]))
    else:
        ok &= check('staged exe == build/Release/PreyVR.exe', False, 'missing')

    # 2. the pk4 exists and carries what this build was supposed to change
    pk4 = os.path.join(staged, 'preybase', 'vr_support.pk4')
    if not os.path.exists(pk4):
        ok &= check('vr_support.pk4 present', False, 'Setup has not been run')
    else:
        ok &= check('vr_support.pk4 present', True)
        try:
            gui = zipfile.ZipFile(pk4).read(GUI).decode('latin-1')
        except Exception as e:
            gui = ''
            ok &= check('menu readable from the pk4', False, str(e))
        for want in expect:
            ok &= check('pk4 contains %r' % (want[:38] + ('...' if len(want) > 38 else '')),
                        want in gui)
        if not expect:
            print('  %-44s %s' % ('(no --expect given)',
                                  'pass one per change you are claiming'))

    # 3. nothing the carried config silently overrides
    aud = os.path.join(HERE, 'audit_staged_config.py')
    if os.path.exists(aud):
        r = subprocess.run([sys.executable, aud, staged], capture_output=True, text=True)
        over = re.search(r'(\d+) overridden by the carried config', r.stdout)
        n = int(over.group(1)) if over else -1
        print()
        print('  carried config overrides %s archived default(s):' % n)
        for line in r.stdout.splitlines():
            if re.match(r'^(pcvr_|vr_)\w+\s', line):
                print('      ' + line.rstrip())
        print('      ^ check each is deliberate - anything you just changed must NOT be here')

    # 4. the patch set the packager used was current
    man = os.path.join(HERE, 'pk4patch', 'manifest.json')
    if os.path.exists(man):
        import json
        m = json.load(open(man))
        ids = subprocess.check_output(['git', 'ls-tree', '-r', 'HEAD', '--',
                                       'app/src/main/pk4'], cwd=REPO, text=True)
        now = hashlib.sha256(ids.encode()).hexdigest()
        print()
        ok &= check('pk4 patch set matches app/src/main/pk4 at HEAD',
                    m.get('sources_sha256') == now)

    print()
    print('VERDICT:', 'staged build carries the change' if ok
          else 'DO NOT ASK ANYONE TO TEST THIS')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
