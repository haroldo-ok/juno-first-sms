#!/usr/bin/env python3
"""Generates src/gfx.h: sprite tiles, perspective-field tiles, logo, LUTs."""
import math, random, sys

PAL = {'.':0,'Y':1,'L':2,'D':3,'R':4,'O':5,'W':6,'G':7,'C':8,'B':9,'M':10,
       'r':11,'b':12,'P':13,'g':14,'K':15}

def art(s):
    rows = [r for r in s.strip('\n').split('\n')]
    assert len(rows) == 16, len(rows)
    for i, r in enumerate(rows):
        assert len(r) == 16, (i, r, len(r))
    return [[PAL[c] for c in r] for r in rows]

PLAYER = art("""
.......WW.......
.......WW.......
......WCCW......
......WCCW......
.....WLCCLW.....
.....WLLLLW.....
....WWLLLLWW....
.R..WLLWWLLW..R.
.R.WWLLWWLLWW.R.
.WWWLLLLLLLLWWW.
WWLLLLLLLLLLLLWW
WLLDWWLLLLWWDLLW
WWWWW.WLLW.WWWWW
.RR...WWWW...RR.
.OO....OO....OO.
.O......O.....O.
""")

SAUCER = art("""
................
.....GGGGGG.....
....GGggggGG....
...GGgWWWWgGG...
...GgWWCCWWgG...
..YYYYYYYYYYYY..
.YYRYYRYYRYYRYY.
YYYYYYYYYYYYYYYY
YYYYYYYYYYYYYYYY
.YYRYYRYYRYYRYY.
..OOOOOOOOOOOO..
...OO..OO..OO...
....O..OO..O....
................
................
................
""")

DART = art("""
R..............R
RR............RR
RRR....MM....RRR
.RRR..MMMM..RRR.
.RRRRMMWWMMRRRR.
..RRRMWWWWMRRR..
..RRRMWKKWMRRR..
...RRMMKKMMRR...
...RRRMMMMRRR...
....RRRMMRRR....
....rRRRRRRr....
.....rRRRRr.....
.....rrRRrr.....
......rRRr......
......rrrr......
.......rr.......
""")

SPINNER = art("""
.......CC.......
......CBBC......
.....CBBBBC.....
..C..CBWWBC..C..
.CBC..CWWC..CBC.
CBBBC.CBBC.CBBBC
CBWWBCCBBCCBWWBC
.CWWBBBWWBBBWWC.
.CWWBBBWWBBBWWC.
CBWWBCCBBCCBWWBC
CBBBC.CBBC.CBBBC
.CBC..CWWC..CBC.
..C..CBWWBC..C..
.....CBBBBC.....
......CBBC......
.......CC.......
""")

BOMBER = art("""
..OO........OO..
.OOOO......OOOO.
.OO.OO....OO.OO.
....OOOOOOOO....
...OOPPPPPPOO...
..OOPPPPPPPPOO..
.OOPWWPPPPWWPOO.
.OOPWKPPPPKWPOO.
.OOPPPPPPPPPPOO.
.OOPPRRRRRRPPOO.
..OOPPRRRRPPOO..
...OOPPPPPPOO...
.OO.OOOOOOOO.OO.
OO...OO..OO...OO
O....O....O....O
................
""")

ASTRO = art("""
......WWWW......
.....WWWWWW.....
.....WCCCCW.....
.....WCCCCW.....
.....WWWWWW.....
..WW..WWWW..WW..
..WWWWWWWWWWWW..
...WWWRWWRWWW...
......WWWW......
......WWWW......
......WWWW......
.....WWWWWW.....
.....WW..WW.....
.....WW..WW.....
....WWW..WWW....
....LLL..LLL....
""")

ASTRO2 = art("""
......WWWW......
.WW..WWWWWW..WW.
.WW..WYYYYW..WW.
..WW.WYYYYW.WW..
..WW.WWWWWW.WW..
...WW.WWWW.WW...
...WWWWWWWWWW...
....WWRWWRWW....
......WWWW......
......WWWW......
......WWWW......
.....WWWWWW.....
....WWW..WWW....
...WWW....WWW...
...WW......WW...
...LL......LL...
""")

def swap(img, m):
    return [[m.get(p, p) for p in row] for row in img]

