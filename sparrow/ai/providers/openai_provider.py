"""OpenAI Chat Completions adapter (official `openai` SDK).

Serves both OpenAI itself and OpenAI-compatible servers (Ollama, Groq, vLLM,
llama.cpp, ...) through base_url. Compatible servers differ in what they
accept, so their capabilities come from the configuration rather than being
assumed.
"""

from __future__ import annotations

import json
from typing import Any

from ..config import AIConfig
from .base import Capabilities, Completion, Message, Provider, ProviderError, TextCallback

SCHEMA_NAME = "sparrow_output"
PLACEHOLDER_KEY = "not-needed"  # local servers ignore the key, the SDK requires one
# Fewer prompt tokens than one per this many characters means the server dropped input.
TRUNCATION_CHARS_PER_TOKEN = 8


def _import_sdk():
    try:
        import openai
    except ImportError as error:
        raise ProviderError("the openai package is missing: pip install '.[ai]'") from error
    return openai


def _structured_mode(config: AIConfig) -> str:
    if config.structured_output != "auto":
        return config.structured_output
    return "json_schema" if config.provider == "openai" else "json_object"


def schema_instructions(schema: dict[str, Any]) -> str:
    return (
        "Reply with a single JSON object and nothing else. It must validate against "
        f"this JSON schema:\n{json.dumps(schema, indent=2)}"
    )


class OpenAIProvider(Provider):
    def __init__(self, config: AIConfig):
        self._sdk = _import_sdk()
        self._config = config
        self.name = config.provider
        key = config.resolve_api_key()
        if not key and config.provider == "openai":
            raise ProviderError(f"openai: API key not found in ${config.api_key_env}")
        options: dict[str, Any] = {
            "api_key": key or PLACEHOLDER_KEY,
            "timeout": config.timeout_s,
            "max_retries": config.max_retries,
        }
        if config.base_url:
            options["base_url"] = config.base_url
        self._client = self._sdk.OpenAI(**options)
        self.capabilities = Capabilities(
            streaming=True,
            structured_output=_structured_mode(config),
            temperature=True,
            effort=config.provider == "openai",
        )

    def _params(self, system: str, messages: list[Message], schema, notes: list[str]) -> dict:
        config = self._config
        mode = self.capabilities.structured_output
        if schema is not None and mode != "json_schema":
            system = f"{system}\n\n{schema_instructions(schema)}"
        params: dict[str, Any] = {
            "model": config.model,
            "messages": [{"role": "system", "content": system}]
            + [{"role": m.role, "content": m.content} for m in messages],
        }
        # OpenAI renamed max_tokens; compatible servers commonly accept only the old name.
        token_field = "max_completion_tokens" if config.provider == "openai" else "max_tokens"
        params[token_field] = config.max_tokens
        if schema is not None and mode == "json_schema":
            params["response_format"] = {
                "type": "json_schema",
                "json_schema": {"name": SCHEMA_NAME, "schema": schema, "strict": True},
            }
        elif schema is not None and mode == "json_object":
            params["response_format"] = {"type": "json_object"}
        if config.temperature is not None:
            params["temperature"] = config.temperature
        if config.effort:
            if self.capabilities.effort:
                params["reasoning_effort"] = config.effort
            else:
                notes.append("effort ignored: not supported by openai-compatible servers")
        return params

    def _stream(self, params: dict, on_text: TextCallback | None) -> tuple[str, str, str, Any]:
        parts: list[str] = []
        finish = ""
        model = self._config.model
        usage = None
        extra = {"stream_options": {"include_usage": True}} if self.name == "openai" else {}
        for chunk in self._client.chat.completions.create(**params, stream=True, **extra):
            model = chunk.model or model
            usage = chunk.usage or usage
            if not chunk.choices:
                continue
            choice = chunk.choices[0]
            finish = choice.finish_reason or finish
            text = choice.delta.content or ""
            if text:
                parts.append(text)
                if on_text is not None:
                    on_text(text)
        return "".join(parts), finish, model, usage

    def complete(
        self,
        system: str,
        messages: list[Message],
        schema: dict[str, Any] | None = None,
        on_text: TextCallback | None = None,
    ) -> Completion:
        notes: list[str] = []
        sdk = self._sdk
        params = self._params(system, messages, schema, notes)
        try:
            if self._config.stream:
                text, finish, model, usage = self._stream(params, on_text)
            else:
                response = self._client.chat.completions.create(**params)
                choice = response.choices[0]
                text, finish = choice.message.content or "", choice.finish_reason or ""
                model, usage = response.model, response.usage
        except sdk.APIStatusError as error:
            raise ProviderError(
                f"{self.name}: HTTP {error.status_code}: {error.message}"
            ) from error
        except sdk.APIConnectionError as error:
            raise ProviderError(f"{self.name}: connection failed: {error}") from error
        input_tokens = getattr(usage, "prompt_tokens", 0) or 0
        sent = sum(len(m["content"]) for m in params["messages"])
        if input_tokens and input_tokens < sent / TRUNCATION_CHARS_PER_TOKEN:
            notes.append(
                f"the server counted {input_tokens} prompt tokens for {sent} characters; it "
                "probably truncated the request to its context length"
            )
        return Completion(
            text=text,
            model=model,
            stop_reason=finish,
            input_tokens=input_tokens,
            output_tokens=getattr(usage, "completion_tokens", 0) or 0,
            notes=notes,
        )
