"""Read everything the public tree says about people, and show it.

audit_public_repo.py answers pass or fail. That is the wrong shape for this
question, because the thing that went wrong last time passed every check that
existed: the release zip was clean and the pushed TREE was not. What is needed
before a push is not a verdict but a list - every line in our own files that
mentions a person, quotes anyone, recounts an exchange, or speaks in the first
person - short enough to read.

Vendored code is skipped. SDL, the GLES headers, id Software's own sources and
the licence texts are not ours, we do not edit them, and their authors' names in
them are correct.

    python tools/release/audit_public_text.py
"""
import io
import os
import re
import subprocess
import sys

PUB = r'E:\Tools\Games\PreyVR-PCVR-public'

TEXT = ('.c', '.cpp', '.h', '.py', '.ps1', '.bat', '.md', '.cfg', '.txt',
        '.gui', '.guifragment', '.mtr', '.skin', '.json', '.script', '.def')

VENDORED = ('app/src/main/jni/SupportLibs/', 'app/src/main/jni/GLES/',
            'app/src/main/jni/d3es-multithread-master/',
            'licenses/', 'COPYING.txt', 'LICENSE')

# This file names the things it looks for, so it would report itself.
SELF = ('tools/release/audit_public_text.py',
        'tools/release/audit_public_repo.py',
        'tools/release/audit_release_zip.py',
        'tools/release/verify_staged.py')

# Everyone who should appear, and only in credit.
PEOPLE = ('lennyguy20', 'lubos', 'lvonasek', 'vonasek', 'drbeef', 'defunkt',
          'baggyg', 'bummser', 'stiefl525', 'glkarin', 'domyoji', 'yodaui',
          'emileb', 'gabrielcuvillier', 'koz', 'carl', 'humanhead',
          'human head', 'id software', 'cheello', 'raydod')

CHECKS = (
    ('names a person', re.compile('|'.join(re.escape(p) for p in PEOPLE), re.I)),
    ('third party asked / said / wanted',
     re.compile(r'\b(asked|request(ed)?|said|told|complain\w*|object\w*|'
                r'insist\w*|refus\w*|agreed|permission|consent)\b', re.I)),
    ('speaks in the first person',
     re.compile(r'(?<![A-Za-z])(I|I\'m|I\'ve|I\'d|my|we|our|us)(?![A-Za-z])')),
    ('about a person', re.compile(r'(?<![A-Za-z])(he|his|him|she|her|they|'
                                  r'their|somebody|someone|nobody)(?![A-Za-z])', re.I)),
    ('quotes speech', re.compile(r'"[A-Z][^"]{15,}"')),
    ('a private channel', re.compile(r'\b(discord|dm|private message|reddit|'
                                     r'sidequest|forum|thread)\b', re.I)),
    ('a personal name or address',
     re.compile(r'\b(miles|ryan\s+moore|gameordie)\b|[\w.+-]+@[\w.-]+', re.I)),
)


def ours(f):
    return (f.lower().endswith(TEXT)
            and not any(f.startswith(v) or f == v for v in VENDORED)
            and f not in SELF)


def is_prose(path, line):
    """Only comments and documents describe people. Code does not."""
    t = line.strip()
    if path.lower().endswith(('.c', '.cpp', '.h')):
        return t.startswith(('//', '*', '/*'))
    if path.lower().endswith(('.py',)):
        return True          # docstrings and comments are most of these files
    if path.lower().endswith(('.ps1', '.bat', '.cfg')):
        return t.startswith(('#', 'rem ', '::', '//', '<#'))
    # Game data formats are code. Only their comments are prose.
    if path.lower().endswith(('.gui', '.guifragment', '.mtr', '.skin',
                              '.script', '.def', '.json')):
        return t.startswith(('//', '/*', '*'))
    return True


def main():
    files = [f.strip() for f in subprocess.check_output(
        ['git', 'ls-files'], cwd=PUB, text=True).split('\n') if f.strip()]
    mine = [f for f in files if ours(f)]

    found = {label: [] for label, _ in CHECKS}
    for f in mine:
        p = os.path.join(PUB, f.replace('/', os.sep))
        try:
            lines = io.open(p, encoding='latin-1', newline='').read().splitlines()
        except Exception:
            continue
        for i, l in enumerate(lines):
            if not is_prose(f, l):
                continue
            for label, rx in CHECKS:
                if rx.search(l):
                    found[label].append('%s:%d  %s' % (f, i + 1, l.strip()[:96]))

    print('%d files are ours; %d are vendored or excluded'
          % (len(mine), len(files) - len(mine)))
    total = 0
    for label, _ in CHECKS:
        hits = found[label]
        total += len(hits)
        print()
        print('== %s: %d ==' % (label, len(hits)))
        for h in hits[:40]:
            print('   ' + h)
        if len(hits) > 40:
            print('   ... and %d more' % (len(hits) - 40))
    print()
    print('%d lines to read. None of this is automatically wrong - crediting '
          'people is required.' % total)
    print('What must not be there: an exchange retold, an opinion about '
          'somebody, or a name used for anything but credit.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
