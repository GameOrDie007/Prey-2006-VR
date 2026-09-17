"""Rebuild the public tree from HEAD, with no history to mine.

The repository people clone is not this one. This one has a working history:
every wrong turn, every message quoted into a commit body, every path on the
machine it was built on. A public fork needs the CODE, not the diary - so the
public tree is a fresh git repository with exactly one commit, rebuilt from
`git archive HEAD` whenever it is published.

That also means nothing can leak through an old commit. The first release went
out with twelve model files in its tree that the release zip did not contain,
and taking the repository private afterwards does not unpublish a commit.
Rebuilding from scratch is the only version of "removed" that is true.

What is dropped, and why:

    PROGRESS.md                 the working record. Thousands of lines about
                                what failed and who said what.
    doc/headset-test-session.md  test notes, written to a person.
    local.properties            an Android SDK path from this machine.
    2ms                         a scratch file.
    tools/build/                batch files with this machine's Visual Studio
                                path hard-coded.
    tools/headless/             harnesses that only make sense here.

    python tools/release/make_public_tree.py
    python tools/release/audit_public_repo.py      <- then this, before pushing
"""
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
PUB = r'E:\Tools\Games\PreyVR-PCVR-public'

# The identity the published commit carries. A noreply address on purpose: a
# public commit should not hand anyone a personal mailbox.
AUTHOR = 'Ryan Moore'
EMAIL = 'GameOrDie007@users.noreply.github.com'
MESSAGE = 'PreyVR PCVR'

# The only remote this tree may have. Anything else is somebody's directory.
EXPECTED_REMOTE = 'GameOrDie007/Prey-2006-VR'

DROP_FILES = ('PROGRESS.md', 'local.properties', '2ms',
              'doc/headset-test-session.md')
DROP_DIRS = ('tools/build/', 'tools/headless/')


def run(*a, **kw):
    return subprocess.check_output(list(a), text=True, **kw)


def main():
    if not os.path.isdir(os.path.join(REPO, '.git')):
        sys.exit('%s is not a git repository' % REPO)

    # Refuse to touch a tree that is not ours to rebuild. A public tree has no
    # remote and no uncommitted work; anything else is somebody's directory.
    if os.path.isdir(os.path.join(PUB, '.git')):
        # Once published, this tree legitimately has our own repository as its
        # remote, and rebuilding it is how a correction gets out. Any OTHER
        # remote means this is not the directory we think it is.
        remotes = run('git', 'remote', '-v', cwd=PUB)
        for line in remotes.splitlines():
            if line.strip() and EXPECTED_REMOTE not in line:
                sys.exit('%s points at %s - not rebuilding it blind'
                         % (PUB, line.split()[1] if len(line.split()) > 1 else '?'))
        if run('git', 'status', '--porcelain', cwd=PUB).strip():
            sys.exit('%s has uncommitted changes - look at them first' % PUB)

    head = run('git', 'rev-parse', '--short', 'HEAD', cwd=REPO).strip()
    print('rebuilding the public tree from %s' % head)

    if os.path.exists(PUB):
        # git makes its object files read-only, and Windows enforces that on
        # unlink. Clear the bit and retry rather than leaving half a tree.
        def _force(func, path, _exc):
            os.chmod(path, 0o700)
            func(path)
        # Empty it rather than remove it. Something as ordinary as a shell
        # sitting in the directory makes Windows refuse to unlink the root,
        # and by then everything under it is already gone.
        for name in os.listdir(PUB):
            p = os.path.join(PUB, name)
            if os.path.isdir(p) and not os.path.islink(p):
                shutil.rmtree(p, onexc=_force)
            else:
                _force(os.unlink, p, None)
    else:
        os.makedirs(PUB)

    with tempfile.TemporaryDirectory() as tmp:
        tar = os.path.join(tmp, 'head.tar')
        with open(tar, 'wb') as f:
            subprocess.check_call(['git', 'archive', '--format=tar', 'HEAD'],
                                  cwd=REPO, stdout=f)
        with tarfile.open(tar) as t:
            t.extractall(PUB)

    dropped = 0
    for rel in DROP_FILES:
        p = os.path.join(PUB, rel.replace('/', os.sep))
        if os.path.exists(p):
            os.remove(p)
            dropped += 1
    for rel in DROP_DIRS:
        p = os.path.join(PUB, rel.replace('/', os.sep).rstrip(os.sep))
        if os.path.isdir(p):
            dropped += sum(len(fs) for _r, _d, fs in os.walk(p))
            shutil.rmtree(p)
    print('dropped %d file(s) that belong only here' % dropped)

    run('git', 'init', '-q', cwd=PUB)
    run('git', 'remote', 'add', 'origin',
        'https://github.com/%s.git' % EXPECTED_REMOTE, cwd=PUB)
    run('git', 'config', 'user.name', AUTHOR, cwd=PUB)
    run('git', 'config', 'user.email', EMAIL, cwd=PUB)
    run('git', 'add', '-A', cwd=PUB)
    run('git', 'commit', '-q', '-m', MESSAGE, cwd=PUB)

    n = run('git', 'ls-files', cwd=PUB).strip().count('\n') + 1
    print('%s: %d files, 1 commit, %s <%s>' % (PUB, n, AUTHOR, EMAIL))
    print()
    print('now run:  python tools/release/audit_public_repo.py')
    return 0


if __name__ == '__main__':
    sys.exit(main())
