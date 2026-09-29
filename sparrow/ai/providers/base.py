"""Provider interface shared by all AI backends."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any


class ProviderError(Exception):
    """A request failed after the provider's own retries, or returned an unusable response."""


@dataclass(frozen=True)
class Capabilities:
    """What a configured backend supports. Adapters adapt requests to it rather than failing.

    structured_output:
      "json_schema"  the server constrains output to a JSON schema
      "json_object"  the server guarantees JSON, the schema is only given in the prompt
      "prompt"       no server support, JSON is requested in the prompt and parsed leniently
    """

    streaming: bool
    structured_output: str
    temperature: bool
    effort: bool = False


@dataclass(frozen=True)
class Message:
    role: str  # "user" or "assistant"
    content: str


@dataclass
class Completion:
    text: str
    model: str
    stop_reason: str
    input_tokens: int = 0
    output_tokens: int = 0
    notes: list[str] = field(default_factory=list)  # adaptations made for this backend


TextCallback = Callable[[str], None]


class Provider(ABC):
    name: str
    capabilities: Capabilities

    @abstractmethod
    def complete(
        self,
        system: str,
        messages: list[Message],
        schema: dict[str, Any] | None = None,
        on_text: TextCallback | None = None,
    ) -> Completion:
        """Runs one request. With schema, the reply text is JSON that should match it.

        on_text receives text as it streams, if the backend streams.
        """
