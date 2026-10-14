r"""Pack a DayZ mod folder into a .pbo without DayZ Tools (works on Linux).

  python3 tools/build_pbo.py mod/PhysGuns dist/@PhysGuns/addons/physguns.pbo

PBO layout: header entries (name, packing 0, orig size, reserved, timestamp, data size) with a leading
"prefix" property block taken from $PBOPREFIX$, a zero entry terminator, the file data, then 0x00 + SHA1
of everything before it. Files are stored uncompressed (packing method 0), which the engine accepts.
config.cpp stays as text: DayZ reads unbinarised configs, so no binarize / rapify step is needed.
Models go in as MLOD (.p3d), which the engine also reads unbinarised.
"""
from __future__ import annotations
import hashlib, struct, sys, time
from pathlib import Path

INCLUDE = {".p3d", ".c", ".cpp", ".json", ".txt", ".md", ".hpp", ".h", ".layout", ".paa", ".rvmat", ".xml", ".csv", ".ogg", ".wss"}


def build(src: Path, out: Path) -> None:
    src = src.resolve()
    prefix_file = src / "$PBOPREFIX$"
    prefix = prefix_file.read_text(encoding="utf-8").strip().replace("/", "\\") if prefix_file.exists() else src.name
    files = sorted(p for p in src.rglob("*") if p.is_file() and p.suffix.lower() in INCLUDE and p.name not in ("$PBOPREFIX$", "mod.cpp"))

    def entry(name: bytes, packing: int, orig: int, reserved: int, ts: int, size: int) -> bytes:
        return name + b"\x00" + struct.pack("<IIIII", packing, orig, reserved, ts, size)

    buf = bytearray()
    # product entry: a zero-named "Vers" entry followed by properties, ended by an empty string
    buf += entry(b"", 0x56657273, 0, 0, 0, 0)
    buf += b"prefix\x00" + prefix.encode("utf-8") + b"\x00"
    buf += b"\x00"
    blobs = []
    for p in files:
        rel = p.relative_to(src).as_posix().replace("/", "\\").encode("utf-8")
        data = p.read_bytes()
        buf += entry(rel, 0, len(data), 0, int(p.stat().st_mtime), len(data))
        blobs.append(data)
    buf += entry(b"", 0, 0, 0, 0, 0)          # terminator
    for data in blobs:
        buf += data
    digest = hashlib.sha1(bytes(buf)).digest()
    buf += b"\x00" + digest
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(buf))
    print(f"{out}: {len(files)} files, {len(buf)} bytes, prefix {prefix}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build(Path(sys.argv[1]), Path(sys.argv[2]))
