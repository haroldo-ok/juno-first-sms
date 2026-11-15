import sys
# naive test pilot: weaves left/right holding fire, nudging forward now and then
start_wave = int(sys.argv[1]); frames = int(sys.argv[2])
o = ["symbols junofirst.noi", "run 60", "tap 1", "waituntil phase == 1 300 w1"]
if start_wave > 1:
    o += ["poke wave %d w1" % (start_wave - 1), "poke wave_left 0 w1", "waituntil phase == 0 30 w1", "waituntil phase == 1 300 w1"]
o += ["poke loops 0"]
t = 0; k = 0
pat = [("left+1", 30), ("right+up+1", 30), ("right+1", 30), ("left+up+1", 30)]
while t < frames:
    h, n = pat[k % 4]; k += 1
    o += ["hold " + h, "run %d" % n]; t += n
    if t % 1200 == 0:
        o += ["echo t=%d" % t, "print wave w1", "print lives w1", "print game_state w1", "print loops"]
o += ["screenshot shots/bot%d.ppm" % start_wave]
print("\n".join(o))