def scale(img, n):
    """nearest-ish downscale of 16x16 to n x n, centred in a 16x16 canvas"""
    out = [[0]*16 for _ in range(16)]
    off = (16 - n)//2
    for y in range(n):
        for x in range(n):
            y0 = int(y*16/n); y1 = max(y0+1, int((y+1)*16/n))
            x0 = int(x*16/n); x1 = max(x0+1, int((x+1)*16/n))
            cnt = {}
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    c = img[yy][xx]
                    cnt[c] = cnt.get(c, 0) + 1
            tot = sum(cnt.values())
            nz = {c: v for c, v in cnt.items() if c}
            if nz and sum(nz.values())*2 >= tot:
                c = max(nz.items(), key=lambda kv: kv[1])[0]
            else:
                c = 0
            out[off+y][off+x] = c
    return out

def tile_bytes(px):  # px: 8 rows of 8 colour indices
    b = []
    for row in px:
        for plane in range(4):
            v = 0
            for x in range(8):
                if (row[x] >> plane) & 1:
                    v |= 0x80 >> x
            b.append(v)
    return b

def cut(img, x0, y0):
    return [row[x0:x0+8] for row in img[y0:y0+8]]

def spr16(img):   # 4 tiles: left-top,left-bot,right-top,right-bot
    out = []
    for x0 in (0, 8):
        for y0 in (0, 8):
            out += tile_bytes(cut(img, x0, y0))
    return out

def spr8(img):    # centre 8 columns (x 4..11), 2 tiles
    return tile_bytes(cut(img, 4, 0)) + tile_bytes(cut(img, 4, 8))

def set10(img):   # big, medium, small = 10 tiles
    return spr16(img) + spr16(scale(img, 12)) + spr8(scale(img, 8))

random.seed(1983)
def explosion(f):
    img = [[0]*16 for _ in range(16)]
    r_in = [0, 2.5, 5.0][f]; r_out = [4.0, 6.5, 8.0][f]
    dens = [0.95, 0.75, 0.35][f]
    for y in range(16):
        for x in range(16):
            d = math.hypot(x-7.5, y-7.5)
            if r_in <= d <= r_out and random.random() < dens:
                t = (d - r_in)/(r_out - r_in + .01)
                if f == 0: c = 6 if t < .5 else 1
                elif f == 1: c = 1 if t < .4 else (5 if t < .8 else 4)
                else: c = 5 if t < .5 else 4
                img[y][x] = c
    return img

