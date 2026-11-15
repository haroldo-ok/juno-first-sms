import re, sys
syms = {}
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 3 and p[0] == 'DEF': syms[p[1].lstrip('_')] = int(p[2], 16)
def sub(m):
    return "0x%04X" % (syms[m.group(1)] + int(m.group(2)))
sys.stdout.write(re.sub(r'\{(\w+)\+(\d+)\}', sub, open(sys.argv[2]).read()))
