"""Close the holes in Prey's weapon viewmodels, without disturbing the rig.

WHY

A flat shooter shows a viewmodel from one angle, so the artist deleted the faces
it could never reach - 56 separate holes on the auto cannon, 62 on the acid
sprayer, 20 on the rifle. In VR you look into them. Measured by ray casting from
the angles a player actually holds a weapon at, 13 to 27% of what you see is the
inside of the model, and up to 75% at the worst angle.

Marking the materials twoSided does not fix it: this renderer's interaction pass
never sets the cull mode from the shader, it inherits CT_FRONT_SIDED, so back
faces write depth and take ambient but receive no light. They come out black,
which in a dark room is indistinguishable from the hole.

So the holes get closed with real geometry, which lights normally because its
normals point out.

WHAT THIS DOES NOT DO

It does not touch a single existing vertex, weight, triangle or joint. That is
the whole design. The last attempt at this mirrored every triangle in any mesh
that had a hole and it failed in the headset three ways at once - z-fighting
from coincident faces, the player's hands reappearing, and every weapon sitting
in the wrong place - because the thing that positions geometry in a skinned
model is the WEIGHT data, and it had never been compared. Here the existing mesh
is copied through untouched and new triangles are appended after it.

HOW A HOLE IS CLOSED

Per surface:

  1. every vertex is resolved to its bind-pose position - the sum over its
     weights of bias * (jointRotation * weightPos + jointPos) - and welded by
     position. md5mesh duplicates a vertex wherever the UVs or smoothing split,
     so counting boundaries by vertex INDEX finds thousands of seams that are
     not holes. This is the mistake the previous attempt was built on.

  2. edges with one triangle instead of two are real boundary. They are chained
     head to tail into closed loops.

  3. each loop gets one new vertex at its centroid and a triangle fan. The new
     vertex carries a single weight, bias 1.0, on whichever joint most of the
     loop already belongs to, with its position expressed in that joint's local
     space - so it is placed exactly at the centroid in bind pose and follows
     that bone thereafter. Its UV is the average of the loop's.

  4. winding follows the neighbour. A boundary edge appears in exactly one
     triangle, directed a->b; the triangle closing it must contain b->a, so the
     fan emits (b, a, centre). That keeps the cap facing the same way as the
     surface it joins, which is what makes it light correctly.

Loops of fewer than three edges are skipped, and so are surfaces that are single
sided sheets by intent - glass, fluid tubes, the scope beam, the ammo panel.
Closing those would be a bug, not a fix.
"""
import math
import re

# Single-sided by intent. A flat card reads as 100% "see-through" and is right.
FLAT = ('glass', 'mask', 'fluid', 'beam', 'gui', 'snot', 'cover',
        'hands/hands', 'viewhands')

WELD = 1e-4          # model units

# Holes up to this many edges are fanned from one of their own vertices, so
# every corner carries a real texture coordinate. Bigger ones get a centre.
FAN_FROM_VERTEX_MAX = 24

# A pipe has no middle: every vertex is on one of its two rims. The rifle's
# barrels are 20 of 20; the wrench, which also has exactly two holes, is 18
# of 419. Open by design - do not cap.
TUBE_BOUNDARY_FRACTION = 0.8


# --------------------------------------------------------------------- maths
def _quat(x, y, z):
    w = 1.0 - x * x - y * y - z * z
    return (x, y, z, -math.sqrt(w) if w > 0 else 0.0)


def _rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    ix = w * vx + y * vz - z * vy
    iy = w * vy + z * vx - x * vz
    iz = w * vz + x * vy - y * vx
    iw = -x * vx - y * vy - z * vz
    return (ix * w + iw * -x + iy * -z - iz * -y,
            iy * w + iw * -y + iz * -x - ix * -z,
            iz * w + iw * -z + ix * -y - iy * -x)


def _unrot(q, v):
    """Rotate by the inverse of q - the conjugate, q being unit length."""
    return _rot((-q[0], -q[1], -q[2], q[3]), v)


def parse_joints(text):
    m = re.search(r'joints\s*\{(.*?)\n\}', text, re.S)
    out = []
    for line in m.group(1).splitlines():
        g = re.match(r'\s*"[^"]*"\s+(-?\d+)\s*\(\s*(\S+)\s+(\S+)\s+(\S+)\s*\)'
                     r'\s*\(\s*(\S+)\s+(\S+)\s+(\S+)\s*\)', line)
        if g:
            out.append(((float(g.group(2)), float(g.group(3)), float(g.group(4))),
                        _quat(float(g.group(5)), float(g.group(6)), float(g.group(7)))))
    return out


