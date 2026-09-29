"""Assembles repository context for a request, within a fixed character budget.

The budget is a hard limit: when the selected files do not fit, the request
fails with the sizes involved instead of being silently truncated.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


class ContextTooLarge(Exception):
    pass


@dataclass
class Context:
    repo: Path
    budget: int
    sections: list[tuple[str, str]] = field(default_factory=list)
    _files: set[Path] = field(default_factory=set)

    @property
    def size(self) -> int:
        return sum(len(title) + len(body) + 16 for title, body in self.sections)

    def add(self, title: str, body: str) -> None:
        if self.size + len(body) > self.budget:
            raise ContextTooLarge(
                f"context would exceed {self.budget} characters at '{title}' "
                f"({self.size} + {len(body)}); narrow the request or raise max_context_chars"
            )
        self.sections.append((title, body))

    def add_file(self, path: Path, note: str = "") -> None:
        resolved = path.resolve()
        if resolved in self._files or not resolved.is_file():
            return
        self._files.add(resolved)
        relative = resolved.relative_to(self.repo.resolve()).as_posix()
        self.add(f"file {relative}{f' ({note})' if note else ''}", resolved.read_text("utf-8"))

    def add_files(self, paths: list[Path], note: str = "") -> None:
        for path in sorted(paths):
            self.add_file(path, note)

    def render(self) -> str:
        return "\n\n".join(f"=== {title} ===\n{body.rstrip()}" for title, body in self.sections)
