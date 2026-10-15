"""Optional helper for scripts/smoke.ps1: convert the BMP screenshots to PNG.

Only needed if you want to look at the screenshots with tools that do not read
BMP (requires python + opencv-python).  Usage:

    python scripts/bmp2png.py build/smoke.bmp build/smoke.png
"""

import sys

import cv2

src, dst = sys.argv[1], sys.argv[2]
img = cv2.imread(src)
if img is None:
    raise SystemExit("cannot read " + src)
cv2.imwrite(dst, img)
print("wrote", dst, img.shape)
