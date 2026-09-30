"""One task per workflow stage. Each task selects context and says what it may change."""

from __future__ import annotations

import ast
import re
import subprocess
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import yaml

from ..requirements.results import load_results
from ..requirements.trace import Trace, build_trace
from .analysis import summarize_trace
from .context import Context
from .project import AIProject

RESULTS_GLOB = "build/host/results/*.xml"


class TaskError(Exception):
    pass


@dataclass(frozen=True)
class Task:
    name: str
    stage: str
    kind: str  # "proposal" or "report"
    paths: tuple[str, ...]  # ai.paths keys the task may change (proposals only)
    argument: str  # help text for the positional argument
    build: Callable[[AIProject, Trace, Context, str, dict], str]  # returns the instruction


def _requirement(trace: Trace, requirement_id: str):
    for requirement in trace.requirements:
        if requirement.id == requirement_id:
            return requirement
    raise TaskError(f"unknown requirement {requirement_id}")


def _requirement_yaml(requirement) -> str:
    return yaml.safe_dump(
        {
            "id": requirement.id,
            "title": requirement.title,
            "statement": requirement.statement,
            "rationale": requirement.rationale,
            "parameters": list(requirement.parameters),
            "implementation": list(requirement.implementation),
        },
        sort_keys=False,
        width=100,
    )


def _implementation_files(ai: AIProject, requirement) -> list[Path]:
    return [(ai.project.root / ref.split("::")[0]).resolve() for ref in requirement.implementation]


def _add_protected(ai: AIProject, context: Context) -> None:
    context.add("protected (generated) files", "\n".join(ai.protected))


def _add_generated_headers(ai: AIProject, context: Context) -> None:
    context.add_files([p for p in ai.protected_paths() if p.suffix == ".h"], "generated, read-only")


def _next_requirement_id(trace: Trace) -> str:
    numbers = [int(r.id.split("-")[1]) for r in trace.requirements]
    return f"REQ-{max(numbers, default=0) + 1:03d}"


def _add_requirement_with_links(ai, trace, context, requirement_id: str) -> None:
    requirement = _requirement(trace, requirement_id)
    context.add(f"requirement {requirement_id}", _requirement_yaml(requirement))
    context.add_file(requirement.source, f"defines {requirement_id}")
    context.add_files(_implementation_files(ai, requirement), "implements it")
    context.add_files(sorted({t.path for t in trace.tests_for(requirement_id)}), "verifies it")


# Stage 1: requirements


def build_requirements(ai, trace, context, idea, options) -> str:
    context.add_files(list(ai.project.requirement_files), "requirements")
    if ai.project.model:
        context.add_file(ai.project.model, "model")
    return (
        f"Write or amend requirements for this idea:\n\n{idea}\n\n"
        f"Use new IDs starting at {_next_requirement_id(trace)}. Add each requirement to the "
        "file whose component matches. State behaviour and limits precisely, name config "
        "parameters, and give a rationale when the reason is not obvious. Leave "
        "implementation empty for requirements that are not implemented yet. If the idea "
        "needs new config parameters, do not invent them in the requirement; list them in "
        "the rationale so the model task can add them."
    )


# Stage 2: model


def build_model(ai, trace, context, requirement_id, options) -> str:
    _add_requirement_with_links(ai, trace, context, requirement_id)
    context.add_file(ai.project.model, "model, the file to change")
    _add_protected(ai, context)
    return (
        f"Change the model so that {requirement_id} can be implemented: faults, config "
        "parameters with defaults and ranges, struct fields or states it needs. Cite the "
        "requirement on new faults. Do not change existing names unless the requirement "
        "demands it; generated code is rebuilt with `sparrow gen`."
    )


# Stage 4: code


def build_code(ai, trace, context, requirement_id, options) -> str:
    _add_requirement_with_links(ai, trace, context, requirement_id)
    context.add_files(ai.files("code"), "controller")
    _add_generated_headers(ai, context)
    _add_protected(ai, context)
    return (
        f"Implement {requirement_id} in the controller. Keep the controller deterministic: "
        "no clock, no allocation, no global state, time only from dt_ms. Update the "
        "requirement's implementation list with path::symbol references to what you add. "
        f"Implementation paths are relative to {ai.relative(ai.project.root) or '.'}/, the "
        "directory of the project descriptor, not to the repository root."
    )


# Stage 5: tests


