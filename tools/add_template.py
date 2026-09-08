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
    python tools/add_template.py --selftest

Arguments:
    screenshot.png   real Douyin/VALORANT livestream screenshot
    MAM639           the six characters shown inside the ROI (ground truth)
    x y w h          ROI in image pixels that tightly wraps the six characters
    -o ROOT          template root (default: ./templates next to this script)
    --index NN       two-digit file number, default = next free slot (01..08)
    --selftest       verify bbox math + VIT1 round-trip on synthetic cells

Output format (binary VIT1, exactly what src/recognizer.cpp reads):
    "VIT1" (4) + u16 width + u16 height + u16 bboxX/Y/W/H + char (1)
    + background gray (1) + 32*32 raw grayscale bytes.
Requires Pillow (only to decode the PNG; the app itself stays pure C++).
"""
from __future__ import annotations

import re
import shutil
import struct
import sys
import tempfile
from pathlib import Path

from PIL import Image

CODE_RE = re.compile(r"^[A-Z]{3}[0-9]{3}$")
SIDE = 32
MAGIC = b"VIT1"
MAX_PER_CHAR = 8
HEADER_BYTES = len(MAGIC) + 6 * 2 + 1 + 1  # 18
FILE_BYTES = HEADER_BYTES + SIDE * SIDE  # 1042


def _u16(value: int) -> bytes:
    return int(value).to_bytes(2, "little")


def parse_vit1(path: Path):
    """Read a binary VIT1 template back and sanity-check it.

    Returns (width, height, bbox_x, bbox_y, bbox_w, bbox_h, char, background, pixels).
    """
    raw = path.read_bytes()
    if raw[:4] != MAGIC:
        raise SystemExit(f"{path}: bad magic")
    if len(raw) != FILE_BYTES:
        raise SystemExit(f"{path}: size {len(raw)} != {FILE_BYTES}")
    width, height, bx, by, bw, bh = struct.unpack("<HHHHHH", raw[4:16])
    char = raw[16:17].decode("ascii")
    background = raw[17]
    return width, height, bx, by, bw, bh, char, background, raw[18:]


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
    """Inclusive ink bounds. Returns min_x, min_y, max_x, max_y (-1 when empty)."""
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

    # ink_bbox() returns INCLUSIVE min/max corners - never pass max values to
    # the VIT1 width/height fields. Derive an actual bbox box here.
    min_x, min_y, max_x, max_y = ink_bbox(cell, bg)
    if max_x < min_x or max_y < min_y:
        raise SystemExit(f"slot for '{char}' has no visible ink; widen the ROI or check the screenshot")
    bbox_x = min_x
    bbox_y = min_y
    bbox_w = max_x - min_x + 1
    bbox_h = max_y - min_y + 1
    if bbox_x + bbox_w > SIDE or bbox_y + bbox_h > SIDE:
        raise SystemExit(f"slot for '{char}' produced an out-of-range bbox {bbox_x},{bbox_y} {bbox_w}x{bbox_h}")

    char_dir.mkdir(parents=True, exist_ok=True)
    path = char_dir / f"{index:02d}.bin"
    payload = MAGIC + _u16(SIDE) + _u16(SIDE)
    payload += _u16(bbox_x) + _u16(bbox_y) + _u16(bbox_w) + _u16(bbox_h)
    payload += char.encode("ascii") + bytes([bg]) + bytes(cell)
    path.write_bytes(payload)

    # Reparse the file exactly like the C++ loader and assert invariants.
    _, _, rx, ry, rw, rh, rchar, _, pixels = parse_vit1(path)
    assert (rx, ry, rw, rh) == (bbox_x, bbox_y, bbox_w, bbox_h), "bbox round-trip mismatch"
    assert rchar == char, f"char round-trip mismatch: {rchar} != {char}"
    assert rx + rw <= SIDE and ry + rh <= SIDE, "bbox exceeds 32x32"
    assert len(pixels) == SIDE * SIDE
    return path


def selftest() -> int:
    """Synthetic cells with a known ink rectangle -> verify bbox + VIT1 round-trip."""
    bg = 20
    cell = bytearray(bytes([bg]) * (SIDE * SIDE))
    for y in range(5, 21):  # rows 5..20 inclusive
        for x in range(8, 16):  # columns 8..15 inclusive
            cell[y * SIDE + x] = 235

    min_x, min_y, max_x, max_y = ink_bbox(cell, bg)
    assert (min_x, min_y, max_x, max_y) == (8, 5, 15, 20), (min_x, min_y, max_x, max_y)
    assert (max_x - min_x + 1, max_y - min_y + 1) == (8, 16), "bbox width/height"

    # A glyph touching no border on a top-left ink should also be found.
    cell2 = bytearray(bytes([bg]) * (SIDE * SIDE))
    cell2[2 * SIDE + 2] = 235
    cell2[3 * SIDE + 3] = 235
    assert ink_bbox(cell2, bg) == (2, 2, 3, 3)

    tmp = Path(tempfile.mkdtemp(prefix="vit1_selftest_"))
    try:
        path = write_template(tmp, 1, "M", cell)
        width, height, bx, by, bw, bh, char, background, _ = parse_vit1(path)
        assert (width, height) == (SIDE, SIDE)
        assert (bx, by, bw, bh) == (8, 5, 8, 16), (bx, by, bw, bh)
        assert bx + bw <= SIDE and by + bh <= SIDE
        assert char == "M"
        assert path.stat().st_size == FILE_BYTES
        # task: parse header back with struct and assert the bounds invariant.
        assert bx + bw <= SIDE and by + bh <= SIDE
        print(f"selftest passed: bbox={bx},{by} {bw}x{bh} size={path.stat().st_size}B")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()

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
