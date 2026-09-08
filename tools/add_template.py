#!/usr/bin/env python3
"""Capture a real VALORANT invite-code template from a livestream screenshot.

Development helper only - it writes per-character VIT1 templates into the
templates/ tree that the C++ Recognizer loads. It is not part of the runtime.

Invite codes are strict: ^[A-Z]{3}[0-9]{3}$  e.g. MAM639.

The six-char ROI is split into six equal slots exactly like Recognizer::
normalizeSlot() (remainder columns go to the left slots), each slot is
nearest-neighbour resized to 32x32 grayscale, an ink bounding box is computed
with the same foreground rule the recognizer uses at runtime, and one VIT1
binary template is written as templates/<char>/NN.bin.

Usage:
    python tools/add_template.py screenshot.png MAM639 x y w h [-o ROOT]
    python tools/add_template.py screenshot.png MAM639 x y w h --index 03

Arguments:
    screenshot.png   real Douyin/VALORANT livestream screenshot
    MAM639           the six characters shown inside the ROI (ground truth)
    x y w h          ROI in image pixels that tightly wraps the six characters
    -o ROOT          template root (default: ./templates next to this script)
    --index NN       two-digit file number, default = next free slot (01..08)

Output format (binary VIT1, exactly what src/recognizer.cpp reads):
    "VIT1" (4) + u16 width + u16 height + u16 bboxX/Y/W/H + char (1)
    + background gray (1) + 32*32 raw grayscale bytes.
Requires Pillow (only to decode the PNG; the app itself stays pure C++).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image

CODE_RE = re.compile(r"^[A-Z]{3}[0-9]{3}$")
SIDE = 32
MAGIC = b"VIT1"
MAX_PER_CHAR = 8


def _u16(value: int) -> bytes:
    return int(value).to_bytes(2, "little")


def nearest_resize(gray: Image.Image, left: int, top: int, right: int, bottom: int):
    """Sample ROI sub-rect [left,right)x[top,bottom) into SIDE x SIDE.

    Mirrors Recognizer::normalizeSlot(): out[y][x] = src[min(bottom-1-top...)]
    integer-division nearest neighbour, so generated templates line up with how
    the same ROI is normalised at runtime.
    """
    w = right - left
    h = bottom - top
    src = gray.load()
    out = bytearray(SIDE * SIDE)
    for y in range(SIDE):
        sy = min(h - 1, (y * h) // SIDE)
        for x in range(SIDE):
            sx = min(w - 1, (x * w) // SIDE)
            out[y * SIDE + x] = src[left + sx, top + sy]
    return out, w, h


def estimate_background(cell: bytearray) -> int:
    """Median of the 1px border ring; glyph ink should not touch the border."""
    ring = []
    for x in range(SIDE):
        ring += [cell[x], cell[(SIDE - 1) * SIDE + x]]
    for y in range(1, SIDE - 1):
        ring += [cell[y * SIDE], cell[y * SIDE + SIDE - 1]]
    ring.sort()
    return ring[len(ring) // 2]


def ink_bbox(cell: bytearray, background: int, delta: int = 12):
    min_x, min_y, max_x, max_y = SIDE, SIDE, -1, -1
    for y in range(SIDE):
        for x in range(SIDE):
            if abs(cell[y * SIDE + x] - background) > delta:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    return min_x, min_y, max_x, max_y


def next_index(char_dir: Path) -> int:
    used = {int(p.stem) for p in char_dir.glob("*.bin") if p.stem.isdigit()}
    for i in range(1, MAX_PER_CHAR + 1):
        if i not in used:
            return i
    raise SystemExit(f"{char_dir} already has {MAX_PER_CHAR} templates; remove one first")


def write_template(char_dir: Path, index: int, char: str, cell: bytearray) -> Path:
    bg = estimate_background(cell)
    bx, by, bw, bh = ink_bbox(cell, bg)
    if bw <= 0 or bh <= 0:
        raise SystemExit(f"slot for '{char}' has no visible ink; widen the ROI or check the screenshot")

    char_dir.mkdir(parents=True, exist_ok=True)
    path = char_dir / f"{index:02d}.bin"
    payload = MAGIC + _u16(SIDE) + _u16(SIDE)
    payload += _u16(bx) + _u16(by) + _u16(bw) + _u16(bh)
    payload += char.encode("ascii") + bytes([bg]) + bytes(cell)
    path.write_bytes(payload)
    return path


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent.parent / "templates"
    out_root = root
    index = None
    positional = []

    it = iter(argv)
    for arg in it:
        if arg == "-o":
            out_root = Path(next(it))
        elif arg == "--index":
            index = int(next(it))
        else:
            positional.append(arg)

    if len(positional) != 6:
        print(__doc__)
        return 2

    image_path, code, *roi_s = positional
    x, y, w, h = (int(v) for v in roi_s)
    if w < 6 or h < 1:
        print("error: ROI too small (w must be >= 6)")
        return 2
    if not CODE_RE.match(code):
        print(f"error: code must match ^[A-Z]{{3}}[0-9]{{3}}$ (got '{code}')")
        return 2

    image = Image.open(image_path).convert("L")
    iw, ih = image.size
    if x < 0 or y < 0 or x + w > iw or y + h > ih:
        print(f"error: ROI {x},{y} {w}x{h} is outside image {iw}x{ih}")
        return 2

    for slot in range(6):
        char = code[slot]
        left = x + (w * slot) // 6
        right = x + (w * (slot + 1)) // 6
        cell, _, _ = nearest_resize(image, left, y, right, y + h)
        char_dir = out_root / char
        slot_index = index if index is not None else next_index(char_dir)
        path = write_template(char_dir, slot_index, char, cell)
        print(f"slot {slot} '{char}' -> {path}")

    print(f"done: 6 templates under {out_root}  (background/foreground recomputed per slot)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
