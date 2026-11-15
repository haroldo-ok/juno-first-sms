import sys
seed = int(sys.argv[1])
o = ["symbols junofirst.noi", "run %d" % (40 + seed*7), "tap 1", "waituntil phase == 1 300 w1", "poke demo 2 w1",
     "waituntil game_state == 2 30000 w1", "print wave w1", "print loops", "print stat_rammed w1", "print stat_shot w1", "print score w1", "print {score+1} w1","print {score+2} w1"]
print("\n".join(o))
