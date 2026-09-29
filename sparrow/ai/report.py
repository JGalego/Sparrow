"""Structured review and analysis reports."""

from __future__ import annotations

from dataclasses import dataclass

from .proposal import ProposalError, parse_json_reply

REPORT_SCHEMA = {
    "type": "object",
    "properties": {
        "summary": {"type": "string"},
        "findings": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "severity": {"type": "string", "enum": ["error", "warning", "info"]},
                    "location": {"type": "string"},
                    "requirement": {"type": "string"},
                    "text": {"type": "string"},
                },
                "required": ["severity", "location", "requirement", "text"],
                "additionalProperties": False,
            },
        },
        "next_steps": {"type": "array", "items": {"type": "string"}},
    },
    "required": ["summary", "findings", "next_steps"],
    "additionalProperties": False,
}

ORDER = {"error": 0, "warning": 1, "info": 2}


@dataclass(frozen=True)
class Finding:
    severity: str
    location: str
    requirement: str
    text: str


@dataclass(frozen=True)
class Report:
    summary: str
    findings: tuple[Finding, ...]
    next_steps: tuple[str, ...]


def parse_report(text: str) -> Report:
    raw = parse_json_reply(text)
    try:
        findings = tuple(
            Finding(
                f.get("severity", "info"),
                f.get("location", ""),
                f.get("requirement", ""),
                f["text"],
            )
            for f in raw.get("findings", [])
        )
        return Report(raw["summary"], findings, tuple(raw.get("next_steps", [])))
    except (KeyError, TypeError) as error:
        raise ProposalError(f"reply does not match the report schema: {error}") from None


def render_report(report: Report) -> str:
    lines = [report.summary, ""]
    for finding in sorted(report.findings, key=lambda f: ORDER.get(f.severity, 3)):
        where = f" {finding.location}" if finding.location else ""
        requirement = f" [{finding.requirement}]" if finding.requirement else ""
        lines.append(f"{finding.severity.upper():7}{where}{requirement}: {finding.text}")
    if report.next_steps:
        lines += ["", "Next steps:"] + [f"  - {step}" for step in report.next_steps]
    return "\n".join(lines).rstrip() + "\n"
