#!/usr/bin/env python3
from PIL import Image
import sys, math

if len(sys.argv) < 3:
    print("Usage: bdf2tilemap.py input.bdf output.png [cols]")
    sys.exit(1)

input_path = sys.argv[1]
output_path = sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 16

cell_w = cell_h = None
codes = []
bitmap = []
cur_code = None
drawing = False
cur_bbx = None
cur_yoff = 0

with open(input_path, "r", encoding="latin1", errors="ignore") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue

        if line.startswith("FONTBOUNDINGBOX"):
            _, w, h, _, _ = line.split()[:5]
            cell_w, cell_h = int(w), int(h)

        elif line.startswith("STARTCHAR"):
            bitmap = []
            cur_code = None
            cur_bbx = None
            cur_yoff = 0
            drawing = False

        elif line.startswith("ENCODING"):
            cur_code = int(line.split()[1])

        elif line.startswith("BBX"):
            _, w, h, xoff, yoff = line.split()
            cur_bbx = (int(w), int(h), int(xoff), int(yoff))
            cur_yoff = int(yoff)

        elif line.startswith("BITMAP"):
            drawing = True
            bitmap = []

        elif line.startswith("ENDCHAR"):
            if bitmap and cur_code is not None and cur_bbx is not None:
                codes.append((cur_code, bitmap, cur_bbx))
            drawing = False

        elif drawing:
            row_bits = bin(int(line, 16))[2:].rjust(cell_w, "0")
            bitmap.append([1 if c == "1" else 0 for c in row_bits[-cell_w:]])

if cell_w is None or cell_h is None or not codes:
    print("Error: couldn't parse BDF (no glyphs found).")
    sys.exit(1)

first = 0
last = 255
rows = math.ceil((last - first + 1) / cols)
img = Image.new("RGBA", (cols * cell_w, rows * cell_h), (0, 0, 0, 0))
on = (255, 255, 255, 255)

table = {code: (bm, bbx) for code, bm, bbx in codes}

# Compute baseline: top of tallest ascender
baseline = max(yoff + h for (_, _, (_, h, _, yoff)) in codes)

for code in range(first, last + 1):
    idx = code - first
    gx = idx % cols
    gy = idx // cols
    x = gx * cell_w
    y = gy * cell_h

    entry = table.get(code)
    if not entry:
        continue

    bm, (gw, gh, xoff, yoff) = entry

    # Vertical alignment (baseline)
    y_top = baseline - (yoff + gh)
    if y_top < 0:
        y_top = 0
    if y_top + gh > cell_h:
        y_top = cell_h - gh

    # Horizontal alignment (center)
    x_left = (cell_w - gw) // 2
    if x_left < 0:
        x_left = 0

    for j, row in enumerate(bm):
        if y_top + j >= cell_h:
            break
        for i, bit in enumerate(row[:cell_w]):
            if bit and (x + i + x_left < img.width and y + j + y_top < img.height):
                img.putpixel((x + i + x_left, y + j + y_top), on)

img.save(output_path)
print(f"Saved {output_path} ({cols}x{rows} tiles of {cell_w}x{cell_h}) baseline + horizontally centered")
