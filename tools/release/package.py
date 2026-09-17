#!/usr/bin/env python3
"""Assemble the PCVR release, and refuse to build one that carries game data.

What goes in, and where it comes from:

    PreyVR.exe, gamex86_64.dll      build/Release  (the build must be current)
    the eight runtime DLLs          the play directory (PREYVR_RUN), the exact
                                    files every headset run used; their SHA256
                                    is printed so the provenance is on record
    tools/pk4patch/                 the DIFFERENCE, not the content: 23 text
                                    patches against retail originals plus 19
                                    files with no retail counterpart. Setup
                                    builds preybase/vr_support.pk4 from these
                                    and the player's own Prey install. The
                                    release carries no game data at all.
    saves/preybase/preyconfig.cfg   app/src/main/assets/preyconfig.cfg with the
                                    PC settings applied - the first-run config,
                                    exactly the way the Quest launcher seeds it
    Play PreyVR.bat, Play PreyVR flatscreen.bat, README.txt, licences

What may NOT go in: any other .pk4, any .pdb, anything not named below. The
retail data belongs to its publisher, and the player supplies their own copy.
The audit at the end is the rule, and it refuses rather than warns.

Usage:  python tools/release/package.py [--version 1.0] [--out dist]

    PREYVR_RUN   the play directory holding the DLLs (default E:\\Games\\PreyVR-PCVR)
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(REPO, 'build', 'Release')
PK4_SRC = os.path.join(REPO, 'app', 'src', 'main', 'pk4')
CFG_SRC = os.path.join(REPO, 'app', 'src', 'main', 'assets', 'preyconfig.cfg')
RUN = os.environ.get('PREYVR_RUN', r'E:\Games\PreyVR-PCVR')

BINARIES = ['PreyVR.exe', 'gamex86_64.dll']
DLLS = ['SDL2.dll', 'OpenAL32.dll', 'libjpeg-8.dll', 'libogg-0.dll',
        'libvorbis-0.dll', 'libvorbisfile-3.dll', 'zlib1.dll', 'openxr_loader.dll']
# "TEST - even world motion.bat" is deliberately absent: it is an A/B arm for
# pcvr_frameLockedTics from the frame-pacing work, and a release is not the
# place for a diagnostic that asks the player to compare two builds.
LAUNCHERS = ['Play PreyVR.bat', 'Play PreyVR flatscreen.bat', 'Setup.bat',
             'Update.bat', 'Collect report.bat']
TOOLS = ['setup.ps1', 'buildpk4.ps1', 'update.ps1']
TEXT = ['README.txt', 'LICENSE', 'COPYING.txt', 'version.txt']
SAVES_ALLOWED = ['config.spec', 'preyconfig.cfg', 'autoexec.cfg']

# The PC first-run settings, applied over lvonasek's Quest config. Every one
# of these was checked to have readers in this tree (doc/pc-graphics-settings.md)
# and measured on the headset (PROGRESS.md, the frame-rate sections).
PC_SETTINGS = {
    # Everything at maximum, by design: "It's a 20 year old game,
    # modern hardware shouldn't have any issue running it." Every value here was
    # checked to be a cvar this engine actually declares - the 2006 machine-spec
    # preset also writes image_useCompression, image_lodbias,
    # image_usePrecompressedTextures and image_useNormalCompression, none of
    # which exist in the d3es renderer, so they are omitted rather than shipped
    # as clutter that does nothing.

    # --- effects the VR options menu can turn down. All three already default
    # to 1, but they are archived and the menu writes them, so the shipped
    # config states them rather than relying on a default surviving.
    'g_decals':               '1',
    'g_muzzleFlash':          '1',
    'g_projectileLights':     '1',

    # r_skipNewAmbient and r_swapInterval used to be here. Both are declared
    # and read by nothing in this engine - the same as the four image_* keys
    # the 2006 preset writes. Shipping them stated a preference the game
    # cannot act on. lvonasek's options page still offers r_skipNewAmbient as
    # a control; it does nothing either.

    # --- lighting and shading
    'r_shadows':              '1',
    'r_skipBump':             '0',     # normal maps
    'r_skipSpecular':         '0',
    'r_usePhong':             '1',

    # --- bloom. The radius no longer changes the brightness, so these are
    # independent: 3 is the widest blur, 0.2 is the brightness that has been
    # playing at. See game_playerview.cpp.
    'r_skipBloomFX':          '0',
    'pcvr_bloomRange':        '3',
    'pcvr_bloomScale':        '0.2',

    # --- textures. The quality slider resets anisotropy to 8; this is 16.
    'image_anisotropy':       '16',
    'image_filter':           'GL_LINEAR_MIPMAP_LINEAR',
    'image_downSize':         '0',
    'image_downSizeBump':     '0',
    'image_downSizeSpecular': '0',
    'image_preload':          '1',

    # --- detection, so a first launch does not decide these itself
    'com_machineSpec':        '3',
    'com_videoRam':           '4096',

    # --- VR
    'vr_refreshrate':         '90',    # VDXR ignores the request harmlessly
    'vr_msaa':                '0',     # does not exist on this path either way
    'vr_supersampling':       '1.0',   # 1.0 IS native - the runtime's own size
    'vr_turnmode':            '1',     # deliberate: smooth turning
    'pcvr_smoothTurnSpeed':   '130',

    # --- portals at any range, which is the shipped choice. The one max setting
    # here that has never been tested with a working build: 0.9.20 shipped it
    # and froze, but that was the logging bug in the render backend, not this.
    # pcvr_portalDistance 1 is retail if it costs too much in the Feeding Tower.
    'pcvr_portalDistance':    '0',

    # --- one world update per displayed frame. Not a quality setting: the
    # clock-driven default runs the world at 90.909 Hz against a 90 Hz display
    # and 6-7% of frames get two updates or none. Measured 0.15% uneven with
    # this on. Costs 1.5% of world speed, which is constant and should be
    # invisible. pcvr_frameLockedTics 0 restores the old behaviour.
    'pcvr_frameLockedTics':   '1',

    # measured worse in the headset and felt worse; see Common.cpp
    'pcvr_evenTics':          '0',
}

TOP_ALLOWED = set(BINARIES + DLLS + LAUNCHERS + TEXT)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def build_pk4(dest):
    """Zip app/src/main/pk4 the way the Android build does: every tracked
    file, paths with forward slashes, deflated. Returns {arcname: crc}.

    The bytes come from git's HEAD, not the working tree. Five text files
    (.gui, .md5mesh) come out of a Windows checkout with CRLF endings, and
    the pk4 in lvonasek's own APK carries them LF - taking the committed
    blob makes this pk4 match the Quest build's file for file, which was verified by CRC
    against the one the Quest build ships."""
    rel = 'app/src/main/pk4'
    names = subprocess.check_output(['git', 'ls-files', rel], cwd=REPO, text=True).split()
    if not names:
        sys.exit('git lists nothing under %s' % rel)
    crcs = {}
    with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in sorted(names):
            data = subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=REPO)
            z.writestr(name[len(rel) + 1:], data)
        for info in z.infolist():
            crcs[info.filename] = info.CRC
    return crcs


def build_config(dest):
    """lvonasek's shipped config with the PC settings applied. A line that
    exists is changed in place; one that does not is appended."""
    lines = open(CFG_SRC, encoding='latin-1').read().splitlines()
    seen = set()
    out = []
    for ln in lines:
        m = re.match(r'^seta\s+(\S+)\s+', ln)
        if m and m.group(1) in PC_SETTINGS:
            ln = 'seta %s "%s"' % (m.group(1), PC_SETTINGS[m.group(1)])
            seen.add(m.group(1))
        out.append(ln)
    for k, v in PC_SETTINGS.items():
        if k not in seen:
            out.append('seta %s "%s"' % (k, v))
    with open(dest, 'w', encoding='latin-1', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    return len(seen), len(PC_SETTINGS) - len(seen)


def audit(root):
    """Return complaints. Empty means it is safe to ship."""
    bad = []
    for entry in sorted(os.listdir(root)):
        p = os.path.join(root, entry)
        if os.path.isdir(p):
            if entry == 'preybase':
                # Empty now, and it must stay empty. vr_support.pk4 used to sit
                # here and carried 31 files derived from retail Prey.
                for n in os.listdir(p):
                    bad.append('preybase/%s is not ours to ship' % n)
            elif entry == 'saves':
                for r, d, fs in os.walk(p):
                    for n in fs:
                        if n not in SAVES_ALLOWED:
                            bad.append('saves/ carries %s' % n)
            elif entry == 'licenses':
                pass
            elif entry == 'tools':
                for n in os.listdir(p):
                    if n == 'pk4patch':
                        continue
                    if n not in TOOLS:
                        bad.append('tools/%s is not part of the release' % n)
            else:
                bad.append('unexpected directory %s' % entry)
        else:
            if entry not in TOP_ALLOWED:
                bad.append('unexpected file %s' % entry)
            if entry.lower().endswith(('.pk4', '.pdb')):
                bad.append('%s must not ship' % entry)
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', default=None, help='release version; default from git describe')
    ap.add_argument('--out', default=os.path.join(REPO, 'dist'))
    ap.add_argument('--release', action='store_true',
                    help='allow a 1.x version - only for the published build')
    args = ap.parse_args()

    version = args.version
    if version and re.match(r'^[1-9]', version) and not args.release:
        sys.exit('%s looks like a release number, and this port has not '
                 'shipped yet.\nUse a 0.x version for a test build, or pass '
                 '--release if it really is the published one.' % version)
    if not version:
        try:
            version = subprocess.check_output(
                ['git', 'describe', '--tags', '--always'], cwd=REPO, text=True).strip()
        except (OSError, subprocess.CalledProcessError):
            version = 'dev'

    name = 'PreyVR-PCVR-%s' % version
    dest = os.path.join(args.out, name)
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(os.path.join(dest, 'preybase'))
    os.makedirs(os.path.join(dest, 'saves', 'preybase'))

    # the binaries must be the current build, never the play directory's copy
    for b in BINARIES:
        src = os.path.join(BUILD, b)
        if not os.path.exists(src):
            sys.exit('no %s in %s - build first' % (b, BUILD))
        shutil.copy2(src, dest)
    for d in DLLS:
        src = os.path.join(RUN, d)
        if not os.path.exists(src):
            sys.exit('no %s in %s (set PREYVR_RUN)' % (d, RUN))
        shutil.copy2(src, dest)

    # config.spec. The engine only checks whether this file exists: when it is
    # missing it decides the machine is a first launch, runs its own hardware
    # detection, and writes the resulting quality preset OVER the settings
    # below as archived cvars. That is a 2004 auto-detect making decisions
    # about a 2026 GPU, and it also forces a sound restart during init.
    # Shipping the file means a first run uses the settings in this release,
    # which are the ones that were measured. Deleting it re-enables detection.
    # The user's own settings file. Every line is commented out, so it does
    # nothing until someone edits it - but it is read after preyconfig.cfg, so
    # anything they turn on wins, and nothing we ship overwrites their choice.
    # The engine execs autoexec.cfg at startup (Common.cpp).
    shutil.copy2(os.path.join(REPO, 'release', 'autoexec.cfg'),
                 os.path.join(dest, 'saves', 'preybase', 'autoexec.cfg'))

    with open(os.path.join(dest, 'saves', 'preybase', 'config.spec'), 'w') as f:
        f.write('')

    # Which build this is, in the folder and in the report zip. A log arrived
    # from another machine and the only thing identifying the build was the
    # timestamp inside it; a file costs nothing and answers it outright.
    with open(os.path.join(dest, 'version.txt'), 'w') as f:
        f.write('PreyVR PCVR %s\n' % version)
        f.write('packaged %s\n' % time.strftime('%Y-%m-%d %H:%M'))
        f.write('PreyVR.exe      sha256 %s\n'
                % sha256(os.path.join(dest, 'PreyVR.exe')))
        f.write('gamex86_64.dll  sha256 %s\n'
                % sha256(os.path.join(dest, 'gamex86_64.dll')))

    # Is the binary we are about to ship actually built from this source?
    #
    # Nothing checked this, and it cost a test round: pcvr_mirror's default was
    # changed 0 -> 1, committed, packaged and shipped, and the exe was eight
    # minutes older than the edit. The setting was right in the source and
    # absent from the game. verify_staged.py did not catch it either - it
    # compares the staged exe against build/Release, and both were stale.
    # Two binaries, and they are not built from the same sources. Everything
    # under neo/game goes into gamex86_64.dll (game.vcxproj); the rest goes into
    # PreyVR.exe. Comparing every source against the exe reports the game code
    # as stale every time the dll alone is rebuilt, which is most of the time.
    BINS = [('build/Release/gamex86_64.dll',
             lambda d: os.path.sep + 'game' + os.path.sep in d),
            ('build/Release/PreyVR.exe',
             lambda d: os.path.sep + 'game' + os.path.sep not in d)]
    newer = []
    for relbin, belongs in BINS:
        binp = os.path.join(REPO, relbin)
        if not os.path.exists(binp):
            continue
        built = os.path.getmtime(binp)
        for sub in ('app/src/main/jni', 'neo'):
            base = os.path.join(REPO, sub)
            for dirpath, dirnames, filenames in os.walk(base):
                if 'SupportLibs' in dirpath or not belongs(dirpath + os.path.sep):
                    continue
                for fn in filenames:
                    if fn.lower().endswith(('.c', '.cpp', '.h')):
                        f = os.path.join(dirpath, fn)
                        if os.path.getmtime(f) > built:
                            newer.append('%s (-> %s)' % (os.path.relpath(f, REPO),
                                                         os.path.basename(relbin)))
    if True:
        if newer:
            sys.exit('%d source file(s) are NEWER than build/Release/PreyVR.exe, so '
                     'this package would ship a binary without your change. Rebuild '
                     'first. Newest: %s'
                     % (len(newer), ', '.join(sorted(newer)[:4])))

    # No vr_support.pk4. 31 of its 50 files are derived from retail Prey and
    # are not ours to redistribute; setup builds it on the player's machine
    # from their own paks plus the patch set below. See make_pk4_patches.py.
    os.makedirs(os.path.join(dest, 'tools'))
    patchsrc = os.path.join(HERE, 'pk4patch')
    if not os.path.exists(os.path.join(patchsrc, 'manifest.json')):
        sys.exit('no patch set - run tools/release/make_pk4_patches.py first')

    # And refuse a set that is out of date, which is worse than a missing one:
    # a missing set stops the build, a stale set ships quietly and the change
    # you just made is simply not in the game. That cost two test rounds
    # - the menu edit was committed, built and staged, and the previous
    # menu both times, because the pk4 is built from these patches and nothing
    # regenerated them.
    import json as _json
    _m = _json.load(open(os.path.join(patchsrc, 'manifest.json')))
    _ids = subprocess.check_output(['git', 'ls-tree', '-r', 'HEAD', '--',
                                    'app/src/main/pk4'], cwd=REPO, text=True)
    _now = hashlib.sha256(_ids.encode()).hexdigest()
    if _m.get('sources_sha256') != _now:
        sys.exit('the pk4 patch set is STALE - app/src/main/pk4 has changed '
                 'at HEAD since it was generated, so packaging now would ship '
                 'the previous content. Run: python tools/release/make_pk4_patches.py --paks <preybase>')
    shutil.copytree(patchsrc, os.path.join(dest, 'tools', 'pk4patch'))
    crcs = {}
    changed, added = build_config(os.path.join(dest, 'saves', 'preybase', 'preyconfig.cfg'))

    for l in LAUNCHERS:
        shutil.copy2(os.path.join(REPO, 'release', l), dest)
    for t in TOOLS:
        shutil.copy2(os.path.join(REPO, 'release', 'tools', t),
                     os.path.join(dest, 'tools'))
    shutil.copy2(os.path.join(REPO, 'README.md'), os.path.join(dest, 'README.txt'))
    shutil.copy2(os.path.join(REPO, 'LICENSE'), dest)
    shutil.copy2(os.path.join(REPO, 'COPYING.txt'), dest)
    shutil.copytree(os.path.join(REPO, 'licenses'), os.path.join(dest, 'licenses'))

    bad = audit(dest)
    if bad:
        for b in bad:
            print('REFUSED:', b)
        shutil.rmtree(dest)
        sys.exit(1)

    zpath = os.path.join(args.out, name + '.zip')
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(dest):
            dirs.sort()
            for f in sorted(files):
                full = os.path.join(root, f)
                z.write(full, os.path.join(name, os.path.relpath(full, dest)).replace(os.sep, '/'))

    print('release %s' % name)
    print('  vr_support.pk4: built by setup, not shipped')
    print('  Setup.bat + tools/setup.ps1 included')
    print('  preyconfig.cfg: %d settings changed, %d added' % (changed, added))
    print('  %s  %.1f MB' % (zpath, os.path.getsize(zpath) / 1e6))
    print()
    print('sha256')
    for root, dirs, files in os.walk(dest):
        dirs.sort()
        for f in sorted(files):
            full = os.path.join(root, f)
            print('  %s  %s' % (sha256(full), os.path.relpath(full, dest).replace(os.sep, '/')))
    print('  %s  %s' % (sha256(zpath), os.path.basename(zpath)))


if __name__ == '__main__':
    main()
