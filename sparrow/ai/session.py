"""Runs one task: context, provider, validated proposal or report, optional apply and gates."""

from __future__ import annotations

import json
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .config import AIConfig
from .gates import GateResult, run_gates
from .project import AIProject
from .prompts import SYSTEM
from .proposal import (
    PROPOSAL_SCHEMA,
    Policy,
    Proposal,
    ProposalError,
    apply,
    parse_json_reply,
    parse_proposal,
    render_diff,
    resolve,
)
from .providers import Message, Provider
from .report import REPORT_SCHEMA, parse_report, render_report
from .tasks import Task, prepare

OUTPUT_DIR = ".sparrow"


@dataclass
class Outcome:
    task: str
    text: str  # rendered diff or report
    saved: list[Path] = field(default_factory=list)
    proposal: Proposal | None = None
    gates: list[GateResult] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    applied: bool = False
    usage: list[tuple[str, int, int]] = field(default_factory=list)  # (model, in, out) per request

    @property
    def gates_passed(self) -> bool:
        return all(g.passed for g in self.gates)


def build_request(ai: AIProject, task: Task, argument: str, options: dict, config: AIConfig):
    instruction, context = prepare(ai, task, argument, options, config.max_context_chars)
    if task.kind == "proposal":
        allowed = sorted({g for key in task.paths for g in ai.globs(key)})
        instruction += "\n\nYou may change only files matching: " + ", ".join(allowed)
    user = f"{context.render()}\n\n=== task ===\n{instruction}"
    return user, context


def _policy(ai: AIProject, task: Task) -> Policy:
    return Policy(tuple(g for key in task.paths for g in ai.globs(key)), ai.protected)


def _save(ai: AIProject, task: Task, suffix_text: dict[str, str]) -> list[Path]:
    directory = ai.repo / OUTPUT_DIR / ("proposals" if task.kind == "proposal" else "reports")
    directory.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    saved = []
    for suffix, text in suffix_text.items():
        path = directory / f"{stamp}-{task.name}{suffix}"
        path.write_text(text, encoding="utf-8")
        saved.append(path)
    return saved


def _progress(text: str) -> None:
    sys.stderr.write(".")
    sys.stderr.flush()


def run_task(
    ai: AIProject,
    task: Task,
    argument: str,
    options: dict,
    config: AIConfig,
    provider: Provider,
    apply_changes: bool = False,
    verify: bool = False,
    attempts: int = 2,
) -> Outcome:
    user, _ = build_request(ai, task, argument, options, config)
    schema = PROPOSAL_SCHEMA if task.kind == "proposal" else REPORT_SCHEMA
    messages = [Message("user", user)]
    notes: list[str] = []
    usage: list[tuple[str, int, int]] = []
    for attempt in range(1, attempts + 1):
        completion = provider.complete(SYSTEM, messages, schema, on_text=_progress)
        sys.stderr.write("\n")
        notes += completion.notes
        usage.append((completion.model, completion.input_tokens, completion.output_tokens))
        try:
            if task.kind == "report":
                report = parse_report(completion.text)
                text = render_report(report)
                saved = _save(ai, task, {".md": text})
                return Outcome(task.name, text, saved, notes=notes, usage=usage)
            proposal = parse_proposal(completion.text)
            changes = resolve(proposal, ai.repo, _policy(ai, task))
            break
        except ProposalError as error:
            if attempt == attempts:
                raise
            notes.append(f"attempt {attempt} rejected: {error}")
            messages += [
                Message("assistant", completion.text),
                Message(
                    "user",
                    f"The proposal was rejected: {error}\nReply with the complete corrected "
                    "proposal. Copy search text verbatim from the files in the context.",
                ),
            ]
    diff = render_diff(changes, ai.repo)
    header = f"# {proposal.summary}\n#\n# {proposal.rationale}\n"
    saved = _save(
        ai, task, {".patch": diff, ".json": json.dumps(parse_json_reply(completion.text), indent=2)}
    )
    outcome = Outcome(task.name, header + diff, saved, proposal, notes=notes, usage=usage)
    if apply_changes:
        apply(changes)
        outcome.applied = True
        if verify:
            outcome.gates = run_gates(ai.gates, ai.repo)
    return outcome
