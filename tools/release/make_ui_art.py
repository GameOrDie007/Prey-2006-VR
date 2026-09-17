"""Draw the port's UI bitmaps from scratch, so they are ours.

PreyVR's weapon sight art - the dot, the circle dot, the crosshair and the beam
- was contributed by others and is not used here. The FEATURE is lvonasek's GPL
code and never went anywhere; only the pictures did, and a dot and a crosshair
are pictures anyone can draw. Everyone who contributed to PreyVR is credited in
the README.

Everything here is generated procedurally by the code below: circles, a ring,
two lines and a gradient. Nothing is traced, sampled or derived from anybody's
artwork, and the names are our own (pcvr/sight_*) rather than theirs so there is
no question about which is which.

Two sets:

  the aim sight   a dot, a ring, a crosshair and a beam gradient
  the weapon wheel  an annulus and a soft glow, measured off the originals so
                  they drop in at the same radii and read the same in the gui

Nothing is traced, sampled or derived from anybody's artwork. The originals
were radial gradients, which is a shape, not a design - the measurements below
are of geometry, the way you would measure a doorway before making a door.

    python tools/release/make_ui_art.py
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'app', 'src', 'main', 'pk4', 'guis', 'assets', 'pcvr_sight')

N = 64


def write_tga(path, px, w, h):
    """Uncompressed 32-bit BGRA TGA, bottom-up - what this engine's loader wants."""
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)
    body = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[y][x]
            body += bytes((b, g, r, a))
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(body)


def blank(w, h):
    return [[(0, 0, 0, 0) for _ in range(w)] for _ in range(h)]


def soft(d, edge, width=1.4):
    """1 inside, 0 outside, a pixel and a half of feather between."""
    return max(0.0, min(1.0, (edge - d) / width + 0.5))


def put(px, x, y, a):
    if a <= 0:
        return
    a = int(255 * min(1.0, a))
    if a > px[y][x][3]:
        px[y][x] = (255, 255, 255, a)


def dot():
    px = blank(N, N)
    c = (N - 1) / 2.0
    for y in range(N):
        for x in range(N):
            d = math.hypot(x - c, y - c)
            put(px, x, y, soft(d, N * 0.10))
    return px


def circle_dot():
    px = blank(N, N)
    c = (N - 1) / 2.0
    ring, thick = N * 0.32, 1.6
    for y in range(N):
        for x in range(N):
            d = math.hypot(x - c, y - c)
            put(px, x, y, soft(d, N * 0.055))                    # centre
            put(px, x, y, soft(abs(d - ring), thick))            # ring
    return px


def cross():
    px = blank(N, N)
    c = (N - 1) / 2.0
    arm_in, arm_out, thick = N * 0.10, N * 0.38, 1.5
    for y in range(N):
        for x in range(N):
            dx, dy = abs(x - c), abs(y - c)
            if arm_in <= dy <= arm_out:
                put(px, x, y, soft(dx, thick))                   # vertical arms
            if arm_in <= dx <= arm_out:
                put(px, x, y, soft(dy, thick))                   # horizontal arms
    return px


def beam(w=16, h=64):
    """A thin bright core fading to the edges; the engine stretches it."""
    px = blank(w, h)
    c = (w - 1) / 2.0
    for y in range(h):
        for x in range(w):
            t = abs(x - c) / c if c else 0.0
            a = max(0.0, 1.0 - t) ** 2.2
            if a > 0.004:
                px[y][x] = (255, 255, 255, int(255 * a))
    return px


# --- the weapon wheel -------------------------------------------------------
# Radii and colours measured off PreyVR's own bitmaps, so these drop straight
# into guis/vr_weapon_wheel.gui without touching the layout: the ring runs from
# 0.196 to 0.495 of the width, blue-grey fading darker outward at alpha ~228;
# the glow is flat white alpha 136 out to 0.27 and gone by 0.47.

def smoothstep(a, b, x):
    if b == a:
        return 0.0
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def wheel(size=1024):
    px = blank(size, size)
    c = (size - 1) / 2.0
    r_in, r_out = 0.196 * size, 0.495 * size
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c, y - c)
            a = min(soft(d, r_out, 3.0), 1.0 - soft(d, r_in, 3.0))
            if a <= 0.004:
                continue
            t = max(0.0, min(1.0, (d - r_in) / (r_out - r_in)))
            px[y][x] = (int(61 + (41 - 61) * t),
                        int(71 + (49 - 71) * t),
                        int(116 + (81 - 116) * t),
                        int(228 * a))
    return px


def wheel_glow(size=512):
    px = blank(size, size)
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c, y - c) / size
            a = 136 * (1.0 - smoothstep(0.27, 0.47, d))
            if a > 1:
                px[y][x] = (255, 255, 255, int(a))
    return px


def main():
    os.makedirs(OUT, exist_ok=True)
    made = []
    # the weapon wheel lives beside the other gui assets, not in pcvr_sight/
    wdir = os.path.dirname(OUT)
    for nm, fn, sz in (('weapon_wheel', wheel, 1024), ('weapon_wheel_glow', wheel_glow, 512)):
        q = os.path.join(wdir, nm + '.tga')
        write_tga(q, fn(sz), sz, sz)
        print('  %-21s %dx%-4d %7d bytes' % (nm + '.tga', sz, sz, os.path.getsize(q)))
    for name, px, w, h in (('dot', dot(), N, N),
                           ('circledot', circle_dot(), N, N),
                           ('cross', cross(), N, N),
                           ('beam', beam(), 16, 64)):
        p = os.path.join(OUT, name + '.tga')
        write_tga(p, px, w, h)
        made.append((name, w, h, os.path.getsize(p)))
    for n, w, h, sz in made:
        print('  %-11s %dx%-4d %5d bytes' % (n + '.tga', w, h, sz))
    print('\ndrawn from scratch into %s' % os.path.relpath(OUT, REPO))


if __name__ == '__main__':
    main()
