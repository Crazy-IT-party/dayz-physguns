#!/usr/bin/env python3
"""The two guns' models, materials and textures, made here (no Object Builder, no Binarize).

    python3 -I tools/build_guns.py      -> mod/PhysGuns/data/{gravity_gun,portal_gun,portal_lamp_*}.p3d, gun_*.rvmat, tex/*.paa

Both are held as the AKS74U without its stock (PG_GunBase inherits it - its hands and animations), so they are sized
to it, in the engine's model space: the barrel points along -X, Y up; the back of the receiver (x +0.24) at the
cheek, the right hand round a pistol grip under x 0.12-0.18, the left round the fore end (x -0.06..-0.24), the bore
at y 0.095. About 0.65 m long.

Gravity gun: the HL2 manipulator's look - a black receiver with orange panels, an orange core housing with a bright
core between three claws; engraved name and warning plates, status lamps, coloured wires looping into the housing,
a capacitor on top, screwed bands. Portal gun: the ASHPD's look - a white shell, a black core showing a blue glow,
three black prongs round a glowing tip; engraved plates, a blue and an orange lamp, cables underneath.

Textures are drawn here (Pillow) and written as .paa (tools/paa.py): brushed steel, grained plastics, a knurled
grip, and the engraved plates (text cut in: dark, with a light edge under it). Faces get box-projected UVs; the
plates get their own.
A weapon's MLOD is not turned 180 degrees about Y the way an item's is, so every level is written pre-turned to undo
write_mlod's turn.
"""
from __future__ import annotations

import math
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_models import ARMAKE, BUILD, DATA, PFX, Mesh, write_mlod  # noqa: E402
import paa  # noqa: E402

TEX = DATA / "tex"
TPFX = PFX + "tex\\"
BORE = 0.095
SIDES = 16
TILE = 0.22                                    # metres per repeat of a material texture
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
FONT_COND = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"


# ---- materials ------------------------------------------------------------------------------------------------------
class Mat:
    def __init__(self, tex, mat):
        self.tex, self.mat = tex, mat


def textured(name) -> Mat:
    return Mat(TPFX + f"{name}_co.paa", PFX + f"gun_{name}.rvmat")


def glowing(r, g, b, rvmat) -> Mat:
    return Mat(f"#(argb,8,8,3)color({r},{g},{b},1,CO)", PFX + f"{rvmat}.rvmat")


STEEL, BLACK, ORANGE, WHITE, GRIP = (textured(n) for n in ("steel", "black", "orange", "white", "grip"))
WIRES = {c: Mat(f"#(argb,8,8,3)color({r},{g},{b},1,CO)", PFX + "gun_wire.rvmat")
         for c, (r, g, b) in {"red": (0.45, 0.1, 0.07), "yellow": (0.55, 0.46, 0.13), "blue": (0.12, 0.2, 0.45),
                              "green": (0.14, 0.34, 0.15), "black": (0.04, 0.04, 0.04)}.items()}
CORE = glowing(0.85, 0.95, 1, "gun_glow")
TIP = glowing(0.45, 0.75, 1, "gun_glow")
LAMPS = {"green": glowing(0.4, 1, 0.4, "lamp_green"), "amber": glowing(1, 0.7, 0.2, "lamp_amber"),
         "red": glowing(1, 0.25, 0.2, "lamp_red"), "blue": glowing(0.3, 0.6, 1, "lamp_blue"),
         "orange": glowing(1, 0.55, 0.15, "lamp_orange"),
         "blue_off": glowing(0.3, 0.6, 1, "lamp_blue_off"), "orange_off": glowing(1, 0.55, 0.15, "lamp_orange_off")}
LAMP_GLOW = {"green": (0.3, 1, 0.3), "amber": (1, 0.6, 0.1), "red": (1, 0.15, 0.1), "blue": (0.2, 0.5, 1),
             "orange": (1, 0.45, 0.05)}
LAMP_OFF = {"blue_off": (0.2, 0.5, 1), "orange_off": (1, 0.45, 0.05)}       # unlit glass: the portal gun's mode lamps


# ---- shapes -----------------------------------------------------------------------------------------------------
def auto_uv(m: Mesh, idx):
    """box projection: onto the plane across the face normal's biggest axis"""
    P = np.array([m.pts[i] for i in idx])
    n = np.cross(P[1] - P[0], P[2] - P[0])
    a = int(np.argmax(np.abs(n)))
    u_ax, v_ax = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[a]
    return [(p[u_ax] / TILE, -p[v_ax] / TILE) for p in P]


def face(m: Mesh, idx, mt: Mat, group, inside, uv=None):
    m.face(list(idx), uv or auto_uv(m, idx), mt.tex, mt.mat, group, inside)


def hexa(m: Mesh, P, mt: Mat, group):
    """a solid of eight corners: 0-3 one end, 4-7 the other, in the same turn"""
    idx = [m.pt(q) for q in P]
    c = tuple(np.mean(np.asarray(P, float), 0))
    for q in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
        face(m, [idx[i] for i in q], mt, group, c)


def cube(m: Mesh, lo, hi, mt: Mat, group):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    hexa(m, [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1),
             (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], mt, group)


def frame(d):
    d = np.asarray(d, float) / np.linalg.norm(d)
    up = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    s = np.cross(d, up)
    s /= np.linalg.norm(s)
    return d, s, np.cross(s, d)


def bar(m: Mesh, a, b, w, h, mt: Mat, group, twist=0.0):
    """a square-ish bar from a to b (w wide across, h high), its cross section turned by twist about its length"""
    a, b = np.asarray(a, float), np.asarray(b, float)
    _, s, u = frame(b - a)
    c, k = math.cos(twist), math.sin(twist)
    s, u = s * c + u * k, u * c - s * k
    ring = [s * w / 2 + u * h / 2, -s * w / 2 + u * h / 2, -s * w / 2 - u * h / 2, s * w / 2 - u * h / 2]
    hexa(m, [tuple(a + r) for r in ring] + [tuple(b + r) for r in ring], mt, group)


