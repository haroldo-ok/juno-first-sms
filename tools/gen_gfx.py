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
................
................
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

def scale_any(img, n, size):
    """downscale a size x size image to n x n (majority colour per cell)"""
    out = [[0]*n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            y0 = int(y*size/n); y1 = max(y0+1, int((y+1)*size/n))
            x0 = int(x*size/n); x1 = max(x0+1, int((x+1)*size/n))
            cnt = {}
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    c = img[yy][xx]; cnt[c] = cnt.get(c, 0) + 1
            tot = sum(cnt.values())
            nz = {c: v for c, v in cnt.items() if c}
            if nz and sum(nz.values())*2 >= tot:
                out[y][x] = max(nz.items(), key=lambda kv: kv[1])[0]
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

sprite_tiles = []
defs = []
def add(name, data):
    defs.append((name, len(sprite_tiles)//32))
    sprite_tiles.extend(data)

def bullet(rows):
    img = [[0]*8 for _ in range(16)]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            img[y][x] = PAL[c]
    return tile_bytes(img[:8]) + tile_bytes(img[8:])

add('SPR_BLANK', [0]*64)
add('SPR_PLAYER', spr16(PLAYER))

def flame(side, mid, phase):
    """three engine jets (x 1-2, 7-8, 13-14), lengths in px; phase flickers the tips"""
    img = [[0]*16 for _ in range(16)]
    for (x0, L) in ((1, side), (7, mid), (13, side)):
        L = max(0, L + (1 if (phase and x0 == 7) else 0) - (1 if (phase and x0 != 7) else 0))
        for y in range(L):
            f = y / max(1, L)
            c = 'W' if f < 0.2 else 'Y' if f < 0.45 else 'O' if f < 0.75 else 'R'
            w = 2 if f < 0.6 else 1
            for dx in range(w):
                xx = x0 + dx + (1 if (w == 1 and (phase ^ (y & 1)) and x0 != 7) else 0)
                if xx < 16: img[y][xx] = PAL[c]
    return img
# small flame (slow forward) and big flame (fast), two flicker frames each
add('SPR_FLAME', spr16(flame(3, 4, 0)) + spr16(flame(3, 4, 1)) + spr16(flame(7, 11, 0)) + spr16(flame(7, 11, 1)))
en = []
for img, sw in ((SAUCER, {4: 6, 5: 4}), (DART, {10: 1, 6: 10}),
                (SPINNER, {6: 1, 8: 6}), (BOMBER, {4: 1, 6: 4})):
    en += set10(img) + set10(swap(img, sw))
add('SPR_ENEMY', en)
add('SPR_ASTRO', set10(ASTRO) + set10(ASTRO2))
# dashed laser shots, Williams style
dash = ["...WW...", "...WW...", "...WW...", "........", "........"]
add('SPR_PBUL', bullet([dash[i % 5] for i in range(16)]))
add('SPR_PBUL_S', bullet(["...WW...", "...WW...", "........", "...YY...", "...YY...", "........", "...YY..."]))
add('SPR_EBUL', bullet(["........", "........", "..RRRR..", ".RRWWRR.", ".RWWWWR.", ".RWWWWR.", ".RRWWRR.", "..RRRR.."])
    + bullet(["........", "........", "..YYYY..", ".YYWWYY.", ".YWRRWY.", ".YWRRWY.", ".YYWWYY.", "..YYYY.."]))
add('SPR_EBUL_S', bullet(["........", "........", "........", "...RR...", "..RWWR..", "..RWWR..", "...RR..."]))
# explosion particles: a 2x2 dot at (3,3), four heat colours
part = []
for c in "WYOR":
    part += bullet(["........", "........", "........", "...%s%s..." % (c, c), "...%s%s..." % (c, c)])
add('SPR_PART', part)
# ---- bosses: five ships, each bigger and with more parts than the last.
# Hull colour is sprite colour 12, recoloured per boss.  Only the current
# boss's tiles are in VRAM (uploaded at its wave); all five live in bank 2.
BOSS_DIM = {1: (24, 16), 2: (32, 24), 3: (40, 32), 4: (48, 40), 5: (48, 48)}

def scale_rect(img, w, h, nw, nh):
    out = [[0]*nw for _ in range(nh)]
    for y in range(nh):
        for x in range(nw):
            y0 = int(y*h/nh); y1 = max(y0+1, int((y+1)*h/nh))
            x0 = int(x*w/nw); x1 = max(x0+1, int((x+1)*w/nw))
            cnt = {}
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    c = img[yy][xx]; cnt[c] = cnt.get(c, 0) + 1
            nz = {c: v for c, v in cnt.items() if c}
            if nz and sum(nz.values())*2 >= sum(cnt.values()):
                out[y][x] = max(nz.items(), key=lambda kv: kv[1])[0]
    return out

def boss_ship(lv):
    W, H = BOSS_DIM[lv]
    CH = ((H + 15)//16)*16
    oy = CH - H                                   # art sits on the bottom edge
    g = [[0]*W for _ in range(CH)]
    def put(x, y, c):
        x = int(x); y = int(y)
        if 0 <= x < W and 0 <= y < CH: g[y][x] = PAL[c]
    def ellipse(cx, cy, rx, ry, fill, edge=None):
        for y in range(CH):
            for x in range(W):
                d = ((x + .5 - cx)/rx)**2 + ((y + .5 - cy)/ry)**2
                if d <= 1.0: put(x, y, edge if (edge and d > 0.7) else fill)
    cx = W/2
    hcy = oy + H*0.60; hry = max(3.5, H*0.21)
    if lv >= 5:                                   # outer wing arrays
        for x in range(W):
            for dy in (-1, 0, 1):
                put(x, hcy + dy, 'D' if dy else 'L')
        for dy in (-2, -1, 0, 1, 2):
            put(0, hcy + dy, 'O'); put(W - 1, hcy + dy, 'O')
    if lv >= 2:                                   # engine pods
        pw = max(3, int(W*0.1)); ph = max(3, int(H*0.16))
        for y in range(int(hcy - ph), int(hcy + ph) + 1):
            for x in list(range(1, 1 + pw)) + list(range(W - 1 - pw, W - 1)):
                put(x, y, 'L' if y in (int(hcy - ph), int(hcy + ph)) else 'D')
        put(2, hcy, 'R'); put(W - 3, hcy, 'R')
    if lv >= 3:                                   # dorsal fins
        for k in range(int(H*0.2)):
            for fx in (W*0.22, W*0.78):
                for dx in range(-(int(H*0.2) - k)//2, (int(H*0.2) - k)//2 + 1):
                    put(fx + dx, hcy - hry - k + 1, 'L')
    ellipse(cx, hcy, W/2 - 1.5 if lv >= 2 else W/2 - 0.5, hry, 'b', 'D')       # hull
    if lv >= 4:                                   # armour plating
        for x in range(4, W - 4, 6):
            for y in range(int(hcy - hry + 2), int(hcy + hry - 1)):
                if g[y][x] == PAL['b']: put(x, y, 'D')
    ellipse(cx, oy + H*0.36, max(4, W*0.19), max(3, H*0.23), 'C', 'L')       # dome
    put(cx - 3, oy + H*0.30, 'W'); put(cx - 2, oy + H*0.26, 'W')
    if lv >= 4:                                   # antenna mast
        top = int(oy + H*0.36 - max(3, H*0.23))
        for y in range(max(0, top - 6), top + 1): put(cx - 1, y, 'L')
        put(cx - 1, max(0, top - 7), 'R'); put(cx - 2, max(0, top - 7), 'R')
    if lv >= 2:                                   # running lights
        for x in range(4, W - 4, 4):
            if abs(x - cx) > W*0.12: put(x, hcy, 'Y' if lv < 5 else 'W')
    cr = max(1.8, W*(0.07 if lv < 5 else 0.1))
    ellipse(cx, hcy + 0.5, cr + 0.6, cr, 'R', 'O')                          # core
    put(cx - 1, hcy, 'Y'); put(cx, hcy, 'Y')
    if lv >= 5: put(cx - 1, hcy - 1, 'W'); put(cx, hcy - 1, 'W')
    if lv >= 3:                                   # underside cannons
        n = 3 if lv == 3 else 5
        for k in range(n):
            gx = cx + (k - (n - 1)/2)*W*(0.28 if n == 3 else 0.17)
            for y in range(int(hcy + hry - 1), int(hcy + hry + H*0.14)):
                put(gx - 1, y, 'L'); put(gx, y, 'D')
            put(gx - 1, hcy + hry + H*0.14, 'O'); put(gx, hcy + hry + H*0.14, 'O')
    if lv >= 5:                                   # ventral spikes
        for k in range(4):
            sx = cx + (k - 1.5)*W*0.17
            for d in range(3): put(sx, CH - 1 - d, 'R' if d == 0 else 'O')
    return g, W, CH

def blocks(img, cols, rows):
    """tall-sprite blocks: for each row of 16 px, each column of 8 px -> top, bottom tile"""
    out = []
    for r in range(rows):
        for c in range(cols):
            out += tile_bytes(cut(img, 8*c, 16*r)) + tile_bytes(cut(img, 8*c, 16*r + 8))
    return out

boss_data = {}; boss_desc = []
BOSS_SLOT = 40                                    # VRAM tiles reserved for the current boss
for lv in range(1, 6):
    img, W, CH = boss_ship(lv)
    cols, rows = W//8, CH//16
    near = blocks(img, cols, rows)
    fh = max(4, min(16, round(CH*16/W)))
    far = [[0]*16 for _ in range(16)]
    for y, row in enumerate(scale_rect(img, W, CH, 16, fh)):
        far[16 - fh + y] = row
    data = near + spr16(far)
    assert len(data)//32 <= BOSS_SLOT, (lv, len(data)//32)
    boss_data[lv] = data
    boss_desc.append((cols, rows, len(near)//32, len(data)//32, W//2, CH))
    globals()['BOSS_IMG_%d' % lv] = img
add('SPR_BOSS', [0]*(32*BOSS_SLOT))               # uploaded per boss
add('SPR_PLAYER_SMALL', spr8(scale(PLAYER, 8)))      # for the ending fly-out
n_sprite_tiles = len(sprite_tiles)//32
assert n_sprite_tiles <= 192, n_sprite_tiles

FIELD_BASE_ = 96
# ---------------------------------------------------------------- projection
D = 150.0
Y0, Y1, RELMAX = 200.0, 26.0, 223
B = (Y0 - Y1)/(1/D - 1/(D+RELMAX)); A = Y0 - B/D
def gy(rel): return A + B/(D + rel)
ylut = [min(255, max(0, int(round(gy(r))))) if r <= RELMAX else 200 for r in range(256)]

# Grid: a dot lattice in true perspective.  Rows of dots every 32 depth units
# move towards the player by palette cycling (12 phases, one lit at a time);
# columns are world lines every 24 units that converge on the horizon, so the
# field reads as lines receding into the distance.  Each tile row needs at most
# 4 tile variants (dot column at x offset 0/2/4/6 inside the tile).
NP, S, COLW = 11, 32, 24          # palette entry 13 = static column lines
FIELD_TOP = 26
def kp(rel): return (D + 25.0)/(D + rel)
def rel_of_y(y): return B/(y - A) - D
lines = {}
for k in range(0, 9):
    for p in range(NP):
        rel = S*k - (p + 0.5)*S/NP
        if rel < 0 or rel > RELMAX: continue
        y = int(round(gy(rel)))
        if y < FIELD_TOP or y > 191: continue
        lines.setdefault(y, []).append((p, rel))
drops = sum(max(0, len(v) - 2) for v in lines.values())
field_tiles = []; field_map = []
for r in range(3, 24):
    rel = rel_of_y(8*r + 4)
    variants = {}
    row = [0]*32
    for j in range(-13, 14):
        sx = int(round(128 + COLW*j*kp(rel)))
        if sx < 0 or sx > 255: continue
        o = sx & 6
        if o not in variants:
            px = [[0]*8 for _ in range(8)]
            for yy in range(8):
                if (8*r + yy) % 4 == 0:
                    px[yy][o] = 13           # faint receding column line
                for n, (ph, prel) in enumerate(lines.get(8*r + yy, [])[:2]):
                    if n == 0:
                        px[yy][o] = 2 + ph
                        if prel < 110: px[yy][o + 1] = 2 + ph
                    else:
                        px[yy][o + 1] = 2 + ph
            variants[o] = px
        row[sx >> 3] = FIELD_BASE_ + (r - 3)*4 + (o >> 1)
    for o in (0, 2, 4, 6):
        field_tiles += tile_bytes(variants.get(o, [[0]*8 for _ in range(8)]))
    field_map += row
print("field: scanline conflicts dropped:", drops, file=sys.stderr)
kptab = [min(255, int(round(128*kp(r)))) if r < 224 else 0 for r in range(256)]
sq4 = [n*n//4 for n in range(300)]
# explosion particles: 8 directions, radius grows with age (screen px at scale 1)
# pre-scaled for the 4 size classes: ship (x2), near (x1), mid (x3/4), far (x1/2)
ptab = []
for scl in (2.0, 1.0, 0.75, 0.5):
    for t in range(24):
        rr = (3 + 2.2*t)*scl
        for i in range(8):
            a = 2*math.pi*i/8
            ptab += [int(round(rr*math.cos(a))) & 255, int(round(rr*math.sin(a)*0.8)) & 255]

# ---------------------------------------------------------------- ship starburst
# 12 rings of particles, ring k drawn in palette phase colour 2+k; lighting the
# rings in turn makes a screen-filling expanding explosion out of tiles.
CX, CY = 128, 158
burst = {}
for k in range(11):
    r = 12 + 17*k
    n = max(8, int(2*math.pi*r/6))          # evenly spaced all the way round
    for i in range(n):
        a = 2*math.pi*(i + (0.5 if k & 1 else 0))/n
        # 64-column map centred on column 32, so the rings never wrap round
        # onto the far side of the screen whatever the ship's column
        x = int(round(256 + r*math.cos(a))); y = int(round(CY + r*math.sin(a)))
        if y < 24 or y > 191 or x < 0 or x > 511: continue
        cell = (x >> 3, y >> 3)
        if cell in burst: continue
        burst[cell] = 1 + k*4 + ((x >> 2) & 1) + 2*((y >> 2) & 1)
burst_map = []
for ty in range(3, 24):
    for tx in range(64):
        burst_map.append(burst.get((tx, ty), 0))
burst_tiles = []
for k in range(11):
    for q in range(4):
        px = [[0]*8 for _ in range(8)]
        ox = 1 + 4*(q & 1); oy = 1 + 4*(q >> 1)
        for yy in range(2):
            for xx in range(2):
                px[oy+yy][ox+xx] = 2 + k
        burst_tiles += tile_bytes(px)

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
# Big title logo: "JUNO" over "FIRST" in thick block letters lying on a plane
# that tilts back into the distance, so the letters narrow and flatten
# towards the top (drawn in true perspective, then cut into tiles).
# Front faces shade from pale yellow to orange (BG colours 2-5), the letters'
# thickness shows below them in dark reds (6-7), lit edges in 8.  It borrows
# the BG tiles of the field, starburst and radar (96-255), which the game
# reloads on start.
BLK = 8                      # px per font pixel on the flat plane
EXT = 6                      # letter thickness in px (seen below each letter)
GAP = 10                     # between the two lines
LW = 32
def flat_line(text):
    w = len(text)*6*BLK - BLK
    front = [[False]*w for _ in range(7*BLK)]
    for i, ch in enumerate(text):
        for fy in range(7):
            for fx in range(5):
                if F5[ch][fy][fx] != 'X': continue
                for yy in range(BLK):
                    for xx in range(BLK):
                        front[fy*BLK + yy][i*6*BLK + fx*BLK + xx] = True
    return front
F1, F2 = flat_line("JUNO"), flat_line("FIRST")
FW = max(len(F1[0]), len(F2[0])) + 4
FH = 7*BLK*2 + GAP + EXT + 2
front = [[False]*FW for _ in range(FH)]
for (img, oy) in ((F1, 0), (F2, 7*BLK + GAP)):
    ox = (FW - len(img[0]))//2
    for y, row in enumerate(img):
        for x, v in enumerate(row):
            if v: front[oy + y][ox + x] = True
flat = [[0]*FW for _ in range(FH)]
for y in range(FH):
    for x in range(FW):
        if front[y][x]:
            line_y = y if y < 7*BLK else y - (7*BLK + GAP)
            c = 2 + min(3, (line_y*4)//(7*BLK))
            if y == 0 or not front[y-1][x] or x == 0 or not front[y][x-1] or not front[y][x+1]: c = 8
            flat[y][x] = c
        else:
            for d in range(1, EXT + 1):          # thickness below the letter
                if y - d >= 0 and front[y - d][x]:
                    flat[y][x] = 6 if d <= EXT//2 else 7
                    break
# perspective: flat row v (0 = top = far) sits at depth z = 1 + A*(1 - v)
A = 0.6
LH = 12                                        # tile rows
OH = LH*8 - 2                                  # output height in px
BOTW = 252.0                                   # width of the near edge
def zof(v): return 1 + A*(1 - v)
def sy(v):                                     # screen y of flat row v (0..1)
    t0 = 1/zof(0)
    return (1/zof(v) - t0)/(1 - t0)*OH
logo = [[0]*(LW*8) for _ in range(LH*8)]
SS = 3                                          # supersampling per axis
for oy in range(LH*8):
    for ox in range(LW*8):
        votes = {}
        for sj in range(SS):
            yy = oy + (sj + 0.5)/SS
            if yy >= OH: continue
            lo, hi = 0.0, 1.0                    # invert sy() by bisection
            for _ in range(30):
                mid = (lo + hi)/2
                if sy(mid) < yy: lo = mid
                else: hi = mid
            v = lo; z = zof(v)
            fy = int(v*FH)
            if fy >= FH: continue
            for si in range(SS):
                xx = ox + (si + 0.5)/SS - 128
                fx = int(FW/2 + xx*z*FW/BOTW)
                if 0 <= fx < FW:
                    c = flat[fy][fx]
                    votes[c] = votes.get(c, 0) + 1
        if votes:
            c, n = max(votes.items(), key=lambda kv: (kv[1], kv[0] != 0))
            nz = sum(n2 for c2, n2 in votes.items() if c2)
            if c == 0 and nz*2 >= SS*SS - 1:     # keep thin far strokes
                c = max(((n2, c2) for c2, n2 in votes.items() if c2))[1]
            logo[oy][ox] = c
logo_tiles = []; logo_map = []; seen = {}
blank = tuple([0]*32)
seen[blank] = 0; logo_tiles += list(blank)
for ty in range(LH):
    for tx in range(LW):
        t = tuple(tile_bytes([row[tx*8:tx*8+8] for row in logo[ty*8:ty*8+8]]))
        if t not in seen:
            seen[t] = len(seen); logo_tiles += list(t)
        logo_map.append(seen[t])
TITLE_BASE = 64            # also reuses the font's unused lower-case slots (64-95)
print("logo: %d x %d tiles, %d unique" % (LW, LH, len(seen)), file=sys.stderr)
assert TITLE_BASE + len(seen) <= 256, len(seen)
FIELD_BASE = FIELD_BASE_; LOGO_BASE = TITLE_BASE
BURST_BASE = FIELD_BASE + 84; RADAR_BASE = BURST_BASE + 48
assert RADAR_BASE + 18 <= 256, RADAR_BASE

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
    f.write("#define BURST_BASE %d\n#define RADAR_BASE %d\n#define LOGO_TILES %d\n" % (BURST_BASE, RADAR_BASE, len(seen)))
    f.write("#define NPHASE %d\n" % NP)
    BANKED = [('sprite_tiles', sprite_tiles), ('field_tiles', field_tiles),
              ('logo_tiles', logo_tiles), ('burst_tiles', burst_tiles)]
    f.write("/* in ROM bank 3: only reachable while it is mapped (see load_tiles_b3) */\n")
    for n, d in BANKED:
        f.write("extern const unsigned char %s[%d];\n" % (n, len(d)))
    with open(sys.argv[2], 'w') as fb:
        fb.write("/* generated by tools/gen_gfx.py - do not edit; lives in ROM bank 3 */\n")
        for n, d in BANKED:
            fb.write(carr(n, d))
    f.write(carr('logo_map', logo_map))
    # bosses: descriptors here, tile data in bank 2 (always mapped)
    f.write("/* boss L: cols, rows (8x16 sprites), far-image tile offset, tiles, half width px, height px */\n")
    for i, nm in enumerate(('boss_cols', 'boss_rows', 'boss_faroff', 'boss_ntiles', 'boss_half', 'boss_height')):
        f.write(carr(nm, [0] + [d[i] for d in boss_desc]))
    for lv in range(1, 6):
        f.write("extern const unsigned char boss_tiles_%d[%d];\n" % (lv, len(boss_data[lv])))
    with open(sys.argv[3], 'w') as fb:
        fb.write("/* generated by tools/gen_gfx.py - do not edit; lives in ROM bank 2 */\n")
        for lv in range(1, 6):
            fb.write(carr('boss_tiles_%d' % lv, boss_data[lv]))
    f.write(carr('burst_map', burst_map))
    f.write(carr('field_map', field_map))
    f.write(carr('kptab', kptab))
    f.write(carr('sq4', sq4, 'unsigned int'))
    f.write(carr('ptab', ptab))
    f.write(carr('rrow', [(y >> 3)*192 + (y & 7)*4 for y in range(24)], 'unsigned int'))
    f.write(carr('rcol', [(x >> 3)*32 for x in range(48)]))
    f.write(carr('rmask', [0x80 >> (x & 7) for x in range(48)]))
    # stereo 3D: left-eye shift in px by depth (0 at the player's row, + behind, - in front)
    PSCALE = 10.0
    par = [int(round(PSCALE*(1 - kp(r)))) if r < 224 else 0 for r in range(256)]
    f.write(carr('par_l', [v & 255 for v in par]))
    # left-eye background scroll per band of 3 tile rows (band 0 = HUD rows 0-2, fixed);
    # the line interrupt fires every 24 lines and loads the next band's value
    bands = [0]*9
    for g in range(1, 8):
        rr = max(0.0, min(223.0, rel_of_y(8*(3*g + 1) + 4)))
        bands[g] = (-int(round(PSCALE*(1 - kp(rr))))) & 255
    f.write(carr('band_left', bands))
    # every-row version (used while a message is on row 11): the message row
    # floats 3 px in front of the screen instead of sitting in the field's band
    rows = [0]*25
    for r in range(3, 24): rows[r] = bands[r // 3]
    f.write(carr('rows_left', rows))
    rows[11] = 3
    f.write(carr('rows_left_msg', rows))
    # 9-band version for messages: the whole band holding row 11 (rows 9-11)
    # moves 3 px in front, so the scroll change lands above the text
    mb = list(bands); mb[3] = 3
    f.write(carr('band_left_msg', mb))
    # title in 3-D (left eye, 9 bands of 24 lines): the logo (rows 3-14) tilts
    # from 4 px behind the screen at the top to 2 px in front at the bottom
    # two depths, split in the blank gap between the words: "JUNO" (and the
    # score line above it) 2 px behind the screen, "FIRST" 2 px in front,
    # the text below at the screen plane
    used = [y for y in range(len(logo)) if any(logo[y])]
    gap = [y for y in range(used[0], used[-1]) if not any(logo[y])]   # between the words
    mid = 24 + gap[len(gap)//2]                  # the logo starts at row 3 = line 24
    f.write('#define TITLE_SPLIT %d   /* line counter: first IRQ ends line %d */\n' % (mid - 1, mid - 1))
    f.write(carr('title_band', [(-2) & 255, 2, 0, 0]))
    print("3D parallax: horizon %d px, bottom %d px" % (par[223], par[0]), file=sys.stderr)
    # formation manoeuvres: each shape morphs (m = 0..16) into a second shape and back
    FDX = [[0]*8, [-28, -20, -12, -4, 4, 12, 20, 28], [28, 20, 12, 4, -4, -12, -20, -28], [0]*8,
           [-14, 14, -14, 14, -14, 14, -14, 14]]
    FDZ = [[0, 12, 24, 36, 48, 60, 72, 84]]*4 + [[0, 0, 16, 16, 32, 32, 48, 48]]
    def target(fm):
        dx = []; dz = []
        for k in range(8):
            if fm == 0:                          # column -> zigzag
                dx.append(-18 if k & 1 else 18); dz.append(8*k)
            elif fm in (1, 2):                   # diagonal -> its mirror image (swing)
                dx.append(-FDX[fm][k]); dz.append(FDZ[fm][k])
            elif fm == 3:                        # snake -> ring
                a = 2*math.pi*k/8
                dx.append(int(round(30*math.cos(a)))); dz.append(int(round(30 + 24*math.sin(a))))
            else:                                # twin columns -> semicircle fanned out to both sides
                j = k >> 1
                a = math.pi/2 + (math.pi/2)*(j + 1)/4 if not (k & 1) else math.pi/2 - (math.pi/2)*(j + 1)/4
                dx.append(int(round(44*math.cos(a)))); dz.append(int(round(30 - 26*math.sin(a) + 26)))
        return dx, dz
    mdx = []; mdz = []
    for fm in range(5):
        tdx, tdz = target(fm)
        for m in range(17):
            for k in range(8):
                mdx.append(int(round(FDX[fm][k] + (tdx[k] - FDX[fm][k])*m/16)) & 255)
                mdz.append(max(0, int(round(FDZ[fm][k] + (tdz[k] - FDZ[fm][k])*m/16))))
    f.write(carr('morph_dx', mdx))
    f.write(carr('morph_dz', mdz))
    f.write(carr('rxw', [2 + (x*43)//256 for x in range(256)]))
    f.write(carr('ylut', ylut))
    f.write(carr('sin4', [(v >> 2) & 255 for v in sintab]))
