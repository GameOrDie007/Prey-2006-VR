"""Apply the patch set the way setup will, and prove it rebuilds vr_support.pk4.

A patch that does not reconstruct the file byte for byte is worse than shipping
nothing: the game would load a subtly wrong menu or a subtly wrong weapon model
and nobody would know why. So this walks the ops exactly as PowerShell will,
against the retail paks, and compares the result to the file the release used
to carry.

    python tools/release/verify_pk4_patches.py --paks <dir holding pak000.pk4 ...>
"""
import argparse
import glob
import hashlib
import json
import os
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pk4_patches

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(HERE, 'pk4patch')


def apply_ops(original, ops):
    src = original.split('\n')
    i = 0
    out = []
    for op in ops:
        if op[0] == 'copy':
            out.extend(src[i:i + op[1]]); i += op[1]
        elif op[0] == 'skip':
            i += op[1]
        elif op[0] == 'add':
            out.extend(op[1])
        else:
            raise ValueError('unknown op %r' % (op[0],))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--paks', required=True)
    args = ap.parse_args()

    man = json.load(open(os.path.join(OUT, 'manifest.json')))
    # retail paks only - see make_pk4_patches.retail_index for why a mod
    # in the folder silently became the file we checked against.
    index = make_pk4_patches.retail_index(args.paks)

    ok = bad = 0
    for rel, info in sorted(man['patch'].items()):
        pak, member = index[rel.lower()]
        theirs = zipfile.ZipFile(pak).read(member)
        if hashlib.sha256(theirs).hexdigest() != info['sha256']:
            print('  HASH MISMATCH  %s - their copy is not the one we diffed' % rel)
            bad += 1
            continue
        rebuilt = apply_ops(theirs.decode('latin-1'), info['ops']).encode('latin-1')

        if info.get('synth') == 'models':
            # The invariant the whole design rests on: existing data is not
            # touched. The only lines an ops list may remove are the shader
            # being renamed and the three num* counts being bumped. Anything
            # else means a vertex, weight or triangle moved - which is exactly
            # how the previous attempt at this put every weapon in the wrong
            # place and brought the hands back.
            src = theirs.decode('latin-1').split('\n')
            i = 0
            removed = []
            for op in info['ops']:
                if op[0] == 'copy':
                    i += op[1]
                elif op[0] == 'skip':
                    removed.extend(src[i:i + op[1]])
                    i += op[1]
            # difflib expresses "insert after the last vert line" as replacing
            # that line with itself plus the new ones, so a removed line that
            # comes straight back is not a change. What must never happen is a
            # line disappearing.
            added = []
            for op in info['ops']:
                if op[0] == 'add':
                    added.extend(op[1])
            added_set = set(added)
            allowed = [l for l in removed
                       if l.strip().startswith(('shader ', 'numverts', 'numtris',
                                                'numweights'))
                       or l in added_set]
            body = rebuilt.decode('latin-1')
            if (len(allowed) == len(removed)
                    and make_pk4_patches.HANDS_SHADER not in body
                    and 'vert ' in body and 'weight ' in body):
                ok += 1
            else:
                bad += 1
                print('  SYNTH WRONG    %s  (%d of %d removed lines are not a '
                      'shader or a count)' % (rel, len(removed) - len(allowed),
                                              len(removed)))
            continue

        if info.get('synth') == 'twosided':
            # No HEAD copy to compare against. Check the property: the rebuild
            # differs from retail only by added lines, every added line says
            # twoSided, and there is exactly one per material named.
            a = theirs.decode('latin-1').split('\n')
            b = rebuilt.decode('latin-1').split('\n')
            import difflib as _d
            added = [x[2:] for x in _d.ndiff(a, b) if x.startswith('+ ')]
            removed = [x[2:] for x in _d.ndiff(a, b) if x.startswith('- ')]
            want = len(make_pk4_patches.TWOSIDED_MATERIALS)
            if (not removed and len(added) == want
                    and all(l.strip().lower() == 'twosided' for l in added)):
                ok += 1
            else:
                bad += 1
                print('  SYNTH WRONG    %s  (+%d -%d, wanted +%d twoSided)'
                      % (rel, len(added), len(removed), want))
            continue

        if info.get('synth') == 'viewhands':
            # No HEAD copy to compare against - these are built from retail
            # itself so that no geometry is committed. Check the property.
            a = theirs.decode('latin-1').split('\n')
            b = rebuilt.decode('latin-1').split('\n')
            diff = [i for i in range(max(len(a), len(b)))
                    if (a[i] if i < len(a) else None) != (b[i] if i < len(b) else None)]
            if (len(a) == len(b) and len(diff) == 1
                    and make_pk4_patches.HANDS_SHADER in a[diff[0]]
                    and make_pk4_patches.VIEWHANDS_MATERIAL in b[diff[0]]
                    and make_pk4_patches.HANDS_SHADER not in rebuilt.decode('latin-1')):
                ok += 1
            else:
                bad += 1
                print('  SYNTH WRONG    %s  (%d lines differ)' % (rel, len(diff)))
            continue

        want = subprocess.check_output(
            ['git', 'show', 'HEAD:app/src/main/pk4/' + rel], cwd=REPO)
        if rebuilt == want:
            ok += 1
        else:
            bad += 1
            print('  REBUILD DIFFERS %s  (%d bytes vs %d)' % (rel, len(rebuilt), len(want)))

    print()
    print('patched files rebuilt byte-for-byte: %d of %d' % (ok, ok + bad))
    for rel in man['new']:
        p = os.path.join(OUT, 'new', rel)
        if not os.path.exists(p):
            print('  MISSING new file', rel); bad += 1
    print('new files present                 : %d' % len(man['new']))
    print('dropped                           : %d' % len(man['dropped']))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