def build_tests(ai, trace, context, requirement_id, options) -> str:
    _add_requirement_with_links(ai, trace, context, requirement_id)
    context.add_files(
        [p for p in ai.files("tests") if p.name.endswith("_fixture.h") or p.name == "conftest.py"],
        "test fixtures",
    )
    context.add_files([p for p in ai.files("tests") if p.name == "CMakeLists.txt"], "test build")
    context.add_files(_fixture_imports(ai), "imported by conftest.py, the closed-loop test API")
    _add_generated_headers(ai, context)
    generated_python = [p for p in ai.protected_paths() if p.suffix == ".py"]
    context.add_files(generated_python, "generated, read-only")
    harness = ai.repo / "sparrow" / "testing" / "sp_test.h"
    context.add_file(harness, "C test harness")
    failing = _failing_tests_for(ai, trace, requirement_id)
    repair = ""
    if failing:
        context.add("failing tests", "\n\n".join(f"{key}\n{message}" for key, message in failing))
        repair = (
            f"Some existing tests that cite {requirement_id} fail in the last results (listed "
            "under 'failing tests'). Where a test is wrong for the current requirements, "
            "correct it in place instead of adding a replacement. Do not change a test to pass "
            "if the failure shows a controller defect; say so in the rationale. "
        )
    return repair + (
        f"Write tests that verify {requirement_id}. Test each limit it names just below, at "
        "and just above the limit, and the failure behaviour, not only the normal case: the "
        "condition must be shown to raise its alarm as well as not to. Add C unit tests for "
        "controller logic and pytest closed-loop tests where the plant's dynamics matter. "
        "Every new test must cite the requirement. Test through the public interface, as the "
        "existing tests do (the fixture and the controller's step function); never #include a "
        ".c file or call a "
        "static function. Register every new C test file in the test CMakeLists.txt with "
        "sparrow_add_c_test. Do not duplicate existing tests shown in the context."
    )


def _fixture_imports(ai: AIProject) -> list[Path]:
    """The project's own Python modules that the pytest fixtures import (bench, plant)."""
    found: set[Path] = set()
    for conftest in (p for p in ai.files("tests") if p.name == "conftest.py"):
        for node in ast.walk(ast.parse(conftest.read_text("utf-8"))):
            if isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                module = ai.project.root / (node.module.replace(".", "/") + ".py")
                if module.is_file():
                    found.add(module.resolve())
    return sorted(found)


def _failing_tests_for(ai, trace, requirement_id: str) -> list[tuple[str, str]]:
    """Failures in the last JUnit results of tests that cite the requirement.

    A result older than its test file is stale (the test changed after it ran)
    and is left out, so the model is not asked to repair a test that is fixed.
    """
    citing = {t.key: t.path for t in trace.tests_for(requirement_id)}
    failures = []
    for results in sorted(ai.repo.glob(RESULTS_GLOB)):
        ran_at = results.stat().st_mtime
        for name, message in _failed_cases([results]):
            classname, _, case = name.partition("::")
            key = f"{classname.rsplit('.', 1)[-1]}::{case.split('[')[0]}"
            if key in citing and citing[key].stat().st_mtime <= ran_at:
                failures.append((key, message))
    return failures


# HMI and documentation


def build_hmi(ai, trace, context, request, options) -> str:
    context.add_files(ai.files("hmi"), "HMI")
    _add_protected(ai, context)
    return (
        f"Make this HMI change:\n\n{request}\n\n"
        "Reusable widgets belong in the framework widget library, boiler-specific layout in "
        "the application. Widgets take their values from the view model and hold no rules "
        "about limits or states. Use the theme colors and fonts; do not introduce new ones "
        "unless the change needs them."
    )


def build_docs(ai, trace, context, request, options) -> str:
    context.add_files(ai.files("docs"), "documentation")
    context.add(
        "requirements",
        "\n".join(f"{r.id} {r.title}" for r in trace.requirements),
    )
    return (
        f"Make this documentation change:\n\n{request}\n\n"
        "Describe only what exists in the repository. Write short, specific sentences in "
        "the voice of the existing documents. No marketing language, no filler."
    )


# Review and analysis


