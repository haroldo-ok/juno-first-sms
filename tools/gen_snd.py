#!/usr/bin/env python3
"""Generates src/snd.h: PSGlib-format music and sound effects (original tunes)."""
import sys, math

def tone(ch, f):
    n = max(1, min(1023, int(round(3579545.0/(32*f)))))
    return [0x80 | (ch << 5) | (n & 15), 0x40 | (n >> 4)]
def vol(ch, a):
    return [0x90 | (ch << 5) | max(0, min(15, int(a)))]

def pack(frames, loop=False):
    """frames: list of per-frame byte lists -> PSG stream"""
    out = [0x01] if loop else []
    pend = 0           # completed frames not yet flushed as waits
    def flush():
        nonlocal pend
        while pend > 0:
            n = min(pend, 8)
            out.append(0x38 + n - 1); pend -= n
    for fr in frames:
        if fr:
            flush()
            out.extend(fr)
        pend += 1
    flush()
    out.append(0x00)
    return out

NOTE = {'C':0,'C#':1,'D':2,'D#':3,'E':4,'F':5,'F#':6,'G':7,'G#':8,'A':9,'A#':10,'B':11}
def hz(name):
    if name is None: return None
    n, o = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((NOTE[n] + 12*(o - 4) - 9)/12.0)

def render(tracks, total):
    """tracks: list of (ch, [(start_frame, note, dur, basevol)])"""
    frames = [[] for _ in range(total)]
    for ch, notes in tracks:
        for st, note, dur, bv in notes:
            f = hz(note)
            for i in range(dur):
                if st + i >= total: break
                if i == 0:
                    frames[st] += tone(ch, f) + vol(ch, bv)
                elif i == dur - 1:
                    frames[st+i] += vol(ch, 15)
                elif i in (3, 7, 12, 20):
                    bv += 2
                    frames[st+i] += vol(ch, bv)
    return frames

# ---- in-game song: 32 bars (A B C A'), eighth = 12 frames, ~51 s loop
E8 = 12
A1 = {'A': ['A4','C5','E5','C5','A4','C5','E5','A5'],
      'G': ['G4','B4','D5','B4','G4','B4','D5','G5'],
      'F': ['F4','A4','C5','A4','F4','A4','C5','F5'],
      'E': ['E4','G#4','B4','G#4','E5','D5','B4','G#4']}
A2 = {'A': ['E5',None,'A5',None,'G5','E5','C5','E5'],
      'G': ['D5',None,'G5',None,'E5','D5','B4','D5'],
      'F': ['C5',None,'F5',None,'E5','C5','A4','C5'],
      'E': ['B4',None,'E5',None,'D5','B4','G#4','B4']}
def up8(m): return {k: [n and n[:-1] + str(int(n[-1]) + 1) for n in v] for k, v in m.items()}
A3 = up8(A1)
ARP = {'D': ['D5','F5','A5','F5'], 'A': ['A4','C5','E5','C5'], 'E': ['E5','G#5','B5','G#5']}
song = []   # (root, lead: list of 8 eighths | ('arp', root), drums 0/1/2)
for r in 'AGFE': song.append((r, A1[r], 1))
for r in 'AGFE': song.append((r, A2[r], 1))
B = [('F', ['A5',None,'C6',None,'A5','F5','A5',None]),
     ('G', ['B5',None,'D6',None,'B5','G5','B5',None]),
     ('A', ['C6',None,'B5','A5','E5',None,'A5',None]),
     ('A', ['E5','A5','C6','A5','E5','A5','C6','E6']),
     ('F', ['F5',None,'A5',None,'C6','A5','F5',None]),
     ('G', ['G5',None,'B5',None,'D6','B5','G5',None]),
     ('E', ['G#5',None,'B5',None,'E6',None,'D6','B5']),
     ('E', ['G#5','B5','E6','B5','G#5','E5','G#5','B5'])]
