#!/usr/bin/env python3
"""Run the production C++ recognizer against a screenshot ROI.

Usage:
    python tools/test_sample.py screenshot.png MAM639 x y w h
    python tools/test_sample.py screenshot.png MAM639 x y w h --exe build/VALInviteOffline.exe

Pillow is used only to decode the development-time screenshot. Grayscale uses
the exact integer BT.601 conversion from src/app.cpp; recognition and all gates
run in the real C++ Recognizer.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image


CODE_RE = re.compile(r"^[A-Z]{3}[0-9]{3}$")


def image_pixels(image: Image.Image):
    """Use Pillow's non-deprecated flat iterator when available."""
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return image.getdata()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("expected")
    parser.add_argument("x", type=int)
    parser.add_argument("y", type=int)
    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("--exe", type=Path, help="VALInviteOffline executable")
    return parser.parse_args()


def find_executable(root: Path, explicit: Path | None) -> Path:
    if explicit is not None:
        candidate = explicit.resolve()
        if candidate.is_file():
            return candidate
        raise SystemExit(f"offline recognizer not found: {candidate}")

    candidates = (
        root / "build" / "VALInviteOffline.exe",
        root / "build" / "Release" / "VALInviteOffline.exe",
        root / "build" / "Debug" / "VALInviteOffline.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("VALInviteOffline.exe not found; build target VALInviteOffline first")


def grayscale_roi(image: Image.Image, box: tuple[int, int, int, int]) -> bytes:
    rgb = image.convert("RGB").crop(box)
    return bytes(
        (77 * red + 150 * green + 29 * blue + 128) >> 8
        for red, green, blue in image_pixels(rgb)
    )


def main() -> int:
    args = parse_args()
    if not CODE_RE.fullmatch(args.expected):
        raise SystemExit("expected code must match ^[A-Z]{3}[0-9]{3}$")
    if args.width < 6 or args.height < 1:
        raise SystemExit("ROI must be at least 6x1 pixels")

    root = Path(__file__).resolve().parent.parent
    executable = find_executable(root, args.exe)
    with Image.open(args.screenshot) as image:
        right = args.x + args.width
        bottom = args.y + args.height
        if args.x < 0 or args.y < 0 or right > image.width or bottom > image.height:
            raise SystemExit(
                f"ROI {args.x},{args.y} {args.width}x{args.height} "
                f"is outside image {image.width}x{image.height}"
            )
        raw_pixels = grayscale_roi(image, (args.x, args.y, right, bottom))

    raw_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".gray", delete=False) as raw:
            raw.write(raw_pixels)
            raw_path = Path(raw.name)

        print(f"Screenshot: {args.screenshot.resolve()}", flush=True)
        print(
            f"ROI: x={args.x} y={args.y} w={args.width} h={args.height}",
            flush=True,
        )
        completed = subprocess.run(
            [
                str(executable),
                str(raw_path),
                str(args.width),
                str(args.height),
                args.expected,
            ],
            check=False,
        )
        return completed.returncode
    finally:
        if raw_path is not None:
            raw_path.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
