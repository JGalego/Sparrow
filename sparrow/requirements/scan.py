"""Finds the tests that declare which requirements they verify."""

from __future__ import annotations

import ast
import re
from dataclasses import dataclass
from pathlib import Path

C_TEST = re.compile(r'SP_TEST\(\s*(\w+)\s*,\s*"([^"]*)"\s*\)')
REQUIREMENT_ID = re.compile(r"REQ-\d{3}")


@dataclass(frozen=True)
class TestRef:
    key: str  # "<file stem>::<test name>", the same key the JUnit reader produces
    path: Path
    line: int
    requirements: tuple[str, ...]


def _ids(text: str) -> tuple[str, ...]:
    return tuple(REQUIREMENT_ID.findall(text))


def scan_c_file(path: Path) -> list[TestRef]:
    refs = []
    text = path.read_text(encoding="utf-8")
    for match in C_TEST.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        refs.append(TestRef(f"{path.stem}::{match.group(1)}", path, line, _ids(match.group(2))))
    return refs


def _verifies_arguments(function: ast.FunctionDef) -> list[str]:
    found: list[str] = []
    for decorator in function.decorator_list:
        call = decorator if isinstance(decorator, ast.Call) else None
        if call is None or not isinstance(call.func, ast.Attribute) or call.func.attr != "verifies":
            continue
        found += [
            a.value for a in call.args if isinstance(a, ast.Constant) and isinstance(a.value, str)
        ]
    return found


def scan_python_file(path: Path) -> list[TestRef]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    refs = []
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"):
            requirements = _ids(" ".join(_verifies_arguments(node)))
            refs.append(TestRef(f"{path.stem}::{node.name}", path, node.lineno, requirements))
    return refs


def scan_tests(directories: tuple[Path, ...]) -> list[TestRef]:
    refs: list[TestRef] = []
    for directory in directories:
        for path in sorted(directory.rglob("test_*.c")):
            refs += scan_c_file(path)
        for path in sorted(directory.rglob("test_*.py")):
            refs += scan_python_file(path)
    return refs
