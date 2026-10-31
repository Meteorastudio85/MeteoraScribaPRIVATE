# Scriba - by Meteora Plugins
# Copyright (c) 2026 Meteora Plugins. All rights reserved. See LICENSE.txt.

"""Make the (already softened) corners of a TrueType font rounder.

Every glyph outline is flattened to a dense polyline. Corners are the places where the direction turns
a lot within a short distance (sharp corners AND small existing roundings). Each corner region is replaced by a
bigger quadratic fillet, then the outline is simplified and written back as straight segments.

usage: python round_more.py in.ttf out.ttf radius_convex radius_concave
"""
import math, sys
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

src, dst, R_CVX, R_CCV = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
STEP = 3.0          # flattening step (font units)
WIN_UNITS = 42.0    # corner detection half-window (font units): finds sharp corners and the small existing roundings
MIN_TURN = 38.0     # degrees of turn inside the window to be a corner

def lerp(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
def dist(a, b): return math.hypot(a[0] - b[0], a[1] - b[1])

def flatten(rec):
    contours, cur, pos = [], [], None
    def line_to(p):
        nonlocal pos
        n = max(1, int(dist(pos, p) / STEP))
        for i in range(1, n + 1):
            cur.append(lerp(pos, p, i / n))
        pos = p
    def quad_to(c, p):
        nonlocal pos
        L = dist(pos, c) + dist(c, p)
        n = max(2, int(L / STEP))
        p0 = pos
        for i in range(1, n + 1):
            t = i / n
            cur.append(lerp(lerp(p0, c, t), lerp(c, p, t), t))
        pos = p
    for op, args in rec:
        if op == 'moveTo':
            cur, pos = [args[0]], args[0]
        elif op == 'lineTo':
            line_to(args[0])
        elif op == 'qCurveTo':
            offs, last = list(args[:-1]), args[-1]
            for i, c in enumerate(offs):
                end = last if i == len(offs) - 1 else lerp(c, offs[i + 1], 0.5)
                quad_to(c, end)
        elif op in ('closePath', 'endPath'):
            if len(cur) > 3:
                if dist(cur[0], cur[-1]) < 1e-6:
                    cur.pop()
                contours.append(cur)
            cur = []
    return contours

def heading(a, b): return math.atan2(b[1] - a[1], b[0] - a[0])
def wrap(a):
    while a > math.pi: a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a

def round_poly(P):
    n = len(P)
    if n < 8:
        return P
    # cumulative arc length (closed)
    seg = [dist(P[i], P[(i + 1) % n]) for i in range(n)]
    total = sum(seg)
    cum = [0.0]
    for i in range(n - 1):
        cum.append(cum[-1] + seg[i])
    turn = [wrap(heading(P[i - 1], P[i]) - heading(P[i - 2], P[i - 1])) for i in range(n)]

    def idx_at(i, d):
        """index of the vertex about d units of arc from vertex i (d may be negative)"""
        target = (cum[i] + d) % total
        lo, hi = 0, n - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if cum[mid] <= target: lo = mid
            else: hi = mid - 1
        return lo

    # total turn inside a window around every vertex
    w = WIN_UNITS
    span = max(2, int(w / STEP))
    wturn = []
    for i in range(n):
        s = 0.0
        for k in range(-span, span + 1):
            s += turn[(i + k) % n]
        wturn.append(s)
    # corner centres: local extremes of |window turn| over the threshold
    centres = []
    for i in range(n):
        a = abs(wturn[i])
        if math.degrees(a) < MIN_TURN:
            continue
        if all(a >= abs(wturn[(i + k) % n]) for k in range(-span, span + 1)):
            if not centres or (i - centres[-1]) > span:
                centres.append(i)
    if len(centres) > 1 and (centres[0] + n - centres[-1]) <= span:
        centres.pop()
    if not centres:
        return P

    out_regions = []   # (start index, end index, list of new points)
    for ci, c in enumerate(centres):
        convex = wturn[c] < 0      # TrueType: clockwise outer contours, a right (negative) turn is an outer corner
        r = R_CVX if convex else R_CCV
        theta = min(abs(wturn[c]), math.radians(150))
        d = r * math.tan(theta / 2)
        # never reach the neighbouring corners
        prev_c = centres[ci - 1] if len(centres) > 1 else None
        next_c = centres[(ci + 1) % len(centres)] if len(centres) > 1 else None
        lim = total * 0.45
        if prev_c is not None:
            lim = min(lim, 0.45 * ((cum[c] - cum[prev_c]) % total))
        if next_c is not None:
            lim = min(lim, 0.45 * ((cum[next_c] - cum[c]) % total))
        d = min(d, lim)
        if d < STEP * 2:
            continue
        ia, ib = idx_at(c, -d), idx_at(c, d)
        A, B = P[ia], P[ib]
        # tangent directions just outside the region
        ta = heading(P[idx_at(ia, -2 * STEP)], A)
        tb = heading(B, P[idx_at(ib, 2 * STEP)])
        # control point = intersection of the two tangent lines
        da = (math.cos(ta), math.sin(ta)); db = (math.cos(tb), math.sin(tb))
        den = da[0] * db[1] - da[1] * db[0]
        if abs(den) < 1e-3:
            continue
        t = ((B[0] - A[0]) * db[1] - (B[1] - A[1]) * db[0]) / den
        C = (A[0] + da[0] * t, A[1] + da[1] * t)
        if t < 0 or dist(C, A) > 3 * d or dist(C, B) > 3 * d:
            C = P[c]
        m = max(4, int((dist(A, C) + dist(C, B)) / STEP))
        pts = [lerp(lerp(A, C, k / m), lerp(C, B, k / m), k / m) for k in range(m + 1)]
        out_regions.append((ia, ib, pts))

    # rebuild: walk the polygon, replacing regions
    replace = {}
    skip = [False] * n
    for ia, ib, pts in out_regions:
        replace[ia] = pts
        i = (ia + 1) % n
        while i != ib:
            skip[i] = True
            i = (i + 1) % n
        skip[ib] = True
    out = []
    for i in range(n):
        if i in replace:
            out.extend(replace[i])
        elif not skip[i]:
            out.append(P[i])
    return out

def rdp(points, eps):
    if len(points) < 3:
        return points
    a, b = points[0], points[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    best, idx = 0.0, 0
    for i in range(1, len(points) - 1):
        p = points[i]
        dd = abs(dy * p[0] - dx * p[1] + b[0] * a[1] - b[1] * a[0]) / L if L > 1e-9 else dist(p, a)
        if dd > best:
            best, idx = dd, i
    if best > eps:
        return rdp(points[:idx + 1], eps)[:-1] + rdp(points[idx:], eps)
    return [a, b]

def simplify_closed(P, eps=0.8):
    if len(P) < 4:
        return P
    # split at the two farthest-apart points so RDP works on open chains
    far = max(range(len(P)), key=lambda i: dist(P[0], P[i]))
    a = rdp(P[:far + 1], eps)
    b = rdp(P[far:] + [P[0]], eps)
    return a[:-1] + b[:-1]

font = TTFont(src)
gs = font.getGlyphSet()
glyf = font['glyf']
for name in font.getGlyphOrder():
    rec = DecomposingRecordingPen(gs)
    gs[name].draw(rec)
    pen = TTGlyphPen(None)
    for P in flatten(rec.value):
        Q = simplify_closed(round_poly(P))
        Q = [(int(round(x)), int(round(y))) for x, y in Q]
        Q = [p for i, p in enumerate(Q) if p != Q[i - 1]]
        if len(Q) < 3:
            continue
        pen.moveTo(Q[0])
        for p in Q[1:]:
            pen.lineTo(p)
        pen.closePath()
    glyf[name] = pen.glyph()

for t in ('fpgm', 'prep', 'cvt ', 'hdmx', 'LTSH', 'VDMX'):
    if t in font:
        del font[t]
if 'gasp' in font:
    font['gasp'].gaspRange = {0xFFFF: 0x000A}
font.save(dst)
print('ok', dst)
