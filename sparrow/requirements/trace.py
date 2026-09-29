"""Traceability matrix: requirements <-> implementation <-> tests, with consistency checks."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import yaml

from .scan import TestRef, scan_tests
from .schema import Project, Requirement, load_requirements


@dataclass
class Trace:
    project: Project
    requirements: list[Requirement]
    tests: list[TestRef]
    errors: list[str] = field(default_factory=list)

    def tests_for(self, requirement_id: str) -> list[TestRef]:
        return [t for t in self.tests if requirement_id in t.requirements]

    def requirements_for_file(self, path: Path) -> list[Requirement]:
        resolved = path.resolve()
        return [
            r
            for r in self.requirements
            if any(self._implementation_path(i) == resolved for i in r.implementation)
        ]

    def _implementation_path(self, reference: str) -> Path:
        return (self.project.root / reference.split("::")[0]).resolve()


def build_trace(project: Project) -> Trace:
    trace = Trace(project, load_requirements(project), scan_tests(project.test_dirs))
    _check_tests(trace)
    _check_implementation(trace)
    _check_parameters(trace)
    _check_model_faults(trace)
    return trace


def _check_tests(trace: Trace) -> None:
    known = {r.id: r for r in trace.requirements}
    for test in trace.tests:
        for requirement_id in test.requirements:
            requirement = known.get(requirement_id)
            where = f"{test.path.name}:{test.line} {test.key}"
            if requirement is None:
                trace.errors.append(f"{where} verifies unknown {requirement_id}")
            elif requirement.verification != "test":
                trace.errors.append(
                    f"{where} verifies {requirement_id}, which is verified by "
                    f"{requirement.verification}"
                )
    for requirement in trace.requirements:
        if requirement.verification == "test" and not trace.tests_for(requirement.id):
            trace.errors.append(f"{requirement.id} is verified by test but no test declares it")


def _check_implementation(trace: Trace) -> None:
    for requirement in trace.requirements:
        for reference in requirement.implementation:
            path_text, _, symbol = reference.partition("::")
            path = (trace.project.root / path_text).resolve()
            if not path.is_file():
                trace.errors.append(f"{requirement.id}: {reference}: file not found")
            elif symbol and symbol not in path.read_text(encoding="utf-8"):
                trace.errors.append(f"{requirement.id}: {reference}: symbol not found")


def _check_parameters(trace: Trace) -> None:
    if trace.project.model is None:
        return
    model = yaml.safe_load(trace.project.model.read_text(encoding="utf-8"))
    parameters = {p["name"] for p in model["config"]}
    for requirement in trace.requirements:
        for parameter in requirement.parameters:
            if parameter not in parameters:
                trace.errors.append(f"{requirement.id}: unknown config parameter '{parameter}'")


def _check_model_faults(trace: Trace) -> None:
    if trace.project.model is None:
        return
    model = yaml.safe_load(trace.project.model.read_text(encoding="utf-8"))
    ids = {r.id for r in trace.requirements}
    for fault in model["faults"]:
        requirement = fault.get("requirement", "")
        if requirement and requirement not in ids:
            trace.errors.append(f"fault {fault['name']} cites unknown {requirement}")