def cyl(m: Mesh, a, b, r0, r1, mt: Mat, group, sides=12, caps=True):
    """a (tapered) cylinder from a to b"""
    a, b = np.asarray(a, float), np.asarray(b, float)
    _, s, u = frame(b - a)
    ring = lambda c, r: [m.pt(tuple(c + r * (s * math.cos(2 * math.pi * k / sides) + u * math.sin(2 * math.pi * k / sides))))
                         for k in range(sides)]
    ra, rb = ring(a, r0), ring(b, r1)
    mid = tuple((a + b) / 2)
    for k in range(sides):
        j = (k + 1) % sides
        face(m, [ra[k], ra[j], rb[j], rb[k]], mt, group, mid)
    if caps:
        for rr, c in ((ra, a), (rb, b)):
            ci = m.pt(tuple(c))
            for k in range(sides):
                face(m, [ci, rr[k], rr[(k + 1) % sides]], mt, group, mid)


def turned(m: Mesh, stations, mt: Mat, group, y=BORE, z=0.0, sides=SIDES, caps=True):
    """a solid of revolution about the bore line: stations [(x, radius)] from back to front"""
    rings = [[m.pt((x, y + r * math.cos(2 * math.pi * k / sides), z + r * math.sin(2 * math.pi * k / sides)))
              for k in range(sides)] for x, r in stations]
    for i in range(len(stations) - 1):
        mid = ((stations[i][0] + stations[i + 1][0]) / 2, y, z)
        for k in range(sides):
            j = (k + 1) % sides
            q = [rings[i][k], rings[i][j], rings[i + 1][j], rings[i + 1][k]]
            if stations[i][1] < 1e-4:
                q = [rings[i][k], rings[i + 1][j], rings[i + 1][k]]
            elif stations[i + 1][1] < 1e-4:
                q = [rings[i][k], rings[i][j], rings[i + 1][k]]
            face(m, q, mt, group, mid)
    if caps:
        for ring, (x, r), other in ((rings[0], stations[0], stations[1][0]), (rings[-1], stations[-1], stations[-2][0])):
            if r < 1e-4:
                continue
            c = m.pt((x, y, z))
            inside = (x + (other - x) * 0.5, y, z)
            for k in range(sides):
                face(m, [c, ring[k], ring[(k + 1) % sides]], mt, group, inside)


def bezier(p0, p1, p2, p3, n=16):
    P = [np.asarray(p, float) for p in (p0, p1, p2, p3)]
    return [tuple((1 - t) ** 3 * P[0] + 3 * (1 - t) ** 2 * t * P[1] + 3 * (1 - t) * t ** 2 * P[2] + t ** 3 * P[3])
            for t in np.linspace(0, 1, n)]


def wire(m: Mesh, pts, r, mt: Mat, sides=6, r_in=None, cap_end=False, group="wire"):
    """a cable along a polyline (its ends are buried in what it plugs into, so no caps unless asked). r is the
    radius across (for a path in the XY plane: along Z), r_in the one in the path's plane (an oval band)"""
    r_in = r if r_in is None else r_in
    pts = [np.asarray(p, float) for p in pts]
    rings = []
    prev_s = None
    for i, p in enumerate(pts):
        d = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]
        d, s, u = frame(d)
        if prev_s is not None:                 # carry the side vector along so the tube does not twist
            s = prev_s - d * np.dot(prev_s, d)
            s /= np.linalg.norm(s)
            u = np.cross(s, d)
        prev_s = s
        rings.append([m.pt(tuple(p + s * r * math.cos(2 * math.pi * k / sides) + u * r_in * math.sin(2 * math.pi * k / sides)))
                      for k in range(sides)])
    for i in range(len(pts) - 1):
        mid = tuple((pts[i] + pts[i + 1]) / 2)
        for k in range(sides):
            j = (k + 1) % sides
            face(m, [rings[i][k], rings[i][j], rings[i + 1][j], rings[i + 1][k]], mt, group, mid)
    if cap_end:
        c = m.pt(tuple(pts[-1]))
        inside = tuple((pts[-1] + pts[-2]) / 2)
        for k in range(sides):
            face(m, [c, rings[-1][k], rings[-1][(k + 1) % sides]], mt, group, inside)


def screw(m: Mesh, at, normal, r=0.0035):
    """a hex screw head standing on a surface"""
    at, n = np.asarray(at, float), np.asarray(normal, float)
    cyl(m, at - n * 0.001, at + n * 0.0018, r, r * 0.85, STEEL, "screw", sides=6)


def plate(m: Mesh, x0, x1, y0, y1, z, side, tex_name, edge: Mat, thick=0.003, screws=True):
    """an engraved plate on a side (side +1: on +Z facing out, -1: on -Z), its texture across the outer face"""
    za, zb = z, z + side * thick
    lo, hi = (x0, y0, min(za, zb)), (x1, y1, max(za, zb))
    cube(m, lo, hi, edge, "plate")
    # the outer face, a hair proud of the box, with the picture on it - read left to right from outside:
    # from +Z looking back at the gun the front (-X) is to the right, from -Z it is to the left
    zf = zb + side * 0.0004
    corners = [(x0, y1, zf), (x1, y1, zf), (x1, y0, zf), (x0, y0, zf)]
    if side > 0:
        uv = [(1, 0), (0, 0), (0, 1), (1, 1)]
    else:
        uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    idx = [m.pt(c) for c in corners]
    mt = Mat(TPFX + f"{tex_name}_co.paa", PFX + "gun_plate.rvmat")
    face(m, idx, mt, "plate_face", ((x0 + x1) / 2, (y0 + y1) / 2, za), uv)
    if screws:
        inset = 0.006
        for sx, sy in ((x0 + inset, y0 + inset), (x1 - inset, y0 + inset), (x0 + inset, y1 - inset), (x1 - inset, y1 - inset)):
            screw(m, (sx, sy, zf), (0, 0, side), 0.0025)


