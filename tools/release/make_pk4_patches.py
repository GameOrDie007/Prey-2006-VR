"""Turn vr_support.pk4 into something we are allowed to ship.

The problem
-----------
vr_support.pk4 is the one pk4 the release carries, and 31 of its 50 files are
derived from retail Prey - 7 byte-identical copies of Human Head's files and 24
modified versions of them, including all eight weapon .md5mesh models, the main
menu, the HUD and two map scripts. Modified or not, that is 2K Games' content,
and shipping it breaks the same no-game-data rule the Quake and Quake II ports
keep.

The fix
-------
Ship the difference, not the content. Setup already finds the player's own Prey
install, so it can build the pk4 there:

    7 identical   dropped outright. They override retail with identical bytes,
                  so removing them changes nothing. Checked against every pak:
                  none of the others disagree.

    1 binary      guis/assets/loading/feedingtowera.tga - dropped too. It is
                  98.94% identical to retail's, re-encoded 24bpp to 32bpp RLE,
                  with one changed band at x168-854 y858-908: a caption strip.
                  Carrying a pixel delta and a TGA encoder into setup to restore
                  one line of text on one map's loading screen is not worth it.
                  Retail's own caption shows instead.

   23 text        shipped as a patch. .gui, .guifragment, .script, .skin and
                  .md5mesh are all plain text. The patch holds only the lines
                  that differ - lvonasek's work, not Human Head's.

   19 new         shipped as they are. The weapon wheel, the laser sights,
                  vr.mtr, the PK signs: no retail counterpart, nothing owed.

The patch format
----------------
Deliberately simpler than a unified diff, because setup.ps1 has to apply it in
PowerShell with no tools installed. One JSON file holding, per patched file, a
list of operations against the original's lines:

    ["copy", n]        take the next n lines of the original unchanged
    ["skip", n]        drop the next n lines of the original
    ["add", [lines]]   insert these lines

Plus the SHA256 of the original we diffed against, so a mismatched Prey install
fails loudly instead of building a broken pk4.

    python tools/release/make_pk4_patches.py --paks <dir holding pak000.pk4 ...>
"""
import argparse
import difflib
import glob
import hashlib
import json
import os
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_geometry

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
PK4_SRC = os.path.join(REPO, 'app', 'src', 'main', 'pk4')
OUT = os.path.join(HERE, 'pk4patch')

BINARY_EXT = ('.tga', '.dds', '.png', '.jpg', '.wav', '.ogg', '.bik', '.roq')


# The wrench, spirit bow, crawler and hider viewmodels carry Tommy's hands and
# forearms as a separate sub-mesh with its own shader. Flat, the arm is cut off
# below the screen; in VR you look straight at the severed end, and the hand is
# nowhere near the controller. Pointing that one shader at a material of ours
# that draws nothing removes them without remodelling anything.
#
# Synthesised from the player's file rather than diffed against a copy of it,
# so no retail geometry is committed here. See synth_viewhands().
HANDS_SHADER = 'models/weapons/hands/hands'
VIEWHANDS_MATERIAL = 'pcvr/viewhands'
VIEWMODELS_WITH_HANDS = (
    'models/weapons/bow/bow.md5mesh',
    'models/weapons/crawler/crawler.md5mesh',
    'models/weapons/hider/hider.md5mesh',
    'models/weapons/wrench/wrench.md5mesh',
)

# The opaque weapon shells with faces deleted where a flat shooter would never
# show them. Drawn from behind as well, so a VR player does not look into the
# inside of the gun. See synth_twosided().
#
# launchermembrane is deliberately absent: it already carries "//twosided",
# commented out, and it is translucent - drawing a translucent surface twice
# blends it with itself.
TWOSIDED_FILE = 'materials/weapons.mtr'
TWOSIDED_MATERIALS = (
    'models/weapons/autocannon/autocannon',
    'models/weapons/hider/hider',
    'models/weapons/launcher/launcherext',
    'models/weapons/launcher/launcherint',
    'models/weapons/rifle/rifle',
    'models/weapons/rifle/barrel1',
    'models/weapons/rifle/barrel2',
    'models/weapons/rifle/barrel3',
    'models/weapons/soulstripper/Soulstripper',
)

RETAIL_PAKS = ('pak000.pk4', 'pak001.pk4', 'pak002.pk4', 'pak003.pk4',
               'pak004.pk4', 'pak005.pk4', 'pak006.pk4', 'pak020.pk4',
               'pak040.pk4')


