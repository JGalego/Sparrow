"""Reads JUnit XML results (pytest and the C test runner) into pass/fail per test."""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path

PARAMETERS = re.compile(r"\[.*\]$")


def _key(classname: str, name: str) -> str:
    return f"{classname.rsplit('.', 1)[-1]}::{PARAMETERS.sub('', name)}"


def load_results(paths: list[Path]) -> dict[str, str]:
    """Maps test key to "pass", "fail" or "skip". A parametrized test fails if any case fails."""
    outcome: dict[str, str] = {}
    for path in paths:
        for case in ET.parse(path).getroot().iter("testcase"):
            key = _key(case.get("classname", ""), case.get("name", ""))
            if case.find("failure") is not None or case.find("error") is not None:
                result = "fail"
            elif case.find("skipped") is not None:
                result = "skip"
            else:
                result = "pass"
            if outcome.get(key) != "fail":
                outcome[key] = result
    return outcome
