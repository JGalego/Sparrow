"""The `ai:` section of a project descriptor and the paths it resolves to."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

import yaml

from ..codegen.model import load_model
from ..requirements.schema import Project, load_project


class AIProjectError(Exception):
    pass


@dataclass(frozen=True)
class AIProject:
    project: Project
    repo: Path
    gates: tuple[str, ...]
    paths: dict[str, tuple[str, ...]]  # task -> globs relative to the repository root
    protected: tuple[str, ...]  # generated files, relative to the repository root

    def globs(self, key: str) -> tuple[str, ...]:
        if key not in self.paths:
            raise AIProjectError(f"sparrow.yaml: ai.paths.{key} is not defined")
        return self.paths[key]

    def files(self, key: str) -> list[Path]:
        found: set[Path] = set()
        for pattern in self.globs(key):
            found.update(p for p in self.repo.glob(pattern) if p.is_file())
        return sorted(found)

    def protected_paths(self) -> list[Path]:
        return [self.repo / p for p in self.protected]

    def relative(self, path: Path) -> str:
        return path.resolve().relative_to(self.repo).as_posix()


def _repo_root(start: Path) -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=start,
        capture_output=True,
        text=True,
        check=False,
    )
    return Path(result.stdout.strip()).resolve() if result.returncode == 0 else start.resolve()


def load_ai_project(descriptor: Path) -> AIProject:
    project = load_project(descriptor)
    raw = yaml.safe_load(descriptor.read_text(encoding="utf-8")).get("ai")
    if not raw:
        raise AIProjectError(f"{descriptor}: no 'ai' section")
    repo = _repo_root(project.root)

    def to_repo(pattern: str) -> str:
        return (project.root / pattern).resolve().relative_to(repo).as_posix()

    paths = {key: tuple(to_repo(p) for p in globs) for key, globs in raw["paths"].items()}
    protected: list[str] = []
    if project.model is not None:
        model = load_model(project.model)
        protected += [path.relative_to(repo).as_posix() for path in model.outputs.values()]
    protected += [to_repo(p) for p in raw.get("protected", [])]
    return AIProject(project, repo, tuple(raw.get("gates", [])), paths, tuple(protected))
