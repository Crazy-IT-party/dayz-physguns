#!/usr/bin/env python3
"""PhysGuns models and materials, made here (no Object Builder, no Binarize).

    python3 -I tools/build_models.py        -> mod/PhysGuns/data/{portal_blue,portal_orange,portal_ghost,beam_seg,beam_arc}.p3d + *.rvmat

A portal: a thin elliptical ring with a translucent fill, in the model's XY plane, facing +Z (the script turns
+Z to the surface normal). No geometry level - nothing collides with it. The colours glow: the rvmats are
emissive.
A beam segment: a short glowing tube along +Z (SEG_LEN long) - the gravity gun's beam is a chain of them, laid
along a wavering curve by the script (PG_Fx.c): a bright core in a translucent blue sheath, and a thinner
"arc" for the crackle around it. MLOD writer as in dayz-shashka (the engine turns an MLOD 180 degrees about Y; written turned back).
"""
from __future__ import annotations

import math
import os
import shutil
import struct
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "mod/PhysGuns/data"
BUILD = ROOT / "build"
PFX = "physguns\\data\\"
ARMAKE = os.environ.get("ARMAKE2") or shutil.which("armake2") or os.path.expanduser("~/.cargo/bin/armake2")


# ---- mesh ---------------------------------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.pts: list[tuple[float, float, float]] = []
        self.faces: list[dict] = []      # {"idx", "uv", "tex", "mat", "group"}
        self.sel: dict[str, set[int]] = {}
        self.track: str | None = None             # points made while set go into that named selection

    def pt(self, p) -> int:
        self.pts.append((float(p[0]), float(p[1]), float(p[2])))
        if self.track:
            self.sel.setdefault(self.track, set()).add(len(self.pts) - 1)
        return len(self.pts) - 1

    def face(self, idx, uv, tex, mat, group, outward_hint=None):
        """idx counter-clockwise seen from outside; with outward_hint (a point inside) the order is fixed up"""
        if outward_hint is not None:
            P = np.array([self.pts[i] for i in idx])
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if len(idx) == 4:
                n = n + np.cross(P[2] - P[0], P[3] - P[0])
            if np.dot(n, P.mean(0) - np.asarray(outward_hint)) < 0:
                idx, uv = idx[::-1], uv[::-1]
        self.faces.append({"idx": list(idx), "uv": list(uv), "tex": tex, "mat": mat, "group": group})
        return len(self.faces) - 1

    def normal(self, f) -> np.ndarray:
        P = np.array([self.pts[i] for i in f["idx"]])
        n = np.cross(P[1] - P[0], P[2] - P[0])
        if len(P) == 4:
            n = n + np.cross(P[2] - P[0], P[3] - P[0])
        return n                                   # area weighted, outward


