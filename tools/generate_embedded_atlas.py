#!/usr/bin/env python3
"""Generate the compile-time A-Z/0-9 glyph atlas from Montserrat.

The TTF is a development input only. The generated C++ header contains five
small raster variants per character and is the only artifact used at runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
CANVAS = 32
CONTENT = 28
FOREGROUND_THRESHOLD = 32
EXPECTED_FONT_SHA256 = "0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7"


@dataclass(frozen=True)
class Variant:
    weight: int
    font_size: int
    horizontal_scale: float
    vertical_scale: float
    intensity: int
    blur: float
    offset_x: int
    offset_y: int


VARIANTS = (
    Variant(700, 14, 1.00, 1.05, 200, 0.35, 0, 0),
    Variant(700, 14, 0.90, 1.05, 200, 0.35, -1, 0),
    Variant(700, 14, 1.10, 1.05, 200, 0.35, 1, 0),
    Variant(800, 14, 1.00, 1.05, 200, 0.35, 0, 0),
    Variant(700, 14, 1.00, 1.00, 230, 0.00, 0, 0),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "generated" / "glyph_templates.hpp",
    )
    parser.add_argument(
        "--allow-font-hash-mismatch",
        action="store_true",
        help="explicitly allow a different Montserrat build",
    )
    return parser.parse_args()


def load_font(path: Path, variant: Variant) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(path), variant.font_size)
    axes = font.get_variation_axes()
    if axes:
        font.set_variation_by_axes([variant.weight])
    return font


def render_raw(value: str, font_path: Path, variant: Variant) -> Image.Image:
    font = load_font(font_path, variant)
    work = Image.new("L", (96, 96), 0)
    draw = ImageDraw.Draw(work)
    bbox = draw.textbbox((0, 0), value, font=font)
    draw.text((8 - bbox[0], 8 - bbox[1]), value, font=font, fill=variant.intensity)
    glyph_bbox = work.getbbox()
    if glyph_bbox is None:
        raise RuntimeError(f"font produced empty glyph '{value}'")
    work = work.crop(glyph_bbox)
    work = work.resize(
        (
            max(1, round(work.width * variant.horizontal_scale)),
            max(1, round(work.height * variant.vertical_scale)),
        ),
        Image.Resampling.LANCZOS,
    )
    if variant.blur > 0:
        work = work.filter(ImageFilter.GaussianBlur(variant.blur))
    return work


def resize_bilinear(source: np.ndarray, width: int, height: int) -> np.ndarray:
    source_height, source_width = source.shape
    output = np.zeros((height, width), dtype=np.uint8)
    for y in range(height):
        source_y = (y + 0.5) * source_height / height - 0.5
        y0 = max(0, min(source_height - 1, math.floor(source_y)))
        y1 = min(source_height - 1, y0 + 1)
        fy = max(0.0, min(1.0, source_y - y0))
        for x in range(width):
            source_x = (x + 0.5) * source_width / width - 0.5
            x0 = max(0, min(source_width - 1, math.floor(source_x)))
            x1 = min(source_width - 1, x0 + 1)
            fx = max(0.0, min(1.0, source_x - x0))
            top = source[y0, x0] * (1.0 - fx) + source[y0, x1] * fx
            bottom = source[y1, x0] * (1.0 - fx) + source[y1, x1] * fx
            output[y, x] = round(top * (1.0 - fy) + bottom * fy)
    return output


def normalize(raw: Image.Image, variant: Variant) -> np.ndarray:
    pixels = np.asarray(raw, dtype=np.uint8)
    pixels = np.where(pixels > FOREGROUND_THRESHOLD, pixels, 0).astype(np.uint8)
    ys, xs = np.nonzero(pixels)
    if len(xs) == 0:
        raise RuntimeError("empty rendered glyph after thresholding")
    cropped = pixels[
        int(ys.min()) : int(ys.max()) + 1,
        int(xs.min()) : int(xs.max()) + 1,
    ]
    scale = min(CONTENT / cropped.shape[1], CONTENT / cropped.shape[0])
    width = max(1, round(cropped.shape[1] * scale))
    height = max(1, round(cropped.shape[0] * scale))
    resized = resize_bilinear(cropped, width, height)
    canvas = np.zeros((CANVAS, CANVAS), dtype=np.uint8)
    left = (CANVAS - width) // 2 + variant.offset_x
    top = (CANVAS - height) // 2 + variant.offset_y
    if left < 0 or top < 0 or left + width > CANVAS or top + height > CANVAS:
        raise RuntimeError("variant offset moves glyph outside canvas")
    canvas[top : top + height, left : left + width] = resized
    return canvas


def format_pixels(pixels: np.ndarray, indent: str) -> list[str]:
    flat = pixels.reshape(-1)
    lines = []
    for start in range(0, len(flat), 16):
        values = ", ".join(f"0x{int(value):02x}" for value in flat[start : start + 16])
        lines.append(f"{indent}{values},")
    return lines


def generate_header(font_path: Path) -> str:
    lines = [
        "// Generated by tools/generate_embedded_atlas.py. Do not edit manually.",
        "// Source: Montserrat variable font, SIL Open Font License 1.1.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace valinvite::generated {",
        "",
        f"inline constexpr int kGlyphCanvas = {CANVAS};",
        f"inline constexpr int kGlyphContent = {CONTENT};",
        f"inline constexpr std::size_t kGlyphCount = {len(ALPHABET)};",
        f"inline constexpr std::size_t kVariantsPerGlyph = {len(VARIANTS)};",
        f'inline constexpr char kGlyphValues[] = "{ALPHABET}";',
        "",
        "struct GlyphVariantMetadata final {",
        "    int weight;",
        "    int fontSize;",
        "    int horizontalScalePercent;",
        "    int verticalScalePercent;",
        "    int intensity;",
        "    int blurHundredths;",
        "    int offsetX;",
        "    int offsetY;",
        "};",
        "",
        "inline constexpr GlyphVariantMetadata kVariantMetadata[] = {",
    ]
    for variant in VARIANTS:
        lines.append(
            "    {"
            f"{variant.weight}, {variant.font_size}, "
            f"{round(variant.horizontal_scale * 100)}, "
            f"{round(variant.vertical_scale * 100)}, "
            f"{variant.intensity}, {round(variant.blur * 100)}, "
            f"{variant.offset_x}, {variant.offset_y}"
            "},"
        )
    lines += [
        "};",
        "",
        "inline constexpr std::uint8_t",
        "    kGlyphTemplates[kGlyphCount][kVariantsPerGlyph][kGlyphCanvas * kGlyphCanvas] = {",
    ]
    for value in ALPHABET:
        lines.append(f"    {{ // {value}")
        for index, variant in enumerate(VARIANTS):
            lines.append(f"        {{ // variant {index}")
            pixels = normalize(render_raw(value, font_path, variant), variant)
            lines.extend(format_pixels(pixels, "            "))
            lines.append("        },")
        lines.append("    },")
    lines += [
        "};",
        "",
        "} // namespace valinvite::generated",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    font_path = args.font.resolve()
    if not font_path.is_file():
        raise SystemExit(f"font not found: {font_path}")
    digest = hashlib.sha256(font_path.read_bytes()).hexdigest()
    if digest != EXPECTED_FONT_SHA256 and not args.allow_font_hash_mismatch:
        raise SystemExit(
            f"unexpected font SHA-256 {digest}; expected {EXPECTED_FONT_SHA256}. "
            "Use the pinned source or explicitly allow a mismatch."
        )
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate_header(font_path), encoding="utf-8", newline="\n")
    print(
        f"generated {len(ALPHABET)} glyphs x {len(VARIANTS)} variants "
        f"= {len(ALPHABET) * len(VARIANTS)} templates: {output}"
    )
    print(f"font SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
