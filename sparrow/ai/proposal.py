"""AI change proposals: a JSON list of file edits, validated and rendered as a unified diff.

A proposal never touches the working tree until it has been validated in
full. Edits are exact search/replace pairs, so a stale or ambiguous proposal
is rejected instead of being applied in the wrong place.
"""

from __future__ import annotations

import difflib
import fnmatch
import json
import re
from dataclasses import dataclass
from pathlib import Path

EDIT_SCHEMA = {
    "type": "object",
    "properties": {
        "path": {"type": "string"},
        "action": {"type": "string", "enum": ["replace", "create"]},
        "search": {"type": "string"},
        "replace": {"type": "string"},
        "content": {"type": "string"},
    },
    "required": ["path", "action", "search", "replace", "content"],
    "additionalProperties": False,
}

PROPOSAL_SCHEMA = {
    "type": "object",
    "properties": {
        "summary": {"type": "string"},
        "rationale": {"type": "string"},
        "edits": {"type": "array", "items": EDIT_SCHEMA},
    },
    "required": ["summary", "rationale", "edits"],
    "additionalProperties": False,
}

FENCE = re.compile(r"^```[a-zA-Z]*\s*|\s*```$")


class ProposalError(Exception):
    pass


@dataclass(frozen=True)
class Edit:
    path: str  # relative to the repository root
    action: str
    search: str = ""
    replace: str = ""
    content: str = ""


@dataclass(frozen=True)
class Proposal:
    summary: str
    rationale: str
    edits: tuple[Edit, ...]


def parse_json_reply(text: str) -> dict:
    """Parses a JSON reply, tolerating a code fence or prose around the object."""
    stripped = FENCE.sub("", text.strip())
    try:
        return json.loads(stripped)
    except json.JSONDecodeError:
        start, end = stripped.find("{"), stripped.rfind("}")
        if start < 0 or end <= start:
            raise ProposalError("reply contains no JSON object") from None
        try:
            return json.loads(stripped[start : end + 1])
        except json.JSONDecodeError as error:
            raise ProposalError(f"reply is not valid JSON: {error}") from None


def parse_proposal(text: str) -> Proposal:
    raw = parse_json_reply(text)
    try:
        edits = tuple(
            Edit(
                path=e["path"],
                action=e["action"],
                search=e.get("search", ""),
                replace=e.get("replace", ""),
                content=e.get("content", ""),
            )
            for e in raw["edits"]
        )
        return Proposal(raw["summary"], raw.get("rationale", ""), edits)
    except (KeyError, TypeError) as error:
        raise ProposalError(f"reply does not match the proposal schema: {error}") from None


@dataclass(frozen=True)
class Policy:
    """Where a task may write. Globs are relative to the repository root."""

    allowed: tuple[str, ...]
    protected: tuple[str, ...]  # generated files; change their source instead


def _check_path(edit: Edit, repo: Path, policy: Policy) -> Path:
    target = (repo / edit.path).resolve()
    if repo.resolve() not in target.parents:
        raise ProposalError(f"{edit.path}: outside the repository")
    relative = target.relative_to(repo.resolve()).as_posix()
    if relative in policy.protected:
        raise ProposalError(f"{edit.path}: generated file, change its source instead")
    if not any(fnmatch.fnmatch(relative, pattern) for pattern in policy.allowed):
        raise ProposalError(f"{edit.path}: this task may only change {list(policy.allowed)}")
    return target


def resolve(proposal: Proposal, repo: Path, policy: Policy) -> dict[Path, tuple[str, str]]:
    """Validates every edit and returns {path: (old text, new text)}.

    Edits to the same file apply in order. Raises ProposalError on the first
    problem; nothing is written.
    """
    if not proposal.edits:
        raise ProposalError("the proposal contains no edits")
    files: dict[Path, tuple[str, str]] = {}
    for edit in proposal.edits:
        target = _check_path(edit, repo, policy)
        exists = target in files or target.is_file()
        old = files[target][0] if target in files else (target.read_text("utf-8") if exists else "")
        current = files[target][1] if target in files else old
        if edit.action == "create":
            if exists:
                raise ProposalError(f"{edit.path}: create, but the file exists")
            files[target] = ("", edit.content)
            continue
        if edit.action != "replace":
            raise ProposalError(f"{edit.path}: unknown action '{edit.action}'")
        if not exists:
            raise ProposalError(f"{edit.path}: replace, but the file does not exist")
        count = current.count(edit.search) if edit.search else 0
        if count != 1:
            raise ProposalError(
                f"{edit.path}: search text must occur exactly once, found {count} times"
            )
        files[target] = (old, current.replace(edit.search, edit.replace, 1))
    return files


def render_diff(changes: dict[Path, tuple[str, str]], repo: Path) -> str:
    chunks = []
    for path, (old, new) in changes.items():
        name = path.relative_to(repo.resolve()).as_posix()
        chunks += difflib.unified_diff(
            old.splitlines(keepends=True),
            new.splitlines(keepends=True),
            fromfile="/dev/null" if not old and not path.exists() else f"a/{name}",
            tofile=f"b/{name}",
        )
    return "".join(chunks)


def apply(changes: dict[Path, tuple[str, str]]) -> None:
    for path, (_, new) in changes.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(new, encoding="utf-8")
