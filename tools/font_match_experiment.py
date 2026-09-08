#!/usr/bin/env python3
"""Compare segmented real invite-code glyphs with a few candidate fonts.

This is a development-only experiment. It never participates in runtime
recognition and does not write templates.

Example:
    python tools/font_match_experiment.py screenshot.png MAM639 390 370 70 18 \
        --font Montserrat=C:/fonts/Montserrat.ttf --top 5
"""
from __future__ import annotations

import argparse
import itertools
import re
from dataclasses import dataclass, replace
from functools import cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


CANVAS = 32
CONTENT = 28
CODE_RE = re.compile(r"^[A-Z]{3}[0-9]{3}$")


@dataclass(frozen=True)
class Glyph:
    value: str
    x0: int
    x1: int
    normalized: np.ndarray


@dataclass(frozen=True)
class Setting:
    font_name: str
    font_path: Path
    weight: int
    font_size: int
    horizontal_scale: float
    vertical_scale: float
    tracking: int
    intensity: int
    blur: float
    resample: str


@dataclass(frozen=True)
class Result:
    score: float
    shape_score: float
    geometry_score: float
    setting: Setting
    glyph_scores: tuple[float, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("code")
    parser.add_argument("x", type=int)
    parser.add_argument("y", type=int)
    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument(
        "--font",
        action="append",
        required=True,
        metavar="NAME=PATH",
        help="candidate variable/static TTF; may be supplied more than once",
    )
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--threshold", type=int, default=32)
    return parser.parse_args()


def image_pixels(image: Image.Image):
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return image.getdata()


def runtime_grayscale(image: Image.Image) -> Image.Image:
    rgb = image.convert("RGB")
    pixels = bytes(
        (77 * red + 150 * green + 29 * blue + 128) >> 8
        for red, green, blue in image_pixels(rgb)
    )
    return Image.frombytes("L", rgb.size, pixels)


def estimate_background(gray: np.ndarray) -> int:
    border = np.concatenate((gray[0], gray[-1], gray[1:-1, 0], gray[1:-1, -1]))
    return int(np.median(border))


def projection_runs(mask: np.ndarray, min_pixels: int = 2) -> list[tuple[int, int]]:
    active = np.count_nonzero(mask, axis=0) >= min_pixels
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for x, value in enumerate(active):
        if value and start is None:
            start = x
        if not value and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, len(active) - 1))
    return runs


def merge_fractures(
    runs: list[tuple[int, int]], target_count: int, max_gap: int = 1
) -> list[tuple[int, int]]:
    """Merge only as many tiny internal gaps as needed to reach target_count."""
    result = list(runs)
    while len(result) > target_count:
        gaps = [result[index + 1][0] - result[index][1] - 1 for index in range(len(result) - 1)]
        smallest = min(gaps, default=max_gap + 1)
        if smallest > max_gap:
            break
        index = gaps.index(smallest)
        result[index : index + 2] = [(result[index][0], result[index + 1][1])]
    return result


