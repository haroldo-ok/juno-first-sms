import sys
w = int(sys.argv[1]); n = int(sys.argv[2])
o = ["symbols junofirst.noi", "run 60", "tap 1", "waituntil phase == 1 300 w1"]
if w > 1: o += ["poke wave %d w1" % (w - 1), "poke wave_left 0 w1", "waituntil phase == 0 30 w1", "waituntil phase == 1 300 w1"]
o += ["poke demo 2 w1", "poke loops 0", "poke lives 9 w1"]
for t in range(1200, n + 1, 1200):
    o += ["run 1200", "echo t=%d" % t, "print wave w1", "print lives w1", "print stat_rammed w1", "print stat_shot w1"]
print("\n".join(o))