def write_mlod(lods) -> bytes:
    """lods: [(resolution, Mesh, properties, masses or None)]"""
    out = [b"MLOD", struct.pack("<II", 0x101, len(lods))]
    for res, m, props, mass in lods:
        # one normal per (point, smoothing group): the sum of the group's face normals at that point
        acc: dict[tuple[int, str], np.ndarray] = {}
        fn = [m.normal(f) for f in m.faces]
        for f, n in zip(m.faces, fn):
            for i in f["idx"]:
                acc[(i, f["group"])] = acc.get((i, f["group"]), 0) + n
        keys = list(acc)
        nidx = {k: j for j, k in enumerate(keys)}
        out.append(b"P3DM" + struct.pack("<IIIIII", 0x1C, 0x100, len(m.pts), len(keys), len(m.faces), 0))
        for x, y, z in m.pts:                                       # the engine turns an MLOD 180 deg about Y
            out.append(struct.pack("<fffI", -x, y, -z, 0))
        for k in keys:
            n = acc[k]
            n = n / max(np.linalg.norm(n), 1e-12)
            out.append(struct.pack("<fff", n[0], -n[1], n[2]))         # stored inward, turned as the points
        for f in m.faces:
            idx, uv = f["idx"][::-1], f["uv"][::-1]                    # clockwise seen from outside
            out.append(struct.pack("<I", len(idx)))
            for k in range(4):
                if k < len(idx):
                    out.append(struct.pack("<IIff", idx[k], nidx[(idx[k], f["group"])], uv[k][0], uv[k][1]))
                else:
                    out.append(struct.pack("<IIff", 0, 0, 0.0, 0.0))
            out.append(struct.pack("<I", 0) + f["tex"].encode("latin1") + b"\0" + f["mat"].encode("latin1") + b"\0")
        out.append(b"TAGG")

        def tagg(name, data):
            out.append(b"\x01" + name.encode("latin1") + b"\0" + struct.pack("<I", len(data)) + data)
        for k, v in props.items():
            tagg("#Property#", k.encode("latin1").ljust(64, b"\0") + str(v).encode("latin1").ljust(64, b"\0"))
        for name, sel in m.sel.items():
            pm = bytearray(len(m.pts))
            fm = bytearray(len(m.faces))
            for i in sel:
                pm[i] = 1
            for fi, f in enumerate(m.faces):
                if all(i in sel for i in f["idx"]):
                    fm[fi] = 1
            tagg(name, bytes(pm) + bytes(fm))
        if mass is not None:
            tagg("#Mass#", struct.pack(f"<{len(m.pts)}f", *mass))
        tagg("#EndOfFile#", b"")
        out.append(struct.pack("<f", res))
    return b"".join(out)



# ---- portal -------------------------------------------------------------------------------------------------------
RX, RY = 0.66, 1.08          # outer half width / height
RIM = 0.09                   # ring width
SEG = 64


def ellipse(rx, ry, z):
    return [(rx * math.cos(2 * math.pi * k / SEG), ry * math.sin(2 * math.pi * k / SEG), z) for k in range(SEG)]


def build_portal(colour: str, rim_alpha=1.0, fill_alpha=0.45) -> Mesh:
    m = Mesh()
    rim_tex = f"#(argb,8,8,3)color({colour},1,CO)" if rim_alpha >= 1 else f"#(argb,8,8,3)color({colour},{rim_alpha},CA)"
    fill_tex = f"#(argb,8,8,3)color({colour},{fill_alpha},CA)"
    rim_mat = PFX + f"portal_rim_{NAME[colour]}.rvmat"
    fill_mat = PFX + f"portal_fill_{NAME[colour]}.rvmat"
    # a House model is not turned by the engine (like a weapon, unlike an item), but write_mlod turns it: built
    # facing -Z here, it faces +Z - its direction, out of the surface - in the game
    outer = [m.pt(p) for p in ellipse(RX, RY, -0.02)]
    inner = [m.pt(p) for p in ellipse(RX - RIM, RY - RIM, -0.02)]
    behind = (0.0, 0.0, 1.0)                                   # faces point to -Z: "inside" is behind
    for k in range(SEG):
        j = (k + 1) % SEG
        m.face([outer[k], outer[j], inner[j], inner[k]], [(0, 0)] * 4, rim_tex, rim_mat, "rim", behind)
    c = m.pt((0.0, 0.0, -0.01))
    fill = [m.pt(p) for p in ellipse(RX - RIM + 0.005, RY - RIM + 0.005, -0.01)]
    for k in range(SEG):
        j = (k + 1) % SEG
        m.face([c, fill[k], fill[j]], [(0.5, 0.5), (0, 0), (0, 0)], fill_tex, fill_mat, "fill", behind)
    return m


NAME = {}


# ---- beam ----------------------------------------------------------------------------------------------------
SEG_LEN = 0.2                # keep PG_Fx.SEG_LEN the same
BEAM_SIDES = 8