for r, m in B: song.append((r, m, 2))
for r in 'DADEDAEE': song.append((r, ('arp', r), 0))
for r in 'AGFE': song.append((r, A3[r], 2))
for r in 'AGFE': song.append((r, A2[r], 2))
bass = []; lead = []
total = len(song)*8*E8
drum = [[] for _ in range(total)]
for b, (root, mel, dr) in enumerate(song):
    t0 = b*8*E8
    if dr == 0:      # breakdown: bass on quarters only
        for i in range(4):
            bass.append((t0 + i*2*E8, root + ('4' if i & 1 else '3'), 2*E8 - 4, 5))
    else:
        for i, up in enumerate([0, 0, 1, 0, 0, 1, 0, 1]):
            bass.append((t0 + i*E8, "%s%d" % (root, 3 + up), E8 - 2, 5))
    if isinstance(mel, tuple):
        for i in range(16):
            lead.append((t0 + i*6, ARP[root][i % 4], 5, 8))
    else:
        for i in range(8):
            n = mel[i]
            if not n: continue
            dur = 2*E8 if i + 1 < 8 and mel[i+1] is None else E8
            lead.append((t0 + i*E8, n, dur - 1, 7))
    for i in range(8):
        f = t0 + i*E8
        if dr == 2 and i in (2, 6):          # snare
            for k, v in enumerate([5, 7, 9, 11, 13, 15]):
                drum[f + k] += ([0xE5] if k == 0 else []) + vol(3, v)
        else:                                # hi-hat (sparser in the breakdown)
            if dr == 0 and (i & 1): continue
            drum[f] += [0xE4] + vol(3, 10); drum[f + 1] += vol(3, 13); drum[f + 2] += vol(3, 15)
fr = render([(0, bass), (1, lead)], total)
mus_game = pack([x + y for x, y in zip(fr, drum)], loop=True)
print("mus_game: %d bytes, %.1f s" % (len(mus_game), total/60.0), file=sys.stderr)

def line(seq, v):
    t = 0; o = []
    for n, d in seq:
        if n: o.append((t, n, d - 1, v))
        t += d
    return o, t

def period(f): return max(1, min(1023, int(round(3579545.0/(32*f)))))
def tone_n(ch, n): return [0x80 | (ch << 5) | (n & 15), 0x40 | (n >> 4)]

# ---- in-game "growl": two detuned low square waves (beating), the lead voice
# re-struck every 16 frames with a pitch sag, creeping chromatically in D minor.
roots = ['D3', 'D3', 'D#3', 'D3', 'D3', 'F3', 'E3', 'C#3']
PUL = 16
frames = [[] for _ in range(len(roots)*16*PUL)]
for b, root in enumerate(roots):
    n0 = period(hz(root))
    for pi in range(16):
        t0 = (b*16 + pi)*PUL
        accent = pi in (0, 6, 12)
        snarl = pi == 15 and b % 2 == 1
        bends = [1.06, 1.03, 1.0] if not snarl else [0.94, 0.97, 1.0]
        for k, m in enumerate(bends):
            frames[t0 + 2*k] += tone_n(0, min(1023, int(n0*m)))
        for k, (f, v) in enumerate(zip((0, 3, 7, 11), (2, 5, 8, 11) if accent else (5, 7, 9, 12))):
            frames[t0 + f] += vol(0, v)
    # drone: same note, a few periods lower -> slow beating; volume swells per bar
    frames[b*16*PUL] += tone_n(1, min(1023, n0 + 4))
    for k in range(0, 16*PUL, 32):
        frames[b*16*PUL + k] += vol(1, 7 + int(round(2*math.sin(2*math.pi*k/(16*PUL)))))
mus_growl = pack(frames, loop=True)
print("mus_growl: %d bytes, %.1f s" % (len(mus_growl), len(frames)/60.0), file=sys.stderr)

# ---- start jingle: opening of Bach's Toccata in D minor, BWV 565 (public domain)
seq = [('A5', 5), ('G5', 4), ('A5', 30), (None, 10), ('G5', 5), ('F5', 5), ('E5', 5), ('D5', 5),
       ('C#5', 14), ('D5', 46)]