def mesh_spans(text):
    """(start, end) of every mesh { ... } block, in order."""
    out = []
    for m in re.finditer(r'\bmesh\s*\{', text):
        depth = 0
        for k in range(text.index('{', m.start()), len(text)):
            if text[k] == '{':
                depth += 1
            elif text[k] == '}':
                depth -= 1
                if depth == 0:
                    out.append((m.start(), k + 1))
                    break
    return out


# ------------------------------------------------------------------ the work
def _outward_fraction(pos, tris):
    """How much of a surface faces away from its own centre.

    A shell has most triangles pointing outward; a modelled interior points in.
    md5mesh winding is the reverse of cross(v1-v0, v2-v0), so the outward normal
    is cross(v2-v0, v1-v0).
    """
    mids = []
    nrms = []
    for a, b, c in tris:
        pa, pb, pc = pos[a], pos[b], pos[c]
        e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        nrms.append((e2[1] * e1[2] - e2[2] * e1[1],
                     e2[2] * e1[0] - e2[0] * e1[2],
                     e2[0] * e1[1] - e2[1] * e1[0]))
        mids.append(((pa[0] + pb[0] + pc[0]) / 3.0,
                     (pa[1] + pb[1] + pc[1]) / 3.0,
                     (pa[2] + pb[2] + pc[2]) / 3.0))
    cx = sum(m[0] for m in mids) / len(mids)
    cy = sum(m[1] for m in mids) / len(mids)
    cz = sum(m[2] for m in mids) / len(mids)
    n = 0
    for nr, m in zip(nrms, mids):
        if (nr[0] * (m[0] - cx) + nr[1] * (m[1] - cy) + nr[2] * (m[2] - cz)) > 0:
            n += 1
    return float(n) / len(tris)


def _loops(tris, weld):
    """Closed boundary loops, as lists of directed welded-index edges."""
    count = {}
    directed = []
    for a, b, c in tris:
        wa, wb, wc = weld[a], weld[b], weld[c]
        for e in ((wa, wb), (wb, wc), (wc, wa)):
            k = (min(e), max(e))
            count[k] = count.get(k, 0) + 1
            directed.append(e)

    bound = [e for e in directed if count[(min(e), max(e))] == 1]
    nxt = {}
    for a, b in bound:
        nxt.setdefault(a, []).append(b)

    used = set()
    out = []
    for a0, bs in nxt.items():
        for b0 in bs:
            if (a0, b0) in used:
                continue
            loop = []
            a, b = a0, b0
            while True:
                used.add((a, b))
                loop.append((a, b))
                cand = [x for x in nxt.get(b, []) if (b, x) not in used]
                if not cand:
                    break
                a, b = b, cand[0]
                if a == a0:
                    break
            if len(loop) >= 3:
                out.append(loop)
    return out