def fit_canvas(image: Image.Image, resample: Image.Resampling) -> np.ndarray:
    bbox = image.getbbox()
    if bbox is None:
        raise ValueError("empty glyph")
    cropped = image.crop(bbox)
    scale = min(CONTENT / cropped.width, CONTENT / cropped.height)
    width = max(1, round(cropped.width * scale))
    height = max(1, round(cropped.height * scale))
    resized = cropped.resize((width, height), resample)
    canvas = Image.new("L", (CANVAS, CANVAS), 0)
    canvas.paste(resized, ((CANVAS - width) // 2, (CANVAS - height) // 2))
    return np.asarray(canvas, dtype=np.float32)


def extract_real_glyphs(
    screenshot: Path,
    code: str,
    roi_box: tuple[int, int, int, int],
    threshold: int,
) -> tuple[list[Glyph], int, list[tuple[int, int]]]:
    with Image.open(screenshot) as source:
        gray_image = runtime_grayscale(source).crop(roi_box)
    gray = np.asarray(gray_image, dtype=np.int16)
    background = estimate_background(gray)
    delta = np.abs(gray - background)
    mask = delta > threshold
    runs = projection_runs(mask)
    runs = [
        run
        for run in runs
        if run[1] - run[0] + 1 >= 2
        and np.count_nonzero(mask[:, run[0] : run[1] + 1]) >= 8
    ]
    runs = merge_fractures(runs, 6)
    if len(runs) != 6:
        raise SystemExit(f"expected 6 glyph runs, found {len(runs)}: {runs}")

    glyphs: list[Glyph] = []
    roi_x = roi_box[0]
    for value, (x0, x1) in zip(code, runs, strict=True):
        glyph_mask = mask[:, x0 : x1 + 1]
        ys = np.flatnonzero(np.any(glyph_mask, axis=1))
        if len(ys) == 0:
            raise SystemExit(f"glyph {value} has no foreground pixels")
        y0, y1 = int(ys[0]), int(ys[-1])
        ink = np.where(
            glyph_mask[y0 : y1 + 1],
            np.clip(delta[y0 : y1 + 1, x0 : x1 + 1], 0, 255),
            0,
        ).astype(np.uint8)
        normalized = fit_canvas(Image.fromarray(ink, mode="L"), Image.Resampling.LANCZOS)
        glyphs.append(Glyph(value, roi_x + x0, roi_x + x1, normalized))
    return glyphs, background, runs


def set_font_axes(font: ImageFont.FreeTypeFont, name: str, weight: int, size: int) -> None:
    axes = font.get_variation_axes()
    if not axes:
        return
    lower_name = name.lower()
    if "inter" in lower_name and len(axes) == 2:
        font.set_variation_by_axes([max(14, min(32, size)), weight])
    elif "noto" in lower_name and len(axes) == 2:
        font.set_variation_by_axes([weight, 100])
    else:
        font.set_variation_by_axes([weight])


@cache
def load_font(path: Path, name: str, size: int, weight: int) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(path), size)
    set_font_axes(font, name, weight, size)
    return font


@cache
def render_glyph(value: str, setting: Setting) -> np.ndarray:
    font = load_font(
        setting.font_path, setting.font_name, setting.font_size, setting.weight
    )
    work = Image.new("L", (96, 96), 0)
    draw = ImageDraw.Draw(work)
    bbox = draw.textbbox((0, 0), value, font=font)
    draw.text((8 - bbox[0], 8 - bbox[1]), value, font=font, fill=setting.intensity)
    glyph_bbox = work.getbbox()
    if glyph_bbox is None:
        raise ValueError(f"font produced an empty glyph for {value}")
    work = work.crop(glyph_bbox)
    width = max(1, round(work.width * setting.horizontal_scale))
    height = max(1, round(work.height * setting.vertical_scale))
    resample = (
        Image.Resampling.LANCZOS
        if setting.resample == "lanczos"
        else Image.Resampling.BILINEAR
    )
    work = work.resize((width, height), resample)
    normalized = Image.fromarray(fit_canvas(work, resample).astype(np.uint8), mode="L")
    if setting.blur > 0:
        normalized = normalized.filter(ImageFilter.GaussianBlur(setting.blur))
    return np.asarray(normalized, dtype=np.float32)


def cosine(left: np.ndarray, right: np.ndarray) -> float:
    left_norm = float(np.linalg.norm(left))
    right_norm = float(np.linalg.norm(right))
    if left_norm == 0 or right_norm == 0:
        return 0.0
    return float(np.dot(left.ravel(), right.ravel()) / (left_norm * right_norm))


def similarity(real: np.ndarray, rendered: np.ndarray) -> float:
    sad = 1.0 - float(np.mean(np.abs(real - rendered))) / 255.0
    real_centered = real - float(np.mean(real))
    rendered_centered = rendered - float(np.mean(rendered))
    ncc = max(0.0, cosine(real_centered, rendered_centered))
    real_edge = np.hypot(*np.gradient(real))
    rendered_edge = np.hypot(*np.gradient(rendered))
    edge = max(0.0, cosine(real_edge, rendered_edge))
    return 0.30 * sad + 0.45 * ncc + 0.25 * edge


def geometry_vector(runs: list[tuple[int, int]]) -> np.ndarray:
    values: list[float] = []
    for index, (start, end) in enumerate(runs):
        values.append(float(end - start + 1))
        if index + 1 < len(runs):
            values.append(float(runs[index + 1][0] - end - 1))
    vector = np.asarray(values, dtype=np.float32)
    return vector / max(1.0, float(np.sum(vector)))


@cache
def rendered_geometry(code: str, setting: Setting) -> np.ndarray | None:
    font = load_font(
        setting.font_path, setting.font_name, setting.font_size, setting.weight
    )
    work = Image.new("L", (512, 128), 0)
    draw = ImageDraw.Draw(work)
    cursor = 16.0
    for value in code:
        bbox = draw.textbbox((0, 0), value, font=font)
        draw.text((cursor - bbox[0], 16 - bbox[1]), value, font=font, fill=255)
        cursor += float(draw.textlength(value, font=font)) + setting.tracking
    bbox = work.getbbox()
    if bbox is None:
        return None
    work = work.crop(bbox)
    work = work.resize(
        (
            max(1, round(work.width * setting.horizontal_scale)),
            max(1, round(work.height * setting.vertical_scale)),
        ),
        Image.Resampling.LANCZOS,
    )
    runs = projection_runs(np.asarray(work) > 8, min_pixels=1)
    if len(runs) != 6:
        return None
    return geometry_vector(runs)


def evaluate(
    glyphs: list[Glyph],
    real_geometry: np.ndarray,
    code: str,
    setting: Setting,
) -> Result:
    shape_setting = replace(setting, tracking=0)
    scores = tuple(
        similarity(glyph.normalized, render_glyph(glyph.value, shape_setting))
        for glyph in glyphs
    )
    shape_score = float(np.mean(scores))
    geometry_setting = replace(
        setting, intensity=255, blur=0.0, resample="lanczos"
    )
    candidate_geometry = rendered_geometry(code, geometry_setting)
    if candidate_geometry is None or candidate_geometry.shape != real_geometry.shape:
        geometry_score = 0.0
    else:
        geometry_score = max(
            0.0,
            1.0 - float(np.mean(np.abs(real_geometry - candidate_geometry))) * 5.0,
        )
    return Result(
        0.90 * shape_score + 0.10 * geometry_score,
        shape_score,
        geometry_score,
        setting,
        scores,
    )


def parse_fonts(values: list[str]) -> list[tuple[str, Path]]:
    fonts: list[tuple[str, Path]] = []
    for value in values:
        if "=" not in value:
            raise SystemExit(f"invalid --font '{value}', expected NAME=PATH")
        name, raw_path = value.split("=", 1)
        path = Path(raw_path).resolve()
        if not path.is_file():
            raise SystemExit(f"font not found: {path}")
        fonts.append((name, path))
    return fonts


def main() -> int:
    args = parse_args()
    if not CODE_RE.fullmatch(args.code):
        raise SystemExit("code must match ^[A-Z]{3}[0-9]{3}$")
    if args.width < 6 or args.height < 1:
        raise SystemExit("ROI is too small")
    if not args.screenshot.is_file():
        raise SystemExit(f"screenshot not found: {args.screenshot}")
    with Image.open(args.screenshot) as source:
        if (
            args.x < 0
            or args.y < 0
            or args.x + args.width > source.width
            or args.y + args.height > source.height
        ):
            raise SystemExit(
                f"ROI {args.x},{args.y} {args.width}x{args.height} "
                f"is outside image {source.width}x{source.height}"
            )
    roi = (args.x, args.y, args.x + args.width, args.y + args.height)
    glyphs, background, relative_runs = extract_real_glyphs(
        args.screenshot, args.code, roi, args.threshold
    )
    real_geometry = geometry_vector(relative_runs)
    print(f"Background: {background}")
    print("Glyph x-ranges: " + " ".join(
        f"{glyph.value}={glyph.x0}-{glyph.x1}" for glyph in glyphs
    ))

    results: list[Result] = []
    for font_name, font_path in parse_fonts(args.font):
        for values in itertools.product(
            (700, 800),
            range(12, 23),
            (0.90, 1.00, 1.10),
            (0.95, 1.00, 1.05),
            (-1, 0, 1),
            (200, 230, 255),
            (0.0, 0.35),
            ("bilinear", "lanczos"),
        ):
            setting = Setting(font_name, font_path, *values)
            results.append(evaluate(glyphs, real_geometry, args.code, setting))

    results.sort(key=lambda result: result.score, reverse=True)

    def print_result(label: str, result: Result) -> None:
        setting = result.setting
        print(
            f"{label}: font={setting.font_name} weight={setting.weight} "
            f"size={setting.font_size} hscale={setting.horizontal_scale:.2f} "
            f"vscale={setting.vertical_scale:.2f} tracking={setting.tracking:+d} "
            f"intensity={setting.intensity} blur={setting.blur:.2f} "
            f"aa={setting.resample} score={result.score:.6f} "
            f"shape={result.shape_score:.6f} geometry={result.geometry_score:.6f}"
        )
        print("  glyphs: " + " ".join(
            f"{glyph.value}={score:.6f}"
            for glyph, score in zip(glyphs, result.glyph_scores, strict=True)
        ))

    print("Best per font:")
    for font_name, _ in parse_fonts(args.font):
        best = next(result for result in results if result.setting.font_name == font_name)
        print_result(f"Best {font_name}", best)

    print("Overall Top-K:")
    for rank, result in enumerate(results[: max(1, args.top)], start=1):
        print_result(f"Top {rank}", result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