sprite_tiles = []
defs = []
def add(name, data):
    defs.append((name, len(sprite_tiles)//32))
    sprite_tiles.extend(data)

add('SPR_BLANK', [0]*64)
add('SPR_PLAYER', spr16(PLAYER) + spr16(swap(PLAYER, {5: 1})))
en = []
for img, sw in ((SAUCER, {4: 6, 5: 4}), (DART, {10: 1, 6: 10}),
                (SPINNER, {6: 1, 8: 6}), (BOMBER, {4: 1, 6: 4})):
    en += set10(img) + set10(swap(img, sw))
add('SPR_ENEMY', en)
ex = []
for f in range(3):
    ex += set10(explosion(f))
add('SPR_EXPL', ex)
add('SPR_ASTRO', set10(ASTRO) + set10(ASTRO2))

def bullet(rows):
    img = [[0]*8 for _ in range(16)]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            img[y][x] = PAL[c]
    return tile_bytes(img[:8]) + tile_bytes(img[8:])

add('SPR_PBUL', bullet(["...WW...", "...WW...", "...YY...", "...YY...", "...YY...", "...OO..."]))
add('SPR_PBUL_S', bullet(["........", "...WW...", "...YY...", "...YY..."]))
add('SPR_EBUL', bullet(["........", "........", "..RRRR..", ".RRWWRR.", ".RWWWWR.", ".RWWWWR.", ".RRWWRR.", "..RRRR.."])
    + bullet(["........", "........", "..YYYY..", ".YYWWYY.", ".YWRRWY.", ".YWRRWY.", ".YYWWYY.", "..YYYY.."]))
add('SPR_EBUL_S', bullet(["........", "........", "........", "...RR...", "..RWWR..", "..RWWR..", "...RR..."]))
n_sprite_tiles = len(sprite_tiles)//32
assert n_sprite_tiles <= 192, n_sprite_tiles

# ---------------------------------------------------------------- projection
D = 150.0
Y0, Y1, RELMAX = 200.0, 26.0, 223
B = (Y0 - Y1)/(1/D - 1/(D+RELMAX)); A = Y0 - B/D
def gy(rel): return A + B/(D + rel)
ylut = [min(255, max(0, int(round(gy(r))))) if r <= RELMAX else 200 for r in range(256)]

NP, S = 12, 16
FIELD_TOP = 24
scan = [[0]*8 for _ in range(192)]
used = set(); drops = 0
for k in range(0, 16):
    for p in range(NP):
        rel = S*k - (p + 0.5)*S/NP
        if rel < 0 or rel > RELMAX: continue
        y = int(round(gy(rel)))
        if y < FIELD_TOP or y > 191: continue
        slot = (p + k) % 2
        for xo in (4*slot, 4*slot + 2, (4*slot + 4) % 8, (4*slot + 6) % 8):
            if (y, xo) not in used: break
        else:
            drops += 1; continue
        used.add((y, xo))
        scan[y][xo] = 2 + p
        if rel < 110 and (y, (xo + 1) % 8) not in used:
            used.add((y, (xo + 1) % 8))
            scan[y][(xo + 1) % 8] = 2 + p
field_tiles = []
for r in range(24):
    field_tiles += tile_bytes(scan[8*r:8*r+8])
print("field line drops:", drops, "A=%.2f B=%.1f" % (A, B), "player y", ylut[25], file=sys.stderr)

# ---------------------------------------------------------------- logo
F5 = {
 'J': ["..XXX", "...X.", "...X.", "...X.", "...X.", "X..X.", ".XX.."],
 'U': ["X...X", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
 'N': ["X...X", "XX..X", "X.X.X", "X.X.X", "X..XX", "X...X", "X...X"],
 'O': [".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
 'F': ["XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "X...."],
 'I': ["XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX"],
 'R': ["XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"],
 'S': [".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."],
 'T': ["XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."],
 ' ': ["....."]*7,
}
LW, LH = 30, 4
logo = [[0]*(LW*8) for _ in range(LH*8)]
for i, ch in enumerate("JUNO FIRST"):
    for fy in range(7):
        col = 1 if fy < 2 else (14 if fy < 5 else 15)
        for fx in range(5):
            if F5[ch][fy][fx] == 'X':
                for yy in range(3):
                    for xx in range(3):
                        logo[2 + fy*4 + yy][i*24 + 2 + fx*4 + xx] = col
logo_tiles = []; logo_map = []; seen = {}
for ty in range(LH):
    for tx in range(LW):
        t = tuple(tile_bytes([row[tx*8:tx*8+8] for row in logo[ty*8:ty*8+8]]))
        if t not in seen:
            seen[t] = len(seen); logo_tiles += list(t)
        logo_map.append(seen[t])
FIELD_BASE = 96; LOGO_BASE = 120
assert LOGO_BASE + len(seen) <= 256

sintab = [int(round(127*math.sin(2*math.pi*i/256))) for i in range(256)]

def carr(name, data, typ='unsigned char'):
    s = "const %s %s[%d] = {\n" % (typ, name, len(data))
    for i in range(0, len(data), 16):
        s += "  " + ",".join(str(v) for v in data[i:i+16]) + ",\n"
    return s + "};\n"

with open(sys.argv[1], 'w') as f:
    f.write("/* generated by tools/gen_gfx.py - do not edit */\n")
    for n, v in defs: f.write("#define %s %d\n" % (n, v))
    f.write("#define FIELD_BASE %d\n#define LOGO_BASE %d\n#define LOGO_W %d\n#define LOGO_H %d\n" % (FIELD_BASE, LOGO_BASE, LW, LH))
    f.write("#define NPHASE %d\n" % NP)
    f.write(carr('sprite_tiles', sprite_tiles))
    f.write(carr('field_tiles', field_tiles))
    f.write(carr('logo_tiles', logo_tiles))
    f.write(carr('logo_map', logo_map))
    f.write(carr('ylut', ylut))
    f.write(carr('sintab', sintab, 'signed char'))
    f.write(carr('sin4', [(v >> 2) & 255 for v in sintab]))
    f.write(carr('sin16', [(v >> 4) & 255 for v in sintab]))
