import sys
from PIL import Image, ImageChops
a, b = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
box = ImageChops.difference(a, b).getbbox()
print("difference:", box)
print("RESULT: ALL PASSED" if box is None else "RESULT: FAILED"); sys.exit(box is not None)