def tube(m: Mesh, r, tex, mat, group):
    ring0 = [m.pt((r * math.cos(2 * math.pi * k / BEAM_SIDES), r * math.sin(2 * math.pi * k / BEAM_SIDES), 0.0))
             for k in range(BEAM_SIDES)]
    ring1 = [m.pt((r * math.cos(2 * math.pi * k / BEAM_SIDES), r * math.sin(2 * math.pi * k / BEAM_SIDES), SEG_LEN))
             for k in range(BEAM_SIDES)]
    for k in range(BEAM_SIDES):
        j = (k + 1) % BEAM_SIDES
        mid = (0.0, 0.0, SEG_LEN * 0.5)
        m.face([ring0[k], ring0[j], ring1[j], ring1[k]], [(0, 0)] * 4, tex, mat, group, mid)


def build_beam() -> Mesh:
    m = Mesh()
    tube(m, 0.016, "#(argb,8,8,3)color(0.75,0.9,1,1,CO)", PFX + "beam_core.rvmat", "core")
    tube(m, 0.045, "#(argb,8,8,3)color(0.2,0.55,1,0.35,CA)", PFX + "beam_glow.rvmat", "glow")
    return m


def build_arc() -> Mesh:
    m = Mesh()
    tube(m, 0.006, "#(argb,8,8,3)color(0.6,0.85,1,1,CO)", PFX + "beam_core.rvmat", "core")
    return m


def rvmat_glow(r, g, b, power) -> str:
    return (f"ambient[] = {{0, 0, 0, 1}};\ndiffuse[] = {{0, 0, 0, 1}};\nforcedDiffuse[] = {{0, 0, 0, 0}};\n"
            f"emmisive[] = {{{r * power:.3f}, {g * power:.3f}, {b * power:.3f}, 1}};\nspecular[] = {{0, 0, 0, 0}};\n"
            "specularPower = 1;\nPixelShaderID = \"Normal\";\nVertexShaderID = \"Basic\";\n")


def main():
    DATA.mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)
    # name: colour, rim and fill opacity, rim and fill glow. The ghost: the portal gun's aim (PG_PortalAim), faint
    colours = {"blue": ((0.15, 0.55, 1.0), 1.0, 0.45, 6.0, 2.0), "orange": ((1.0, 0.5, 0.08), 1.0, 0.45, 6.0, 2.0),
               "ghost": ((0.85, 0.92, 1.0), 0.5, 0.12, 2.5, 1.0)}
    for name, ((r, g, b), rim_a, fill_a, rim_p, fill_p) in colours.items():
        key = f"{r},{g},{b}"
        NAME[key] = name
        mesh = build_portal(key, rim_a, fill_a)
        (DATA / f"portal_{name}.p3d").write_bytes(write_mlod([(1.0, mesh, {}, None)]))
        for part, power in (("rim", rim_p), ("fill", fill_p)):
            src = BUILD / f"portal_{part}_{name}.rvmat.txt"
            src.write_text(rvmat_glow(r, g, b, power))
            subprocess.run([str(ARMAKE), "rapify", "-f", str(src), str(DATA / f"portal_{part}_{name}.rvmat")], check=True)
        print(f"portal_{name}.p3d: {len(mesh.pts)} pts {len(mesh.faces)} faces")
    for name, mesh in (("beam_seg", build_beam()), ("beam_arc", build_arc())):
        (DATA / f"{name}.p3d").write_bytes(write_mlod([(1.0, mesh, {}, None)]))
    for name, (r, g, b, power) in (("beam_core", (0.6, 0.85, 1.0, 9.0)), ("beam_glow", (0.15, 0.5, 1.0, 4.0))):
        src = BUILD / f"{name}.rvmat.txt"
        src.write_text(rvmat_glow(r, g, b, power))
        subprocess.run([str(ARMAKE), "rapify", "-f", str(src), str(DATA / f"{name}.rvmat")], check=True)


if __name__ == "__main__":
    main()
