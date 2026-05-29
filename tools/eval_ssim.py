#!/usr/bin/env python3
"""Compute SSIM of Low/Medium screenshots vs High reference."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity

ROOT = Path(__file__).resolve().parents[1]


def find_screenshot_dir() -> Path:
    candidates = [
        ROOT / "results" / "screenshots",
        ROOT / "build" / "Release" / "results" / "screenshots",
        ROOT / "build" / "Debug" / "results" / "screenshots",
    ]
    for directory in candidates:
        if (directory / "reference_high.png").is_file():
            return directory
    return candidates[0]


SCREENSHOT_DIR = find_screenshot_dir()
OUTPUT_JSON = ROOT / "results" / "ssim_results.json"

REFERENCE_HIGH = SCREENSHOT_DIR / "reference_high.png"
REFERENCE_MEDIUM = SCREENSHOT_DIR / "test_medium.png"

# Comparisons: name -> (reference_path, test_path). High is the formal ground truth.
COMPARISONS = {
    "low_vs_high": (REFERENCE_HIGH, SCREENSHOT_DIR / "test_low.png"),
    "medium_vs_high": (REFERENCE_HIGH, SCREENSHOT_DIR / "test_medium.png"),
    "low_vs_medium": (REFERENCE_MEDIUM, SCREENSHOT_DIR / "test_low.png"),
}

# Optional pixel crops at 1280×720 (evaluation pose). NOT separate objects in the app —
# just rectangles to compare localized regions. full_frame is the formal metric.
ROIS = {
    "center_viewport": (430, 120, 420, 420),  # optional crop; tune for your scene (e.g. glb/porsche.glb)
    "metal_sphere": (820, 280, 220, 220),
    "dielectric_sphere": (240, 280, 220, 220),
}


def load_rgb(path: Path) -> np.ndarray:
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float64)


def compute_ssim(reference: np.ndarray, test: np.ndarray) -> float:
    return float(
        structural_similarity(
            reference,
            test,
            channel_axis=-1,
            data_range=255.0,
            gaussian_weights=True,
            sigma=1.5,
            use_sample_covariance=False,
        )
    )


def crop(arr: np.ndarray, roi: tuple[int, int, int, int]) -> np.ndarray:
    x, y, w, h = roi
    return arr[y : y + h, x : x + w]


def main() -> int:
    print(f"Using screenshots from: {SCREENSHOT_DIR}")
    if not REFERENCE_HIGH.is_file():
        print(f"Missing reference image: {REFERENCE_HIGH}", file=sys.stderr)
        print("Capture High preset (P) and ensure screenshots are in results/screenshots.", file=sys.stderr)
        return 1

    results: dict = {
        "screenshot_dir": str(SCREENSHOT_DIR.relative_to(ROOT)),
        "note": "full_frame is the formal SSIM metric (entire 1280x720 screenshot). "
        "roi.* are optional pixel crops only (see ROIS in this script).",
        "comparisons": {},
    }

    for key, (ref_path, test_path) in COMPARISONS.items():
        if not ref_path.is_file():
            print(f"Warning: missing reference {ref_path}", file=sys.stderr)
            continue
        if not test_path.is_file():
            print(f"Warning: missing test {test_path}", file=sys.stderr)
            continue

        reference = load_rgb(ref_path)
        test = load_rgb(test_path)
        if test.shape != reference.shape:
            print(f"Shape mismatch {test_path.name}: {test.shape} vs {reference.shape}", file=sys.stderr)
            return 1

        entry = {
            "reference_image": str(ref_path.relative_to(ROOT)),
            "test_image": str(test_path.relative_to(ROOT)),
            "full_frame": compute_ssim(reference, test),
        }

        roi_scores = {}
        for roi_name, roi in ROIS.items():
            roi_scores[roi_name] = compute_ssim(crop(reference, roi), crop(test, roi))
        entry["roi"] = roi_scores

        results["comparisons"][key] = entry
        print(f"{key}: full_frame SSIM = {entry['full_frame']:.4f}")
        for roi_name, score in roi_scores.items():
            print(f"  {roi_name}: {score:.4f}")

    OUTPUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT_JSON.open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    print(f"Wrote {OUTPUT_JSON}")
    return 0 if results["comparisons"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
