"""Requirement records and project descriptor loading."""

from __future__ import annotations

import glob
import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

ID_PATTERN = re.compile(r"^REQ-\d{3}$")
PRIORITIES = ("must", "should")
VERIFICATION_METHODS = ("test", "inspection", "analysis")


class ProjectError(Exception):
    pass


@dataclass(frozen=True)
class Requirement:
    id: str
    title: str
    statement: str
    component: str
    priority: str
    verification: str
    rationale: str = ""
    parameters: tuple[str, ...] = ()
    implementation: tuple[str, ...] = ()
    source: Path = field(default=Path(), compare=False)


@dataclass(frozen=True)
class Project:
    name: str
    root: Path
    model: Path | None
    requirement_files: tuple[Path, ...]
    test_dirs: tuple[Path, ...]


def load_project(path: Path) -> Project:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    root = path.parent.resolve()
    files: list[Path] = []
    for pattern in raw["requirements"]:
        files += [Path(p).resolve() for p in sorted(glob.glob(str(root / pattern)))]
    if not files:
        raise ProjectError(f"{path}: no requirement files match {raw['requirements']}")
    model = (root / raw["model"]).resolve() if "model" in raw else None
    tests = tuple((root / t).resolve() for t in raw.get("tests", []))
    return Project(raw["name"], root, model, tuple(files), tests)


def _parse_requirement(item: dict, source: Path) -> Requirement:
    missing = [
        k
        for k in ("id", "title", "statement", "component", "priority", "verification")
        if k not in item
    ]
    if missing:
        raise ProjectError(f"{source.name}: requirement {item.get('id', '?')} lacks {missing}")
    requirement = Requirement(
        id=item["id"],
        title=item["title"],
        statement=" ".join(item["statement"].split()),
        component=item["component"],
        priority=item["priority"],
        verification=item["verification"],
        rationale=" ".join(item.get("rationale", "").split()),
        parameters=tuple(item.get("parameters", [])),
        implementation=tuple(item.get("implementation", [])),
        source=source,
    )
    if not ID_PATTERN.match(requirement.id):
        raise ProjectError(f"{source.name}: '{requirement.id}' is not of the form REQ-nnn")
    if requirement.priority not in PRIORITIES:
        raise ProjectError(f"{requirement.id}: priority must be one of {PRIORITIES}")
    if requirement.verification not in VERIFICATION_METHODS:
        raise ProjectError(f"{requirement.id}: verification must be one of {VERIFICATION_METHODS}")
    return requirement


def load_requirements(project: Project) -> list[Requirement]:
    requirements: list[Requirement] = []
    for path in project.requirement_files:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
        requirements += [_parse_requirement(item, path) for item in raw["requirements"]]
    ids = [r.id for r in requirements]
    duplicates = sorted({i for i in ids if ids.count(i) > 1})
    if duplicates:
        raise ProjectError(f"duplicate requirement IDs: {duplicates}")
    return sorted(requirements, key=lambda r: r.id)
