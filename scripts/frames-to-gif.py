#!/usr/bin/env python3
"""Assembles frame_NNNN.ppm files into a looping GIF with one shared palette."""

import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frame-ms", type=int, default=100)
    parser.add_argument("--hold-last-ms", type=int, default=2500)
    parser.add_argument("--width", type=int, default=0, help="scale to this width (0 = keep)")
    args = parser.parse_args()

    frames = [Image.open(p).convert("RGB") for p in sorted(args.frames_dir.glob("frame_*.ppm"))]
    if not frames:
        raise SystemExit(f"no frames in {args.frames_dir}")
    if args.width:
        height = round(frames[0].height * args.width / frames[0].width)
        frames = [f.resize((args.width, height), Image.LANCZOS) for f in frames]
    # One palette for the whole clip avoids color flicker between frames.
    palette = frames[len(frames) // 2].quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    indexed = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    durations = [args.frame_ms] * len(indexed)
    durations[-1] = args.hold_last_ms
    indexed[0].save(
        args.output,
        save_all=True,
        append_images=indexed[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=1,
    )


if __name__ == "__main__":
    main()
