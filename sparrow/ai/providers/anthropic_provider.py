"""Anthropic Messages API adapter (official `anthropic` SDK)."""

from __future__ import annotations

from typing import Any

from ..config import AIConfig
from .base import Capabilities, Completion, Message, Provider, ProviderError, TextCallback

# Models that reject sampling parameters (temperature/top_p/top_k return a 400).
NO_SAMPLING_PREFIXES = (
    "claude-fable-5",
    "claude-mythos-5",
    "claude-opus-5",
    "claude-sonnet-5",
    "claude-opus-4-8",
    "claude-opus-4-7",
)
# Models that accept the scalar server-side refusal fallback ("default" routing).
FALLBACK_MODELS = ("claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5")
FALLBACK_BETA = "server-side-fallback-2026-07-01"


def _import_sdk():
    try:
        import anthropic
    except ImportError as error:
        raise ProviderError("the anthropic package is missing: pip install '.[ai]'") from error
    return anthropic


class AnthropicProvider(Provider):
    name = "anthropic"

    def __init__(self, config: AIConfig):
        self._sdk = _import_sdk()
        self._config = config
        options: dict[str, Any] = {"timeout": config.timeout_s, "max_retries": config.max_retries}
        if config.base_url:
            options["base_url"] = config.base_url
        key = config.resolve_api_key()
        if key:
            options["api_key"] = key
        # Without a key the SDK falls back to ANTHROPIC_AUTH_TOKEN or an `ant auth login` profile.
        self._client = self._sdk.Anthropic(**options)
        self.capabilities = Capabilities(
            streaming=True,
            structured_output="json_schema",
            temperature=not config.model.startswith(NO_SAMPLING_PREFIXES),
            effort=True,
        )

    def _params(self, system: str, messages: list[Message], schema, notes: list[str]) -> dict:
        config = self._config
        params: dict[str, Any] = {
            "model": config.model,
            "max_tokens": config.max_tokens,
            "system": system,
            "messages": [{"role": m.role, "content": m.content} for m in messages],
        }
        output_config: dict[str, Any] = {}
        if config.effort:
            output_config["effort"] = config.effort
        if schema is not None:
            output_config["format"] = {"type": "json_schema", "schema": schema}
        if output_config:
            params["output_config"] = output_config
        if config.temperature is not None:
            if self.capabilities.temperature:
                # SDK 1.x has no temperature keyword; older models still accept the field.
                params["extra_body"] = {"temperature": config.temperature}
            else:
                notes.append(f"temperature ignored: {config.model} does not accept it")
        return params

    def _use_fallback(self) -> bool:
        return self._config.refusal_fallback and self._config.model in FALLBACK_MODELS

    def _send(self, params: dict, on_text: TextCallback | None):
        messages = self._client.messages
        if self._use_fallback():
            messages = self._client.beta.messages
            params = {**params, "betas": [FALLBACK_BETA], "fallbacks": "default"}
        if not self._config.stream:
            return messages.create(**params)
        with messages.stream(**params) as stream:
            for text in stream.text_stream:
                if on_text is not None:
                    on_text(text)
            return stream.get_final_message()

    def complete(
        self,
        system: str,
        messages: list[Message],
        schema: dict[str, Any] | None = None,
        on_text: TextCallback | None = None,
    ) -> Completion:
        notes: list[str] = []
        sdk = self._sdk
        try:
            response = self._send(self._params(system, messages, schema, notes), on_text)
        except sdk.APIStatusError as error:
            raise ProviderError(f"anthropic: HTTP {error.status_code}: {error.message}") from error
        except sdk.APIConnectionError as error:
            raise ProviderError(f"anthropic: connection failed: {error}") from error
        if response.stop_reason == "refusal":
            details = getattr(response, "stop_details", None)
            category = getattr(details, "category", None) if details else None
            raise ProviderError(f"anthropic: request declined (category: {category})")
        text = "".join(b.text for b in response.content if getattr(b, "type", "") == "text")
        return Completion(
            text=text,
            model=response.model,
            stop_reason=response.stop_reason or "",
            input_tokens=response.usage.input_tokens,
            output_tokens=response.usage.output_tokens,
            notes=notes,
        )