def _git_diff(repo: Path, base: str) -> str:
    result = subprocess.run(
        ["git", "diff", base, "--"], cwd=repo, capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        raise TaskError(f"git diff {base} failed: {result.stderr.strip()}")
    return result.stdout


def build_review(ai, trace, context, base, options) -> str:
    diff = _git_diff(ai.repo, base or "HEAD")
    if not diff.strip():
        raise TaskError(f"no changes against {base or 'HEAD'}")
    context.add(f"diff against {base or 'HEAD'}", diff)
    touched = sorted(set(re.findall(r"^\+\+\+ b/(.+)$", diff, flags=re.MULTILINE)))
    for name in touched:
        for requirement in trace.requirements_for_file(ai.repo / name):
            context.add(f"requirement {requirement.id}", _requirement_yaml(requirement))
    return (
        "Review this change as a senior embedded engineer. Look for incorrect behaviour, "
        "missing bounds or fault handling, broken determinism, requirements the change "
        "affects without updating their tests, and untested boundaries. Report findings "
        "with file:line locations. Do not comment on style that the formatter enforces."
    )


def _failed_cases(paths: list[Path]) -> list[tuple[str, str]]:
    import xml.etree.ElementTree as ET

    failures = []
    for path in paths:
        for case in ET.parse(path).getroot().iter("testcase"):
            node = case.find("failure")
            if node is None:
                node = case.find("error")
            if node is not None:
                message = (node.get("message", "") + "\n" + (node.text or "")).strip()
                failures.append((f"{case.get('classname')}::{case.get('name')}", message))
    return failures


def build_explain(ai, trace, context, results_glob, options) -> str:
    paths = sorted(ai.repo.glob(results_glob or RESULTS_GLOB))
    if not paths:
        raise TaskError(f"no JUnit results match {results_glob}; run `make test` first")
    failures = _failed_cases(paths)
    if not failures:
        raise TaskError("all tests passed; nothing to explain")
    outcome = load_results(paths)
    context.add("failures", "\n\n".join(f"{name}\n{message}" for name, message in failures))
    failed_keys = {k for k, v in outcome.items() if v == "fail"}
    for test in trace.tests:
        if test.key in failed_keys:
            context.add_file(test.path, "failing test")
            for requirement_id in test.requirements:
                _add_requirement_with_links(ai, trace, context, requirement_id)
    _add_generated_headers(ai, context)
    return (
        "Explain why these tests fail. For each failure say whether the test, the "
        "implementation or the requirement is wrong, citing the lines that show it, and "
        "what change would fix it. Do not guess beyond what the context shows; say what "
        "additional information would settle an open question."
    )


def build_analyze(ai, trace, context, csv_path, options) -> str:
    path = Path(csv_path)
    if not path.is_file():
        raise TaskError(f"{csv_path}: not found; record one with the simulator's --csv option")
    model = yaml.safe_load(ai.project.model.read_text("utf-8"))
    states = [s["name"] for s in model["states"]]
    faults = [f["name"] for f in model["faults"]]
    context.add(f"trace summary {path.name}", summarize_trace(path, states, faults))
    if options.get("scenario"):
        context.add_file(Path(options["scenario"]), "scenario")
    for requirement_id in options.get("requirements") or []:
        _add_requirement_with_links(ai, trace, context, requirement_id)
    context.add(
        "requirements",
        "\n".join(f"{r.id} {r.title}: {r.statement}" for r in trace.requirements),
    )
    return (
        "Analyse this simulation run against the requirements. Check that every state "
        "change and alarm happened when the requirements say it should, flag behaviour "
        "that looks wrong or marginal (overshoot, oscillation, slow detection), and say "
        "which requirement each finding concerns."
    )


TASKS = {
    task.name: task
    for task in (
        Task(
            "requirements",
            "1 requirement",
            "proposal",
            ("requirements",),
            "idea",
            build_requirements,
        ),
        Task("model", "2 model", "proposal", ("model",), "requirement ID", build_model),
        Task("code", "4 code", "proposal", ("code", "requirements"), "requirement ID", build_code),
        Task("tests", "5 tests", "proposal", ("tests",), "requirement ID", build_tests),
        Task("hmi", "HMI", "proposal", ("hmi",), "change request", build_hmi),
        Task("docs", "docs", "proposal", ("docs",), "change request", build_docs),
        Task("review", "review", "report", (), "git base (default HEAD)", build_review),
        Task("explain", "5 tests", "report", (), "JUnit glob", build_explain),
        Task("analyze", "7 simulation", "report", (), "trace CSV", build_analyze),
    )
}


def prepare(
    ai: AIProject, task: Task, argument: str, options: dict, budget: int
) -> tuple[str, Context]:
    trace = build_trace(ai.project)
    context = Context(ai.repo, budget)
    instruction = task.build(ai, trace, context, argument, options)
    return instruction, context
