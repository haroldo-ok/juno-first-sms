"""Check the radar window (x 112-159, y 0-23) holds coloured dots: the yellow
player marker and orange enemy dots (BG colours 14/15)."""
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
ylw = org = 0
for y in range(1, 23):
    for x in range(113, 159):
        r, g, b = im.getpixel((x, y))
        if r > 200 and g > 200 and b < 100: ylw += 1
        elif r > 200 and 40 < g < 140 and b < 100: org += 1
print("radar: %d player pixels, %d enemy pixels" % (ylw, org))
if ylw < 4 or org < 4:
    print("RESULT: FAILED (radar empty)"); sys.exit(1)
print("RESULT: ALL PASSED")