def retail_index(paks):
    """name -> (pak, member), from the RETAIL paks only.

    This used to take every .pk4 in the folder with the later name winning,
    which is the engine's load order. It is the wrong rule here. A modded
    install - eleven mods in the folder this was developed against - has
    revelations_demo.pk4 carrying its own copy of
    guis/mainmenu/mainmenu_newgame.guifragment, and "r" sorts after "p", so the
    original being diffed or checked was the mod's file rather than Human
    Head's. The patches are made against retail and apply to retail; what the
    engine loads on top at run time is the player's business.
    """
    index = {}
    for name in RETAIL_PAKS:
        pk = os.path.join(paks, name)
        if not os.path.exists(pk):
            continue
        try:
            z = zipfile.ZipFile(pk)
        except Exception:
            continue
        for n in z.namelist():
            if not n.endswith('/'):
                index[n.lower().replace('\\', '/')] = (pk, n)
    return index


def ops_for(original, ours):
    """difflib opcodes as something PowerShell can walk."""
    a = original.split('\n')
    b = ours.split('\n')
    ops = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == 'equal':
            ops.append(['copy', i2 - i1])
        elif tag == 'delete':
            ops.append(['skip', i2 - i1])
        elif tag == 'insert':
            ops.append(['add', b[j1:j2]])
        else:                                   # replace
            ops.append(['skip', i2 - i1])
            ops.append(['add', b[j1:j2]])
    return ops


def synth_twosided(index):
    """One patch entry adding twoSided to each named material.

    Insists on exactly one declaration per material and an opening brace on the
    line after its name - the shape every one of them has. Anything else means
    this is not the file the line numbers were measured against, and a silently
    misplaced keyword would land in whichever material follows.
    """
    hit = index.get(TWOSIDED_FILE.lower())
    if not hit:
        sys.exit('%s is not in the retail paks' % TWOSIDED_FILE)
    pak, member = hit
    theirs = zipfile.ZipFile(pak).read(member)
    lines = theirs.decode('latin-1').split('\n')

    at = []
    for name in TWOSIDED_MATERIALS:
        found = [i for i, l in enumerate(lines)
                 if l.strip().lower() == name.lower()]
        if len(found) != 1:
            sys.exit('%s: %d declarations of %s' % (TWOSIDED_FILE, len(found), name))
        i = found[0]
        if lines[i + 1].strip() != '{':
            sys.exit('%s: no opening brace after %s' % (TWOSIDED_FILE, name))
        at.append(i + 1)
    at.sort()

    eol = '\r' if lines[at[0]].endswith('\r') else ''
    ops = []
    prev = 0
    for i in at:
        ops.append(['copy', i + 1 - prev])
        ops.append(['add', ['\ttwoSided' + eol]])
        prev = i + 1
    ops.append(['copy', len(lines) - prev])

    return {TWOSIDED_FILE: {
        'from': os.path.basename(pak),
        'sha256': hashlib.sha256(theirs).hexdigest(),
        'synth': 'twosided',
        'ops': ops,
    }}


def synth_models(index):
    """The eight weapon viewmodels: hands blanked, holes capped.

    One entry per mesh because the acid sprayer needs both. Built from the
    player's own file and diffed, so nothing of Human Head's is committed here -
    the patch carries the geometry this generates and nothing else.
    """
    out = {}
    for w in ('autocannon', 'bow', 'crawler', 'hider', 'launcher', 'rifle',
              'soulstripper', 'wrench'):
        rel = 'models/weapons/%s/%s.md5mesh' % (w, w)
        hit = index.get(rel.lower())
        if not hit:
            sys.exit('%s is not in the retail paks' % rel)
        pak, member = hit
        theirs = zipfile.ZipFile(pak).read(member)
        src = theirs.decode('latin-1')

        ours = weapon_geometry.rename_hands(src, VIEWHANDS_MATERIAL)
        ours = weapon_geometry.fill_holes(ours)
        if ours == src:
            continue

        out[rel] = {
            'from': os.path.basename(pak),
            'sha256': hashlib.sha256(theirs).hexdigest(),
            'synth': 'models',
            'ops': ops_for(src, ours),
        }
    return out


