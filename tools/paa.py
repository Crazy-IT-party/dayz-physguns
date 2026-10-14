#!/usr/bin/env python3
"""Write .paa, the texture format the game reads (DXT1/DXT5 in LZO-compressed mipmaps), without DayZ Tools.

The format, read off a real one (layers/S_016_013_lco.paa of Chernarus):

    u16   type: 0xFF01 = DXT1, 0xFF05 = DXT5
    tags  each "GGAT" + a four byte name reversed + u32 length + payload:
            AVGCTAGG  average colour, used for the lowest level of detail
            MAXCTAGG  maximum colour
            FLAGTAGG  1 when the texture has meaningful alpha (the masks do)
            OFFSTAGG  16 u32 offsets, one per mipmap, zero where there is none
    u16   palette size, always zero here
    then  each mipmap: u16 width, u16 height, u24 length, data
          width carries 0x8000 when the data is LZO compressed, which it is above a few hundred bytes
    end   six zero bytes

The blocks themselves are ordinary DXT, which Pillow writes as DDS; the 128 byte DDS header is dropped and
what remains is exactly what a mipmap holds.
"""
from __future__ import annotations
import io
import struct
from pathlib import Path

try:
    import lzo
except ImportError:                                # the caller gets a clear failure instead of a bad texture
    lzo = None
from PIL import Image

DXT1, DXT5 = 0xFF01, 0xFF05
DDS_HEADER = 128


def _tag(name: str, payload: bytes) -> bytes:
    """A tagg: its eight character name written backwards - AVGCTAGG is stored as GGATCGVA - then the payload
    with its length. The marker and the name are the same eight bytes, not a marker plus a name."""
    return name[::-1].encode("ascii") + struct.pack("<I", len(payload)) + payload


def _blocks(im: Image.Image, fmt: str) -> bytes:
    buf = io.BytesIO()
    im.save(buf, format="DDS", pixel_format=fmt)
    return buf.getvalue()[DDS_HEADER:]


def _mips(im: Image.Image, fmt: str, smallest: int = 4):
    """Every level from the full size down to 4x4, which is one DXT block."""
    w, h = im.size
    while w >= smallest and h >= smallest:
        yield im.resize((w, h), Image.LANCZOS) if im.size != (w, h) else im
        w, h = w // 2, h // 2


def encode(img: Image.Image, dxt5: bool | None = None) -> bytes:
    """One image as .paa bytes. DXT5 is chosen for anything with real alpha - that is what the masks are."""
    if lzo is None:
        raise RuntimeError("python-lzo is needed to write .paa")
    im = img.convert("RGBA")
    if dxt5 is None:
        dxt5 = im.getchannel("A").getextrema()[0] < 255
    fmt, kind = ("DXT5", DXT5) if dxt5 else ("DXT1", DXT1)

    levels = []
    for level in _mips(im, fmt):
        raw = _blocks(level, fmt)
        packed = lzo.compress(raw, 9, False)        # no python-lzo header: the game wants bare LZO1X
        if len(packed) < len(raw):
            levels.append((level.width | 0x8000, level.height, packed))
        else:
            levels.append((level.width, level.height, raw))

    # the average and maximum colour, which the engine uses when a surface is too far away to texture
    small = im.resize((1, 1), Image.LANCZOS).getpixel((0, 0))
    avg = bytes((small[2], small[1], small[0], small[3]))
    head = struct.pack("<H", kind) + _tag("AVGCTAGG", avg) + _tag("MAXCTAGG", b"\xff\xff\xff\xff")
    if dxt5:
        head += _tag("FLAGTAGG", struct.pack("<I", 1))
    head += _tag("OFFSTAGG", b"\0" * 64) + struct.pack("<H", 0)

    # offsets can only be filled in once the header length is known, so lay the body out first
    offsets, body, at = [], bytearray(), len(head)
    for w, h, data in levels:
        offsets.append(at)
        body += struct.pack("<HH", w, h) + struct.pack("<I", len(data))[:3] + data
        at += 7 + len(data)
    body += b"\0" * 6

    off = struct.pack("<16I", *(offsets + [0] * (16 - len(offsets)))[:16])
    head = head.replace(_tag("OFFSTAGG", b"\0" * 64), _tag("OFFSTAGG", off), 1)
    return bytes(head) + bytes(body)


def write(path: str | Path, img: Image.Image, dxt5: bool | None = None) -> int:
    data = encode(img, dxt5)
    Path(path).write_bytes(data)
    return len(data)


def describe(path: str | Path) -> dict:
    """Read back enough of a .paa to check one: its type, tags and mipmap sizes."""
    b = Path(path).read_bytes()
    kind, = struct.unpack_from("<H", b, 0)
    i, tags = 2, {}
    while b[i:i + 4] == b"GGAT":
        name = b[i:i + 8][::-1].decode("ascii")
        n, = struct.unpack_from("<I", b, i + 8)
        tags[name] = b[i + 12:i + 12 + n]
        i += 12 + n
    offs = struct.unpack("<16I", tags["OFFSTAGG"]) if "OFFSTAGG" in tags else ()
    mips = []
    for o in offs:
        if not o or o + 7 > len(b):
            break
        w, h = struct.unpack_from("<HH", b, o)
        size = int.from_bytes(b[o + 4:o + 7], "little")
        mips.append((w & 0x7FFF, h, size, bool(w & 0x8000)))
    return {"type": hex(kind), "tags": sorted(tags), "mips": mips, "bytes": len(b)}


def read(path: str | Path) -> Image.Image:
    """Decode the largest mipmap of a DXT1/DXT5 .paa back into an image, by wrapping its blocks in a DDS
    header and letting Pillow do the DXT part."""
    b = Path(path).read_bytes()
    kind, = struct.unpack_from("<H", b, 0)
    i, tags = 2, {}
    while b[i:i + 4] == b"GGAT":
        name = b[i:i + 8][::-1].decode("ascii")
        n, = struct.unpack_from("<I", b, i + 8)
        tags[name] = b[i + 12:i + 12 + n]
        i += 12 + n
    offs = [x for x in struct.unpack("<16I", tags["OFFSTAGG"]) if x] if "OFFSTAGG" in tags else [i + 2]
    o = max(offs, key=lambda x: struct.unpack_from("<H", b, x)[0] & 0x7FFF)
    w, h = struct.unpack_from("<HH", b, o)
    size = int.from_bytes(b[o + 4:o + 7], "little")
    data = b[o + 7:o + 7 + size]
    packed, w = bool(w & 0x8000), w & 0x7FFF
    blocks = (w // 4) * (h // 4) * (8 if kind == DXT1 else 16)
    if packed:
        data = lzo.decompress(data, False, blocks)
    fourcc = b"DXT1" if kind == DXT1 else b"DXT5"
    hdr = struct.pack("<4sIIIIIII44sII4s20sI16s", b"DDS ", 124, 0x81007, h, w, len(data), 0, 1,
                      b"\0" * 44, 32, 4, fourcc, b"\0" * 20, 0x1000, b"\0" * 16)
    im = Image.open(io.BytesIO(hdr + data))
    im.load()
    return im


if __name__ == "__main__":
    import sys
    src, dst = sys.argv[1], sys.argv[2]
    n = write(dst, Image.open(src))
    print(f"{src} -> {dst}  {n} bytes")
    for k, v in describe(dst).items():
        print(f"  {k}: {v}")