seq2 = [('A4', 5), ('G4', 4), ('A4', 30), (None, 10), ('G4', 5), ('F4', 5), ('E4', 5), ('D4', 5),
        ('C#4', 14), ('D4', 46)]
l1, tot = line(seq, 2); l2, _ = line(seq2, 4)
fr = render([(0, l1), (1, l2)], tot + 2)
fr[-1] += vol(0, 15) + vol(1, 15)
mus_start = pack(fr)

# ---- game over: the growl sinks chromatically and dies away
fr = [[] for _ in range(6*24 + 40)]
for i, n in enumerate(['D3', 'C#3', 'C3', 'B2', 'A#2', 'A2']):
    t0 = i*24
    p = period(hz(n))
    fr[t0] += tone_n(0, p) + tone_n(1, min(1023, p + 5)) + vol(0, 3 + i) + vol(1, 5 + i)
    fr[t0 + 12] += vol(0, 6 + i)
fr[6*24 + 30] += vol(0, 15) + vol(1, 15)
mus_over = pack(fr)

# ---- sound effects (channel 2 = tone, channel 3 = noise)
def sweep(f0, f1, n, v0, v1, ch=2):
    fr = []
    for i in range(n):
        t = i/max(1, n - 1)
        fr.append(tone(ch, f0 * (f1/f0) ** t) + vol(ch, v0 + (v1 - v0)*t))
    return fr
def noise(mode, n, v0, v1):
    fr = []
    for i in range(n):
        t = i/max(1, n - 1)
        fr.append(([0xE0 | mode] if i == 0 else []) + vol(3, v0 + (v1 - v0)*t))
    return fr

sfx_shot = pack(sweep(1900, 500, 7, 2, 9) + [vol(2, 15)])
sfx_eshot = pack(sweep(350, 180, 5, 5, 10) + [vol(2, 15)])
sfx_boom = pack(noise(5, 16, 0, 13) + [vol(3, 15)])
a = sweep(600, 110, 80, 0, 11); b = noise(6, 80, 0, 13)
sfx_die = pack([x + y for x, y in zip(a, b)] + [vol(2, 15) + vol(3, 15)])
fr = []
for n in ['C6', 'E6', 'G6', 'C7', 'G6', 'C7']:
    fr += [tone(2, hz(n)) + vol(2, 1), [], vol(2, 4), []]
sfx_pickup = pack(fr + [vol(2, 15)])
sfx_warp = pack(sweep(200, 2400, 14, 3, 3) + sweep(2400, 300, 14, 3, 10) + [vol(2, 15)])
fr = []
for n in ['G5', 'C6', 'E6', 'G6', 'E6', 'G6', 'C7', 'C7']:
    fr += [tone(2, hz(n)) + vol(2, 0), [], [], vol(2, 3), []]
sfx_extra = pack(fr + [vol(2, 15)])

def carr(name, data):
    s = "const unsigned char %s[%d] = {\n" % (name, len(data))
    for i in range(0, len(data), 16):
        s += "  " + ",".join("0x%02X" % v for v in data[i:i+16]) + ",\n"
    return s + "};\n"
NAMES = ['mus_game', 'mus_growl', 'mus_start', 'mus_over', 'sfx_shot', 'sfx_eshot', 'sfx_boom',
         'sfx_die', 'sfx_pickup', 'sfx_warp', 'sfx_extra']
# argv[1] = header (externs), argv[2] = C file with the data (compiled into ROM bank 2)
with open(sys.argv[1], 'w') as f:
    f.write("/* generated by tools/gen_snd.py - do not edit */\n")
    for n in NAMES:
        f.write("extern const unsigned char %s[%d];\n" % (n, len(globals()[n])))
with open(sys.argv[2], 'w') as f:
    f.write("/* generated by tools/gen_snd.py - do not edit; lives in ROM bank 2 */\n")
    for n in NAMES:
        f.write(carr(n, globals()[n]))
