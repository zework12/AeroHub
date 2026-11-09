"""Dump the Lua-API name -> handler table of Forts.exe from the registration code
(`lea r8,[handler]` ... `lea rdx,[name]`). Static, read-only. Usage: python -I dump_api.py <Forts.exe> <out.tsv>"""
import re
import struct
import sys

exe = open(sys.argv[1], "rb").read()
out_path = sys.argv[2]

e_lfanew = struct.unpack_from("<I", exe, 0x3C)[0]
nsec = struct.unpack_from("<H", exe, e_lfanew + 6)[0]
opt_size = struct.unpack_from("<H", exe, e_lfanew + 20)[0]
opt = e_lfanew + 24
secs = []
for i in range(nsec):
    o = e_lfanew + 24 + opt_size + i * 40
    name = exe[o:o + 8].rstrip(b"\0").decode()
    vsize, va, rsize, raw = struct.unpack_from("<IIII", exe, o + 8)
    chars = struct.unpack_from("<I", exe, o + 36)[0]
    secs.append((name, va, vsize, raw, rsize, chars))


def rva_to_raw(rva):
    for name, va, vsize, raw, rsize, chars in secs:
        if va <= rva < va + max(vsize, rsize):
            return raw + (rva - va)
    return None


def in_text(rva):
    for name, va, vsize, raw, rsize, chars in secs:
        if chars & 0x20000000 and name == ".text" and va <= rva < va + vsize:
            return True
    return False


text = next(s for s in secs if s[0] == ".text")
_, tva, tvs, traw, trs, _ = text
blob = exe[traw:traw + trs]
ident = re.compile(rb"^[A-Za-z_][A-Za-z0-9_]{2,63}$")

# lea r8,[rip+d] = 4C 8D 05 d32 ; lea rdx,[rip+d] = 48 8D 15 d32 ; the pair appears as r8 first, rdx within the next 16 bytes
table = {}
for m in re.finditer(rb"\x4C\x8D\x05", blob, flags=re.DOTALL):
    i = m.start()
    if i + 7 > len(blob):
        continue
    d1 = struct.unpack_from("<i", blob, i + 3)[0]
    handler = tva + i + 7 + d1
    if not in_text(handler):
        continue
    window = blob[i + 7:i + 7 + 16]
    j = window.find(b"\x48\x8D\x15")
    if j < 0:
        continue
    k = i + 7 + j
    d2 = struct.unpack_from("<i", blob, k + 3)[0]
    srva = tva + k + 7 + d2
    soff = rva_to_raw(srva)
    if soff is None:
        continue
    end = exe.find(b"\0", soff, soff + 80)
    if end < 0:
        continue
    s = exe[soff:end]
    if not ident.match(s):
        continue
    hoff = rva_to_raw(handler)
    pro = exe[hoff:hoff + 16].hex(" ")
    table.setdefault(s.decode(), set()).add((handler, pro))

with open(out_path, "w", encoding="utf-8") as f:
    for name in sorted(table):
        for handler, pro in sorted(table[name]):
            f.write(f"{name}\t0x{handler:X}\t{pro}\n")
print(f"{len(table)} names, {sum(len(v) for v in table.values())} handlers -> {out_path}")