def lamp(m: Mesh, at, colour: str, r=0.0055):
    """a status lamp standing up: a steel bezel, a glowing dome"""
    x, y, z = at
    cyl(m, (x, y - 0.001, z), (x, y + 0.004, z), r + 0.0025, r + 0.002, STEEL, "lamp", sides=10)
    cyl(m, (x, y + 0.004, z), (x, y + 0.009, z), r, r * 0.55, LAMPS[colour], "lamp", sides=10)


def slices(m: Mesh, rings_def, mt: Mat, group, n=20, power=3.0, cap_bottom=True):
    """a solid of horizontal slices [(y, x_middle, half depth front, half depth back, half width)], top to bottom:
    each slice a rounded rectangle (a superellipse of this power); the front is -X"""
    rings = []
    for y, xc, df, db, w in rings_def:
        ring = []
        for k in range(n):
            t = 2 * math.pi * k / n
            c, sn = math.cos(t), math.sin(t)
            ex = abs(c) ** (2 / power) * (1 if c >= 0 else -1)
            ez = abs(sn) ** (2 / power) * (1 if sn >= 0 else -1)
            ring.append(m.pt((xc + (db if ex >= 0 else df) * ex, y, w * ez)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        y = (rings_def[i][0] + rings_def[i + 1][0]) / 2
        mid = ((rings_def[i][1] + rings_def[i + 1][1]) / 2, y, 0.0)
        for k in range(n):
            j = (k + 1) % n
            face(m, [rings[i][k], rings[i][j], rings[i + 1][j], rings[i + 1][k]], mt, group, mid)
    if cap_bottom:
        y, xc = rings_def[-1][0], rings_def[-1][1]
        c = m.pt((xc, y, 0.0))
        for k in range(n):
            face(m, [c, rings[-1][k], rings[-1][(k + 1) % n]], mt, group, (xc, y + 0.01, 0.0))


def pistol_grip(m: Mesh):
    """under the back of the receiver, raked back like an AK's, where the right hand closes: finger grooves down
    the front, a palm swell at the back, a tang over the web of the hand, a flared cap; a steel trigger on its pin
    in a housing, inside a guard that runs from the grip's front to the receiver"""
    rows = []
    top, bottom = 0.042, -0.098
    for k in range(25):
        f = k / 24
        y = top + (bottom - top) * f
        xc = 0.125 + 0.042 * f                                           # the rake
        groove = 0.0045 * max(0.0, math.sin(math.pi * 3 * min(1.0, max(0.0, (f - 0.12) / 0.8)))) ** 1.5
        df = 0.024 - groove                                              # three finger grooves in the front
        db = 0.024 + 0.006 * math.sin(math.pi * min(1.0, f / 0.85))      # the palm swell
        w = 0.0155 + 0.002 * math.sin(math.pi * f)
        rows.append((y, xc, df, db, w))
    slices(m, rows, GRIP, "grip", cap_bottom=False)
    # the cap: a flared, rounded foot with a screw
    slices(m, [(bottom + 0.001, 0.167, 0.027, 0.032, 0.0185), (bottom - 0.006, 0.168, 0.028, 0.033, 0.019),
               (bottom - 0.011, 0.168, 0.025, 0.030, 0.0165)], BLACK, "grip_cap")
    screw(m, (0.168, bottom - 0.011, 0.0), (0, -1, 0), 0.004)
    for zs in (-1, 1):                                                   # the screw holding the grip on
        screw(m, (0.15, -0.005, zs * 0.0175), (0, 0, zs), 0.003)
    # the tang over the web of the hand
    slices(m, [(0.046, 0.165, 0.02, 0.02, 0.017), (0.038, 0.168, 0.02, 0.022, 0.016), (0.032, 0.17, 0.018, 0.016, 0.012)],
           BLACK, "tang")
    # the trigger housing under the receiver, the pin through it, the trigger: a curved blade with a wider shoe
    cube(m, (0.062, 0.03, -0.009), (0.112, 0.042, 0.009), BLACK, "trigger_housing")
    cyl(m, (0.092, 0.036, -0.0105), (0.092, 0.036, 0.0105), 0.0022, 0.0022, STEEL, "trigger_pin", sides=8)
    blade = bezier((0.088, 0.034, 0.0), (0.094, 0.022, 0.0), (0.095, 0.011, 0.0), (0.085, 0.003, 0.0), 10)   # bowed back, the tip forward
    wire(m, blade, 0.0035, STEEL, sides=8, r_in=0.0018, cap_end=True, group="trigger")
    # the guard: a flat band from the grip's front, under the trigger and up into the receiver ahead of it
    guard = bezier((0.104, 0.02, 0.0), (0.098, -0.008, 0.0), (0.05, -0.012, 0.0), (0.052, 0.034, 0.0), 18)
    wire(m, guard, 0.0055, BLACK, sides=8, r_in=0.0022, group="guard")


def half_sleeve(m: Mesh, x0, x1, r, t, arc, mt: Mat, group, n=10):
    """a thick shell round the bottom of the bore line (arc radians wide, t thick), from x0 to x1"""
    angs = [math.pi - arc / 2 + arc * k / n for k in range(n + 1)]
    P = lambda x, rr, a: (x, BORE + rr * math.cos(a), rr * math.sin(a))
    for k in range(n):
        a0, a1 = angs[k], angs[k + 1]
        hexa(m, [P(x0, r, a0), P(x0, r + t, a0), P(x0, r + t, a1), P(x0, r, a1),
                 P(x1, r, a0), P(x1, r + t, a0), P(x1, r + t, a1), P(x1, r, a1)], mt, group)


def ring_screws(m: Mesh, x, r, n, start, facing=-1):
    for k in range(n):
        a = start + k * 2 * math.pi / n
        screw(m, (x, BORE + r * math.sin(a), r * math.cos(a)), (facing, 0, 0))


# ---- the guns -----------------------------------------------------------------------------------------------------
def build_gravity_gun() -> Mesh:
    m = Mesh()
    cube(m, (-0.02, 0.04, -0.036), (0.24, 0.135, 0.036), BLACK, "body")
    for zs in (-1, 1):
        cube(m, (0.0, 0.06, zs * 0.036), (0.21, 0.12, zs * 0.042), ORANGE, "panel")
    plate(m, 0.025, 0.195, 0.066, 0.114, -0.042, -1, "gg_name", STEEL)                   # the name, left side
    plate(m, 0.025, 0.125, 0.068, 0.112, 0.042, 1, "gg_warn", STEEL)                     # a warning, right side
    for x in (0.14, 0.165, 0.19):                                                        # vents behind it
        cube(m, (x, 0.072, 0.042), (x + 0.016, 0.108, 0.045), BLACK, "vent")
    cube(m, (0.07, 0.135, -0.012), (0.16, 0.145, 0.012), STEEL, "rail")
    for x in (0.08, 0.15):
        screw(m, (x, 0.145, 0.0), (0, 1, 0), 0.003)
    for x, c in ((-0.004, "green"), (0.019, "amber"), (0.042, "red")):                   # status lamps
        lamp(m, (x, 0.135, 0.0), c)
    bar(m, (0.24, 0.088, 0.0), (0.255, 0.088, 0.0), 0.065, 0.08, STEEL, "butt")
    for sy in (0.06, 0.116):
        for sz in (-0.022, 0.022):
            screw(m, (0.255, sy, sz), (1, 0, 0), 0.003)
    pistol_grip(m)
    # the core housing, ribbed, and a band of screws round its front ring
    turned(m, [(-0.02, 0.064), (-0.05, 0.072), (-0.19, 0.072), (-0.21, 0.066)], ORANGE, "housing")
    for x in (-0.07, -0.12, -0.17):
        turned(m, [(x, 0.075), (x - 0.012, 0.075)], BLACK, "rib")
    turned(m, [(-0.21, 0.085), (-0.25, 0.085)], STEEL, "ring")
    ring_screws(m, -0.25, 0.078, 6, math.pi / 6)
    turned(m, [(-0.25, 0.034), (-0.255, 0.034)], LAMPS["blue"], "core_ring", sides=12)    # a glowing collar round the core
    turned(m, [(-0.25, 0.03), (-0.29, 0.024), (-0.31, 0.0)], CORE, "core", sides=12)
    for k in range(3):                                                                   # three claws, top and below
        a = math.pi / 2 + k * 2 * math.pi / 3
        p = lambda x, r: (x, BORE + r * math.sin(a), r * math.cos(a))
        bar(m, p(-0.24, 0.07), p(-0.33, 0.115), 0.028, 0.02, BLACK, "claw", twist=-a + math.pi / 2)
        bar(m, p(-0.33, 0.115), p(-0.41, 0.07), 0.024, 0.018, ORANGE, "claw", twist=-a + math.pi / 2)
        bar(m, p(-0.325, 0.112), p(-0.335, 0.118), 0.032, 0.024, STEEL, "claw", twist=-a + math.pi / 2)  # the knuckle
    # the fore end the left hand holds: a knurled plate under the housing
    cube(m, (-0.185, BORE - 0.077, -0.032), (-0.035, BORE - 0.068, 0.032), GRIP, "foregrip")
    for x in np.arange(-0.175, -0.04, 0.013):                                         # ribs across it
        bar(m, (x, BORE - 0.08, -0.028), (x, BORE - 0.08, 0.028), 0.006, 0.006, BLACK, "foregrip_rib")
    for zs in (-1, 1):                                                                  # steel rails along its sides
        cube(m, (-0.19, BORE - 0.079, zs * 0.032), (-0.03, BORE - 0.066, zs * 0.036), STEEL, "foregrip_rail")
        for x in (-0.18, -0.04):
            screw(m, (x, BORE - 0.0725, zs * 0.036), (0, 0, zs), 0.0025)
    # a capacitor on top of the housing, strapped down
    cyl(m, (-0.035, BORE + 0.088, 0.0), (-0.155, BORE + 0.088, 0.0), 0.014, 0.014, STEEL, "capacitor", sides=12)
    for x in (-0.06, -0.13):
        bar(m, (x, BORE + 0.068, 0.0), (x, BORE + 0.104, 0.0), 0.008, 0.032, BLACK, "strap")
    # wires: out of the receiver's front, looping out to the side and into the housing
    for zs, cols in ((-1, ("red", "yellow")), (1, ("blue", "green"))):
        for k, c in enumerate(cols):
            y0 = 0.06 + 0.035 * k
            a = math.radians(-25 + 30 * k)
            end = (-0.10 - 0.045 * k, BORE + 0.07 * math.sin(a), zs * 0.07 * math.cos(a))
            pts = bezier((-0.015, y0, zs * 0.026), (-0.05, y0 - 0.01, zs * 0.075),
                         (end[0] + 0.03, end[1], zs * 0.095), (end[0], end[1], zs * 0.06))
            wire(m, pts, 0.0035, WIRES[c])
    # and one from the receiver's top into the capacitor
    wire(m, bezier((0.06, 0.13, 0.0), (0.05, 0.18, 0.0), (-0.02, 0.19, 0.0), (-0.04, BORE + 0.088, 0.0)), 0.004,
         WIRES["black"])
    return m


def build_portal_gun() -> Mesh:
    m = Mesh()
    turned(m, [(0.26, 0.0), (0.255, 0.028), (0.24, 0.042), (0.19, 0.052), (0.11, 0.058), (0.04, 0.058),
               (-0.01, 0.053), (-0.03, 0.046)], WHITE, "shell")
    turned(m, [(0.205, 0.0535), (0.195, 0.0535)], BLACK, "seam")                         # a seam round the shell
    turned(m, [(-0.03, 0.044), (-0.22, 0.044)], BLACK, "core")
    for x in (-0.06, -0.11, -0.16):                                                       # the glow between bands
        turned(m, [(x, 0.047), (x - 0.03, 0.047)], TIP, "window", sides=12)
    bar(m, (0.06, 0.147, 0.0), (-0.24, 0.135, 0.0), 0.04, 0.02, WHITE, "spine")           # shell to front collar
    bar(m, (-0.235, 0.12, 0.0), (-0.235, 0.145, 0.0), 0.04, 0.02, WHITE, "spine")
    for x in (0.03, -0.09, -0.2):
        screw(m, (x, 0.147 - (0.06 - x) * 0.04 + 0.01, 0.0), (0, 1, 0), 0.003)
    lamp(m, (-0.005, 0.1525, 0.0), "blue_off", 0.005)       # which portal it opens: the lit one (PG_PortalGun's
    lamp(m, (-0.035, 0.1515, 0.0), "orange_off", 0.005)     # mode) is a glowing dome laid over it, portal_lamp_*.p3d
    turned(m, [(-0.21, 0.05), (-0.22, 0.065), (-0.27, 0.06), (-0.28, 0.045)], WHITE, "collar")
    turned(m, [(-0.243, 0.0665), (-0.25, 0.0665)], STEEL, "collar_band")
    ring_screws(m, -0.2805, 0.036, 3, math.pi / 2 + math.pi / 3)
    turned(m, [(-0.27, 0.02), (-0.34, 0.016), (-0.355, 0.0)], TIP, "tip", sides=12)
    for k in range(3):
        a = -math.pi / 2 + k * 2 * math.pi / 3
        p = lambda x, r: (x, BORE + r * math.sin(a), r * math.cos(a))
        bar(m, p(-0.26, 0.05), p(-0.37, 0.034), 0.018, 0.014, BLACK, "prong", twist=-a + math.pi / 2)
        bar(m, p(-0.355, 0.037), p(-0.372, 0.033), 0.021, 0.017, STEEL, "prong", twist=-a + math.pi / 2)  # steel tips
    for side in (-1, 1):
        plate(m, 0.06, 0.15, 0.076, 0.114, side * 0.0585, side, "pg_name", WHITE, thick=0.0025)
    cube(m, (0.05, 0.02, -0.03), (0.17, 0.045, 0.03), BLACK, "base")
    half_sleeve(m, -0.215, -0.04, 0.0445, 0.007, math.radians(150), GRIP, "foregrip")
    for x in np.arange(-0.205, -0.045, 0.016):                                          # ribs round it
        half_sleeve(m, x, x + 0.005, 0.0515, 0.003, math.radians(140), BLACK, "foregrip_rib", n=8)
    pistol_grip(m)
    # two cables under the core, from the shell's belly to the collar
    for zs, c in ((-1, "black"), (1, "blue")):
        wire(m, bezier((0.03, 0.045, zs * 0.018), (-0.04, 0.015, zs * 0.03), (-0.16, 0.02, zs * 0.03),
                       (-0.225, 0.05, zs * 0.02)), 0.004, WIRES[c])
    return m


# ---- geometry and memory ----------------------------------------------------------------------------------------
def build_gun_geometry(front, mass=4.5) -> tuple[Mesh, list[float]]:
    """convex boxes with mass, for the gun lying on the ground"""
    g = Mesh()
    for n, (lo, hi) in enumerate([((-0.03, 0.02, -0.065), (0.27, 0.17, 0.065)),
                                  ((front, BORE - 0.08, -0.08), (-0.03, BORE + 0.11, 0.08)),
                                  ((0.095, -0.11, -0.018), (0.195, 0.02, 0.018))]):
        before = len(g.pts)
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        P = [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]
        idx = [g.pt(p) for p in P]
        c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        for q in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            g.face([idx[i] for i in q], [(0, 0)] * 4, "", "", f"c{n}", c)
        g.sel[f"Component{n + 1:02d}"] = set(range(before, len(g.pts)))
    return g, [mass / len(g.pts)] * len(g.pts)


def build_gun_memory(front) -> Mesh:
    m = Mesh()
    points = {
        "usti hlavne": (front, BORE, 0.0),          # the muzzle: the beam and the portal shots start here
        "konec hlavne": (-0.10, BORE, 0.0),
        "nabojnicestart": (-0.02, 0.12, 0.0),
        "nabojniceend": (0.0, 0.13, 0.10),
        "eye": (0.20, 0.235, 0.0),                    # over the top, looking past the front: no sights
        "lamp_blue": (-0.005, 0.1565, 0.0),           # the portal gun's mode lamps, where the lit dome goes
        "lamp_orange": (-0.035, 0.1555, 0.0),
        "ce_center": (-0.07, 0.04, 0.0),
        "invview": (-0.07, 0.04, 0.55),
        "boundingbox_min": (0.27, -0.11, 0.1),
        "boundingbox_max": (front, 0.21, -0.1),
    }
    for name, pt in points.items():
        m.sel[name] = {m.pt(pt)}
    return m


# ---- textures --------------------------------------------------------------------------------------------------
RNG = np.random.default_rng(7)


def noise(size, blur_x=0.0, blur_y=0.0):
    a = RNG.standard_normal((size, size))
    im = Image.fromarray(((a * 40) + 128).clip(0, 255).astype(np.uint8))
    if blur_x or blur_y:
        im = im.resize((max(1, int(size / (1 + blur_x))), max(1, int(size / (1 + blur_y)))), Image.BILINEAR)
        im = im.resize((size, size), Image.BICUBIC)
    return (np.asarray(im, float) - 128) / 40


def tint(base, grain, amount):
    a = np.clip(np.asarray(base, float)[None, None, :] * (1 + grain[..., None] * amount), 0, 1)
    return Image.fromarray((a * 255).astype(np.uint8), "RGB")


def scratches(im: Image.Image, n, colour, length=40, width=1):
    d = ImageDraw.Draw(im)
    for _ in range(n):
        x, y = RNG.uniform(0, im.width), RNG.uniform(0, im.height)
        a = RNG.uniform(0, math.pi)
        L = RNG.uniform(length * 0.3, length)
        d.line([(x, y), (x + L * math.cos(a), y + L * math.sin(a))], fill=colour, width=width)
    return im


def field(w, h, cells):
    """smooth noise in 0..1, about `cells` blobs across"""
    lw, lh = max(2, int(cells)), max(2, int(cells * h / w))
    a = RNG.random((lh, lw))
    im = Image.fromarray((a * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(im, float) / 255


def weathered(im: Image.Image, rust=0.3, scratch=40, dirt=0.25, bare=(150, 148, 142)) -> Image.Image:
    """old kit: grime in the low spots, rust eating in patches and running down in streaks, scratches through
    the paint to the metal. rust: share of the surface; scratch: how many"""
    w, h = im.size
    a = np.asarray(im.convert("RGB"), float) / 255
    # grime: a soft darkening, blotchy
    g = field(w, h, 6) * 0.6 + field(w, h, 20) * 0.4
    a *= (1 - dirt * g)[..., None]
    if rust > 0:
        m = field(w, h, 5) * 0.55 + field(w, h, 18) * 0.3 + field(w, h, 70) * 0.15
        cut = np.quantile(m, 1 - rust)
        edge = np.clip((m - cut) / 0.06, 0, 1)                      # a frayed edge round each patch
        speck = field(w, h, 140)
        tone = np.stack([0.36 + 0.16 * speck, 0.17 + 0.08 * speck, 0.07 + 0.03 * speck], -1)
        a = a * (1 - edge[..., None]) + tone * edge[..., None]
        # streaks: rust washed down from the patches (image down is world down on the sides)
        run = edge.copy()
        for k in range(1, 40):
            run[k:] = np.maximum(run[k:], edge[:-k] * (1 - k / 40) * 0.5)
        streak = run * (field(w, h, 60) > 0.45)
        a = a * (1 - 0.5 * streak[..., None]) + np.array([0.42, 0.22, 0.09]) * 0.5 * streak[..., None]
    out = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(out)
    for _ in range(scratch):                                         # through to bright metal, with a dark lip
        x, y = RNG.uniform(0, w), RNG.uniform(0, h)
        ang = RNG.uniform(0, math.pi)
        L = RNG.uniform(8, 60) * w / 256
        pts = [(x, y)]
        for _ in range(4):                                           # a little wander
            ang += RNG.uniform(-0.25, 0.25)
            x, y = x + L / 4 * math.cos(ang), y + L / 4 * math.sin(ang)
            pts.append((x, y))
        d.line([(px + 1, py + 1) for px, py in pts], fill=(25, 22, 20), width=1)
        d.line(pts, fill=bare, width=1 if RNG.random() < 0.8 else 2)
    for _ in range(scratch // 3):                                    # chips: small flakes of paint gone
        x, y, r = RNG.uniform(0, w), RNG.uniform(0, h), RNG.uniform(1, 3.5) * w / 256
        d.ellipse((x - r, y - r * 0.7, x + r, y + r * 0.7), fill=bare)
    return out


def tex_steel(size=256):
    g = noise(size, blur_x=30) * 0.7 + noise(size) * 0.15                  # brushed along u
    return scratches(tint((0.55, 0.56, 0.58), g, 0.12), 10, (180, 182, 186), 35)


def tex_plastic(rgb, size=256, amount=0.05, wear=None):
    im = tint(rgb, noise(size, 1, 1), amount)
    if wear:
        scratches(im, 7, wear, 20)
    return im


def tex_grip(size=256):
    """a knurled black grip: a diamond pattern of little pyramids, lit from above"""
    y, x = np.mgrid[0:size, 0:size] / size * 16 * 2 * math.pi
    a = np.sin(x + y) * np.sin(x - y)
    v = 0.09 + 0.05 * a + noise(size) * 0.01
    return Image.fromarray((np.clip(v, 0, 1)[..., None].repeat(3, 2) * 255).astype(np.uint8), "RGB")


def engrave(d: ImageDraw.ImageDraw, xy, text, font, dark, light, anchor="la"):
    """text cut into the surface: its far edge catches the light, the cut is in shadow"""
    x, y = xy
    d.text((x + 1, y + 2), text, font=font, fill=light, anchor=anchor)
    d.text((x, y), text, font=font, fill=dark, anchor=anchor)


def screw_holes(d, w, h, inset, colour):
    for cx, cy in ((inset, inset), (w - inset, inset), (inset, h - inset), (w - inset, h - inset)):
        d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), outline=colour, width=2)


def plate_base(w, h, rgb, brushed=True):
    g = noise(max(w, h), blur_x=30 if brushed else 1)[:h, :w] * 0.6
    im = tint(rgb, g, 0.10)
    d = ImageDraw.Draw(im)
    d.rectangle((6, 6, w - 7, h - 7), outline=(60, 60, 64), width=2)           # an engraved border
    d.rectangle((8, 8, w - 5, h - 5), outline=(200, 200, 205), width=1)
    return im, d


def tex_gg_name():
    w, h = 1024, 288
    im, d = plate_base(w, h, (0.6, 0.6, 0.62))
    big, small = ImageFont.truetype(FONT, 78), ImageFont.truetype(FONT_COND, 34)
    dark, light = (38, 38, 42), (215, 215, 220)
    engrave(d, (60, 44), "PG-1  GRAVITY", big, dark, light)
    engrave(d, (60, 128), "MANIPULATOR", big, dark, light)
    engrave(d, (60, 222), "ZERO-POINT FIELD  ·  CLASS IV  ·  SN 0042-ZP7", small, dark, light)
    # a little emblem: three claws round a dot
    cx, cy = 900, 120
    for k in range(3):
        a = -math.pi / 2 + k * 2 * math.pi / 3
        d.line([(cx + 30 * math.cos(a), cy + 30 * math.sin(a)), (cx + 70 * math.cos(a), cy + 70 * math.sin(a))],
               fill=dark, width=14)
    d.ellipse((cx - 16, cy - 16, cx + 16, cy + 16), fill=(230, 120, 20))
    return im


def tex_gg_warn():
    w, h = 640, 288
    im, d = plate_base(w, h, (0.62, 0.52, 0.16), brushed=False)
    for k in range(-4, 12):                                                   # hazard stripes on the left
        x = k * 44
        d.polygon([(x, 0), (x + 22, 0), (x + 22 + h * 0.5, h), (x + h * 0.5, h)], fill=(25, 22, 18))
    d.rectangle((150, 0, w, h), fill=(158, 132, 44))
    d.rectangle((6, 6, w - 7, h - 7), outline=(60, 50, 10), width=2)
    big, small = ImageFont.truetype(FONT, 58), ImageFont.truetype(FONT_COND, 30)
    dark, light = (30, 26, 12), (245, 220, 120)
    engrave(d, (180, 40), "CAUTION", big, dark, light)
    engrave(d, (180, 118), "DO NOT POINT AT", small, dark, light)
    engrave(d, (180, 156), "PERSONNEL", small, dark, light)
    engrave(d, (180, 214), "MAX LOAD 250 KG", small, dark, light)
    return im


def tex_pg_name():
    w, h = 640, 288
    im, d = plate_base(w, h, (0.76, 0.75, 0.71), brushed=False)
    big, small = ImageFont.truetype(FONT, 62), ImageFont.truetype(FONT_COND, 30)
    dark, light = (70, 72, 76), (255, 255, 255)
    engrave(d, (40, 40), "PG-2", big, dark, light)
    engrave(d, (40, 120), "PORTAL DEVICE", ImageFont.truetype(FONT, 44), dark, light)
    engrave(d, (40, 200), "DUAL APERTURE · SN 1138-AP", small, dark, light)
    d.ellipse((440, 34, 500, 94), fill=(40, 120, 255), outline=dark, width=3)
    d.ellipse((520, 34, 580, 94), fill=(255, 130, 20), outline=dark, width=3)
    return im


def write_tex(name, im: Image.Image):
    TEX.mkdir(parents=True, exist_ok=True)
    w = 1
    while w < max(im.size):
        w *= 2
    if im.size != (w, w // 2 if im.width > im.height else w):
        im = im.resize((w, w // 2 if im.width > im.height else w), Image.LANCZOS)
    (TEX / f"{name}_co.paa").write_bytes(paa.encode(im.convert("RGB"), dxt5=False))


# ---- conditions -------------------------------------------------------------------------------------------------
# The config's healthLevels swap every material for its _damage / _badly / _destruct version as the gun wears
# (PG_GunBase's DamageSystem, config.cpp). Pristine and worn look the same - already old kit; then a damage macro
# (MC) is laid over the paint: grime and scratches when damaged, rust when badly damaged, rust and soot when
# ruined. The lamps dim when badly damaged and go out when ruined, and so does the core's glow.
SURFACES = {"gun_steel": (0.6, 60), "gun_black": (0.25, 30), "gun_orange": (0.3, 30), "gun_white": (0.4, 40),
            "gun_grip": (0.1, 10), "gun_plate": (0.45, 45), "gun_wire": (0.35, 30)}
CONDITIONS = {                 # suffix: (macro, detail, how much shine is left, diffuse)
    "": ("#(argb,8,8,3)color(0,0,0,0,MC)", 0.5, 1.0, 1.0),
    "_damage": (TPFX + "gun_damaged_mc.paa", 0.45, 0.8, 0.95),
    "_badly": (TPFX + "gun_badly_mc.paa", 0.4, 0.5, 0.9),
    "_destruct": (TPFX + "gun_ruined_mc.paa", 0.35, 0.2, 0.8),
}
GLOW_LEFT = {"": 1.0, "_damage": 0.85, "_badly": 0.35, "_destruct": 0.0}
GLOWS = dict({"gun_glow": (0.6, 0.85, 1.0, 9.0)}, **{f"lamp_{n}": (*c, 6.0) for n, c in LAMP_GLOW.items()},
             **{f"lamp_{n}": (*c, 0.35) for n, c in LAMP_OFF.items()})


def damage_lists() -> dict[str, list[str]]:
    """config healthLevels: for each condition the materials in one order, the pristine ones first"""
    names = list(SURFACES) + list(GLOWS)
    return {suf: [PFX + f"{n}{suf}.rvmat" for n in names] for suf in CONDITIONS}


def tex_macro(rust, soot, scratch, seed) -> Image.Image:
    """a damage macro: colour where it covers, alpha how much"""
    global RNG
    RNG = np.random.default_rng(seed)
    w = h = 512
    m = field(w, h, 5) * 0.55 + field(w, h, 18) * 0.3 + field(w, h, 70) * 0.15
    a = np.zeros((h, w))
    rgb = np.zeros((h, w, 3))
    rgb[:] = (0.16, 0.14, 0.12)                                     # grime
    a += 0.25 * field(w, h, 9)
    if rust:
        cut = np.quantile(m, 1 - rust)
        e = np.clip((m - cut) / 0.05, 0, 1)
        sp = field(w, h, 160)
        tone = np.stack([0.38 + 0.18 * sp, 0.18 + 0.08 * sp, 0.07 + 0.03 * sp], -1)
        rgb = rgb * (1 - e[..., None]) + tone * e[..., None]
        a = np.maximum(a, e * 0.95)
    if soot:
        sm = field(w, h, 4)
        cut = np.quantile(sm, 1 - soot)
        e = np.clip((sm - cut) / 0.15, 0, 1)
        rgb = rgb * (1 - e[..., None]) + np.array([0.03, 0.03, 0.03]) * e[..., None]
        a = np.maximum(a, e * 0.85)
    im = Image.fromarray((np.dstack([rgb, a]) * 255).clip(0, 255).astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(im)
    for _ in range(scratch):
        x, y = RNG.uniform(0, w), RNG.uniform(0, h)
        ang, L = RNG.uniform(0, math.pi), RNG.uniform(15, 120)
        d.line([(x, y), (x + L * math.cos(ang), y + L * math.sin(ang))], fill=(175, 172, 165, 230), width=1)
    return im


# ---- rvmats ----------------------------------------------------------------------------------------------------------
def rvmat_super(spec, power, macro, detail, shine, diffuse) -> str:
    """the vanilla Super shader: our colour texture (the face's), a detail map, a damage macro over it"""
    d, s = f"{diffuse:g}", f"{spec * shine:.3f}"

    def stage(n, tex, uv="tex", tile=1):
        tr = (f"\tclass uvTransform\n\t{{\n\t\taside[] = {{{tile}, 0, 0}};\n\t\tup[] = {{0, {tile}, 0}};\n"
              f"\t\tdir[] = {{0, 0, 0}};\n\t\tpos[] = {{0, 0, 0}};\n\t}};\n") if uv == "tex" else ""
        return f"class Stage{n}\n{{\n\ttexture = \"{tex}\";\n\tuvSource = \"{uv}\";\n{tr}}};\n"
    return (f"ambient[] = {{{d}, {d}, {d}, 1}};\ndiffuse[] = {{{d}, {d}, {d}, 1}};\nforcedDiffuse[] = {{0, 0, 0, 0}};\n"
            f"emmisive[] = {{0, 0, 0, 1}};\nspecular[] = {{{s}, {s}, {s}, 1}};\nspecularPower = {max(4, power * shine):g};\n"
            "PixelShaderID = \"Super\";\nVertexShaderID = \"Super\";\n"
            + stage(1, "#(argb,8,8,3)color(0.5,0.5,1,1,NOHQ)")
            + stage(2, f"#(argb,8,8,3)color({detail},{detail},{detail},1,DT)", tile=8)
            + stage(3, macro, tile=1)
            + stage(4, "#(argb,8,8,3)color(1,1,1,1,AS)")
            + stage(5, f"#(argb,8,8,3)color(1,{0.5 * shine:.2f},{0.3 * shine:.2f},1,SMDI)")
            + stage(6, "#(ai,64,64,1)fresnel(1.5,0.5)", "none")
            + stage(7, "#(argb,8,8,3)color(0,0,0,1,CO)", "none"))


def rvmat_glow(r, g, b, power) -> str:
    return (f"ambient[] = {{0, 0, 0, 1}};\ndiffuse[] = {{0, 0, 0, 1}};\nforcedDiffuse[] = {{0, 0, 0, 0}};\n"
            f"emmisive[] = {{{r * power:.3f}, {g * power:.3f}, {b * power:.3f}, 1}};\nspecular[] = {{0, 0, 0, 0}};\n"
            "specularPower = 1;\nPixelShaderID = \"Normal\";\nVertexShaderID = \"Basic\";\n")


def build_mode_lamp(colour) -> Mesh:
    """the portal gun's lit mode lamp: a glowing dome a little over the unlit one, laid on it by script (a child
    of the gun, at its lamp_* memory point)"""
    m = Mesh()
    # centred on its origin: a model without a geometry level is centred on its bounding box anyway (the script
    # puts it PG_PortalGun.LAMP_LIFT over the lamp's memory point)
    cyl(m, (0.0, -0.0035, 0.0), (0.0, 0.0035, 0.0), 0.0064, 0.0036, LAMPS[colour], "lamp", sides=12)
    return m


def write_rvmat(name, text):
    src = BUILD / f"{name}.rvmat.txt"
    src.write_text(text)
    subprocess.run([str(ARMAKE), "rapify", "-f", str(src), str(DATA / f"{name}.rvmat")], check=True)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    # dulled paint, then weathered: rust and scratches over everything, most on the bare steel
    for name, im in (("steel", weathered(tex_steel(512), rust=0.35, scratch=60, dirt=0.3, bare=(175, 175, 172))),
                     ("black", weathered(tex_plastic((0.085, 0.085, 0.09), 512, amount=0.25), rust=0.18, scratch=70,
                                         dirt=0.15, bare=(110, 108, 104))),
                     ("orange", weathered(tex_plastic((0.58, 0.32, 0.11), 512, amount=0.06), rust=0.22, scratch=90,
                                          dirt=0.3)),
                     ("white", weathered(tex_plastic((0.74, 0.73, 0.69), 512, amount=0.03), rust=0.12, scratch=80,
                                         dirt=0.35, bare=(120, 118, 112))),
                     ("grip", weathered(tex_grip(512), rust=0.08, scratch=10, dirt=0.2, bare=(80, 78, 74))),
                     ("gg_name", weathered(tex_gg_name(), rust=0.12, scratch=50, dirt=0.3)),
                     ("gg_warn", weathered(tex_gg_warn(), rust=0.15, scratch=50, dirt=0.35)),
                     ("pg_name", weathered(tex_pg_name(), rust=0.08, scratch=40, dirt=0.35))):
        write_tex(name, im)
    for name, im in (("gun_damaged_mc", tex_macro(0.0, 0.0, 120, 11)), ("gun_badly_mc", tex_macro(0.35, 0.0, 60, 21)),
                     ("gun_ruined_mc", tex_macro(0.6, 0.35, 40, 31))):
        TEX.mkdir(parents=True, exist_ok=True)
        (TEX / f"{name}.paa").write_bytes(paa.encode(im, dxt5=True))
    for suf, (macro, detail, shine, diffuse) in CONDITIONS.items():
        for name, (spec, power) in SURFACES.items():
            write_rvmat(name + suf, rvmat_super(spec, power, macro, detail, shine, diffuse))
        for name, (r, g, b, power) in GLOWS.items():
            write_rvmat(name + suf, rvmat_glow(r, g, b, power * GLOW_LEFT[suf]))
    for name, mesh, front in (("gravity_gun", build_gravity_gun(), -0.42), ("portal_gun", build_portal_gun(), -0.37)):
        geo, mass = build_gun_geometry(front)
        lods = [(1.0, mesh, {}, None), (1e13, geo, {"autocenter": "0"}, mass), (1e15, build_gun_memory(front), {}, None)]
        for _, lm, _, _ in lods:                         # a weapon's MLOD comes in unturned: undo write_mlod's turn
            lm.pts = [(-x, y, -z) for x, y, z in lm.pts]
        (DATA / f"{name}.p3d").write_bytes(write_mlod(lods))
        print(f"{name}.p3d: {len(mesh.pts)} pts {len(mesh.faces)} faces")
    for colour in ("blue", "orange"):
        (DATA / f"portal_lamp_{colour}.p3d").write_bytes(write_mlod([(1.0, build_mode_lamp(colour), {}, None)]))


if __name__ == "__main__":
    main()
