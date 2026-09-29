"""Writes (or checks) the generated files of a model."""

from __future__ import annotations

from pathlib import Path

from . import emit_c, emit_python
from .model import Model, load_model


def render(model: Model) -> dict[Path, str]:
    return {
        model.outputs["c_header"]: emit_c.header(model),
        model.outputs["c_source"]: emit_c.source(model),
        model.outputs["python"]: emit_python.source(model),
    }


def generate(model_path: Path, check: bool = False) -> list[Path]:
    """Writes the generated files. With check=True, returns stale files instead of writing."""
    stale = []
    for path, content in render(load_model(model_path)).items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == content:
            continue
        stale.append(path)
        if not check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
    return stale
