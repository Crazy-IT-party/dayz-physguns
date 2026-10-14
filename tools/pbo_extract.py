"""Read Bohemia PBO archives (list / extract), no DayZ Tools needed.

  python3 tools/pbo_extract.py list  <file.pbo> [substring]
  python3 tools/pbo_extract.py get   <file.pbo> <entry name or substring> <out dir>
  python3 tools/pbo_extract.py getall <file.pbo> <out dir> [substring]

Header: entries of (name\\0, packing u32, original u32, reserved u32, timestamp u32, data u32); the first entry may be
a "prefix" property block (empty name, packing 0x56657273 'Vers', then key\\0value\\0 pairs ending with an empty key);
an empty-name entry with zero sizes ends the header, then the data of every entry follows in order.
Packing 0x43707273 ('Cprs') = LZSS-compressed entry (decoded here); anything else = stored.
"""
import struct, sys
from pathlib import Path

def read_cstr(b, i):
    j = b.index(b"\0", i); return b[i:j].decode("latin-1"), j + 1

def lzss(src, out_size):
    """Bohemia LZSS: flag byte, bit=1 raw byte, bit=0 (offset,length) pair; 4-byte checksum trails the stream."""
    out = bytearray(); i = 0; n = len(src)
    while len(out) < out_size and i < n:
        flags = src[i]; i += 1
        for bit in range(8):
            if len(out) >= out_size or i >= n: break
            if flags & (1 << bit):
                out.append(src[i]); i += 1
            else:
                lo, hi = src[i], src[i + 1]; i += 2
                rpos = lo + ((hi & 0xF0) << 4); rlen = (hi & 0x0F) + 3
                pos = len(out) - rpos
                if pos < 0:                     # references before the start decode as spaces
                    for _ in range(rlen):
                        out.append(0x20 if pos < 0 else out[pos]); pos += 1
                else:
                    for _ in range(rlen): out.append(out[pos]); pos += 1
    return bytes(out[:out_size])

def parse(path):
    b = Path(path).read_bytes(); i = 0; entries = []; props = {}
    while True:
        name, i = read_cstr(b, i)
        packing, orig, res, ts, size = struct.unpack_from("<IIIII", b, i); i += 20
        if name == "" and packing == 0x56657273:
            while True:
                k, i = read_cstr(b, i)
                if k == "": break
                v, i = read_cstr(b, i); props[k] = v
            continue
        if name == "" and size == 0: break
        entries.append([name, packing, orig, size])
    off = i
    for e in entries: e.append(off); off += e[3]
    return b, props, entries

def data_of(b, e):
    name, packing, orig, size, off = e; raw = b[off:off + size]
    return lzss(raw, orig) if packing == 0x43707273 else raw

if __name__ == "__main__":
    cmd, pbo = sys.argv[1], sys.argv[2]
    b, props, entries = parse(pbo)
    if cmd == "list":
        sub = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        print("prefix:", props.get("prefix"), "| entries:", len(entries))
        for e in entries:
            if sub in e[0].lower(): print(f"{e[3]:>10}  {'C' if e[1] == 0x43707273 else ' '}  {e[0]}")
    elif cmd in ("get", "getall"):
        if cmd == "get": sub, out = sys.argv[3].lower(), Path(sys.argv[4]); ents = [e for e in entries if sub in e[0].lower()]
        else: out = Path(sys.argv[3]); sub = sys.argv[4].lower() if len(sys.argv) > 4 else ""; ents = [e for e in entries if sub in e[0].lower()]
        for e in ents:
            p = out / e[0].replace("\\", "/"); p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data_of(b, e)); print("wrote", p, len(data_of(b, e)))