def fill_holes(text, report=None):
    """Return the md5mesh with every real hole capped. Existing data untouched."""
    J = parse_joints(text)
    out = text
    shift = 0

    for s, e in mesh_spans(text):
        blk = text[s:e]
        shader = re.search(r'shader\s+"([^"]+)"', blk).group(1)
        if any(k in shader.lower() for k in FLAT):
            continue

        verts = [(float(g[1]), float(g[2]), int(g[3]), int(g[4])) for g in
                 re.findall(r'vert\s+(\d+)\s*\(\s*(\S+)\s+(\S+)\s*\)\s+(\d+)\s+(\d+)', blk)]
        tris = [tuple(int(x) for x in g[1:]) for g in
                re.findall(r'tri\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', blk)]
        wts = [(int(g[1]), float(g[2]), (float(g[3]), float(g[4]), float(g[5])))
               for g in re.findall(
                   r'weight\s+(\d+)\s+(\d+)\s+(\S+)\s*\(\s*(\S+)\s+(\S+)\s+(\S+)\s*\)', blk)]
        if not verts or not tris:
            continue

        pos = []
        for _u, _v, ws, wc in verts:
            px = py = pz = 0.0
            for k in range(ws, ws + wc):
                ji, bias, wp = wts[k]
                jp, jq = J[ji]
                r = _rot(jq, wp)
                px += (jp[0] + r[0]) * bias
                py += (jp[1] + r[1]) * bias
                pz += (jp[2] + r[2]) * bias
            pos.append((px, py, pz))

        key = {}
        weld = []
        rep = {}
        for i, p in enumerate(pos):
            k = (round(p[0] / WELD), round(p[1] / WELD), round(p[2] / WELD))
            if k not in key:
                key[k] = len(key)
                rep[key[k]] = i
            weld.append(key[k])

        # Surfaces that are inside-out BY DESIGN must not be capped. The
        # launcher's barrel interior is a tube whose faces point inward because
        # the player is meant to see into it; a lid over its mouth made it
        # measurably worse. Judged geometrically rather than by name - it sits
        # at 12% of triangles facing away from the surface's own centre, and
        # the next lowest in the whole weapon set is 52%.
        out_frac = _outward_fraction(pos, tris)
        if out_frac < 0.35:
            if report is not None:
                report.append((shader, 0, 0, 'inside-out by design, left alone'))
            continue

        loops = _loops(tris, weld)
        if not loops:
            continue

        # An open tube - a barrel - is not a defect.
        on_boundary = set()
        for L in loops:
            for a, b in L:
                on_boundary.add(a)
                on_boundary.add(b)
        if len(loops) == 2 and len(on_boundary) >= TUBE_BOUNDARY_FRACTION * len(key):
            if report is not None:
                report.append((shader, 0, 0, 'open tube, left alone'))
            continue

        new_verts, new_wts, new_tris = [], [], []
        nv, nw, nt = len(verts), len(wts), len(tris)

        for loop in loops:
            ids = [a for a, _b in loop]

            # Small hole: fan from one of its own vertices. No new vertex, so
            # every corner keeps a real texture coordinate and the cap samples
            # the surrounding texture instead of one averaged colour.
            if len(loop) <= FAN_FROM_VERTEX_MAX:
                v = [rep[i] for i in ids]
                for i in range(1, len(v) - 1):
                    new_tris.append('\ttri %d %d %d %d'
                                    % (nt, v[0], v[i + 1], v[i]))
                    nt += 1
                continue

            pts = [pos[rep[i]] for i in ids]
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            cz = sum(p[2] for p in pts) / len(pts)
            uu = sum(verts[rep[i]][0] for i in ids) / len(ids)
            vv = sum(verts[rep[i]][1] for i in ids) / len(ids)

            # whichever joint most of this loop already belongs to
            tally = {}
            for i in ids:
                _u, _v, ws, wc = verts[rep[i]]
                for k in range(ws, ws + wc):
                    ji, bias, _wp = wts[k]
                    tally[ji] = tally.get(ji, 0.0) + bias
            ji = max(tally, key=tally.get)
            jp, jq = J[ji]
            local = _unrot(jq, (cx - jp[0], cy - jp[1], cz - jp[2]))

            new_verts.append('\tvert %d ( %f %f ) %d 1' % (nv, uu, vv, nw))
            new_wts.append('\tweight %d %d 1.000000 ( %f %f %f )'
                           % (nw, ji, local[0], local[1], local[2]))
            centre = nv
            nv += 1
            nw += 1

            # the neighbour owns a->b, so the cap must contain b->a
            for a, b in loop:
                new_tris.append('\ttri %d %d %d %d' % (nt, rep[b], rep[a], centre))
                nt += 1

        if report is not None:
            report.append((shader, len(loops), len(new_tris), 'capped'))

        blk2 = re.sub(r'(numverts\s+)\d+', lambda m: m.group(1) + str(nv), blk, count=1)
        blk2 = re.sub(r'(numtris\s+)\d+', lambda m: m.group(1) + str(nt), blk2, count=1)
        blk2 = re.sub(r'(numweights\s+)\d+', lambda m: m.group(1) + str(nw), blk2, count=1)

        # Insert on the LINE list, not by slicing the text. Slicing at the
        # newline leaves the carriage return of a CRLF file attached to the
        # line before, so adding a separator produced a doubled CR - which the
        # parser tolerates and every diff notices.
        rows = blk2.split('\n')
        cr = '\r' if rows and rows[0].endswith('\r') else ''

        def _after_last(rows, pat, lines):
            i = max(k for k, r in enumerate(rows) if r.startswith(pat))
            return rows[:i + 1] + [l + cr for l in lines] + rows[i + 1:]

        rows = _after_last(rows, '\tvert ', new_verts)
        rows = _after_last(rows, '\ttri ', new_tris)
        rows = _after_last(rows, '\tweight ', new_wts)
        blk2 = '\n'.join(rows)

        out = out[:s + shift] + blk2 + out[e + shift:]
        shift += len(blk2) - len(blk)

    return out


def rename_hands(text, material='pcvr/viewhands'):
    """Point the hands sub-mesh at a material that draws nothing."""
    return re.sub(r'(shader\s+")models/weapons/hands/hands(")',
                  lambda m: m.group(1) + material + m.group(2), text, flags=re.I)