def synth_viewhands(index):
    """Patch entries that blank the hands sub-mesh, built from retail itself.

    Refuses anything but exactly one hands shader line per mesh, because a mesh
    that has two, or none, is not the file these line counts were measured
    against and the result would be a silently wrong model.
    """
    out = {}
    for rel in VIEWMODELS_WITH_HANDS:
        hit = index.get(rel.lower())
        if not hit:
            sys.exit('%s is not in the retail paks' % rel)
        pak, member = hit
        theirs = zipfile.ZipFile(pak).read(member)
        lines = theirs.decode('latin-1').split('\n')
        at = [i for i, l in enumerate(lines)
              if l.strip().lower() == 'shader "%s"' % HANDS_SHADER]
        if len(at) != 1:
            sys.exit('%s: expected 1 hands shader line, found %d' % (rel, len(at)))
        i = at[0]
        mine = lines[i].replace(HANDS_SHADER, VIEWHANDS_MATERIAL)
        out[rel] = {
            'from': os.path.basename(pak),
            'sha256': hashlib.sha256(theirs).hexdigest(),
            'synth': 'viewhands',
            'ops': [['copy', i], ['skip', 1], ['add', [mine]],
                    ['copy', len(lines) - i - 1]],
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--paks', required=True, help='directory holding retail pak000.pk4 etc')
    args = ap.parse_args()

    index = retail_index(args.paks)
    if not index:
        sys.exit('no retail paks found in %s' % args.paks)

    names = subprocess.check_output(['git', 'ls-files', 'app/src/main/pk4'],
                                    cwd=REPO, text=True).split()
    if not names:
        sys.exit('git lists nothing under app/src/main/pk4')

    if os.path.exists(OUT):
        import shutil
        shutil.rmtree(OUT)
    os.makedirs(os.path.join(OUT, 'new'))

    manifest = {'new': [], 'patch': {}, 'dropped': []}
    for name in sorted(names):
        rel = name.split('app/src/main/pk4/', 1)[-1]
        ours_bytes = subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=REPO)
        hit = index.get(rel.lower())

        if not hit:
            dest = os.path.join(OUT, 'new', rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            open(dest, 'wb').write(ours_bytes)
            manifest['new'].append(rel)
            continue

        pak, member = hit
        theirs = zipfile.ZipFile(pak).read(member)

        if theirs == ours_bytes:
            manifest['dropped'].append({'file': rel, 'why': 'identical to retail'})
            continue

        if rel.lower().endswith(BINARY_EXT):
            manifest['dropped'].append({'file': rel, 'why': 'binary, derived from retail art'})
            continue

        manifest['patch'][rel] = {
            'from': os.path.basename(pak),
            'sha256': hashlib.sha256(theirs).hexdigest(),
            'ops': ops_for(theirs.decode('latin-1'), ours_bytes.decode('latin-1')),
        }

    manifest['patch'].update(synth_models(index))
    manifest['patch'].update(synth_twosided(index))

    # What this set was generated FROM, so the packager can tell whether it has
    # gone stale. Without it, editing a file under app/src/main/pk4 and then
    # packaging silently ships the PREVIOUS menu - which is exactly what
    # happened: a build was tested twice with no visible change, because
    # the guifragment change never reached the pk4.
    #
    # git's own blob ids, not file hashes, because the patches are generated
    # from HEAD and the working tree is deliberately not consulted.
    ids = subprocess.check_output(['git', 'ls-tree', '-r', 'HEAD', '--',
                                   'app/src/main/pk4'], cwd=REPO, text=True)
    manifest['sources_sha256'] = hashlib.sha256(ids.encode()).hexdigest()

    json.dump(manifest, open(os.path.join(OUT, 'manifest.json'), 'w'), indent=1)

    patched = sum(len(v['ops']) for v in manifest['patch'].values())
    size = sum(os.path.getsize(os.path.join(r, f))
               for r, d, fs in os.walk(OUT) for f in fs)
    print('new files shipped : %d' % len(manifest['new']))
    print('files patched     : %d  (%d operations)' % (len(manifest['patch']), patched))
    print('files dropped     : %d' % len(manifest['dropped']))
    for d in manifest['dropped']:
        print('    %-50s %s' % (d['file'], d['why']))
    print('total on disk     : %.1f KB' % (size / 1024.0))
    print('written to        : %s' % OUT)


if __name__ == '__main__':
    main()
