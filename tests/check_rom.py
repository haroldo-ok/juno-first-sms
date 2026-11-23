"""Check that banked data really made it into the ROM (ihx2sms keeps only the
first visit to each bank, so a bad link order silently drops data)."""
import re, sys
noi, rom_path, srcs = sys.argv[1], sys.argv[2], sys.argv[3:]
syms = {}
for l in open(noi):
    p = l.split()
    if len(p) == 3 and p[0] == 'DEF': syms[p[1].lstrip('_')] = int(p[2], 16)
rom = open(rom_path, 'rb').read()
bad = 0
for src in srcs:
    for m in re.finditer(r'(\w+)\[(\d+)\] = \{([^}]*)\}', open(src).read()):
        name = m.group(1)
        vals = bytes(int(v, 0) for v in m.group(3).replace('\n', '').split(',') if v.strip())
        a = syms[name]
        off = (a >> 16) * 16384 + ((a & 0xFFFF) - 0x8000) if a > 0xFFFF else a
        ok = rom[off:off + len(vals)] == vals
        bad += not ok
        print("%-14s bank %d  %5d bytes  %s" % (name, a >> 16, len(vals), "ok" if ok else "MISSING/WRONG"))
print("RESULT:", "ALL PASSED" if not bad else "%d FAILURE(S)" % bad)
sys.exit(1 if bad else 0)
