"""Loads a controller shared library and locates it in the build tree."""

from __future__ import annotations

import ctypes
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def find_library(name: str) -> Path:
    """Finds lib<name>.so under $SPARROW_BUILD_DIR or build/host."""
    build_dir = Path(os.environ.get("SPARROW_BUILD_DIR", REPO_ROOT / "build" / "host"))
    matches = sorted(build_dir.rglob(f"lib{name}.so"))
    if not matches:
        raise FileNotFoundError(
            f"lib{name}.so not found under {build_dir}; build it first with `make build`"
        )
    return matches[0]


def load_library(name: str) -> ctypes.CDLL:
    return ctypes.CDLL(str(find_library(name)))
