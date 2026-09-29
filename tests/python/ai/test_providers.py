"""Adapters against a local stub server, through the real anthropic and openai SDKs."""

import json

import pytest

pytest.importorskip("anthropic")
pytest.importorskip("openai")
from stub_server import (
    Reply,
    anthropic_message,
    anthropic_stream,
    openai_completion,
    openai_stream,
)

from sparrow.ai.config import AIConfig
from sparrow.ai.providers import Message, ProviderError, create_provider

SCHEMA = {
    "type": "object",
    "properties": {"answer": {"type": "string"}},
    "required": ["answer"],
    "additionalProperties": False,
}
USER = [Message("user", "hello")]


def anthropic_config(stub, **overrides):
    values = dict(
        provider="anthropic",
        model="claude-opus-5-5",
        base_url=stub.url,
        api_key="test-key",
        max_retries=0,
        timeout_s=5,
    )
    return AIConfig(**{**values, **overrides})


def openai_config(stub, provider="openai-compatible", **overrides):
    values = dict(
        provider=provider,
        model="test-model",
        base_url=stub.url + "/v1",
        api_key="test-key" if provider == "openai" else "",
        max_retries=0,
        timeout_s=5,
    )
    return AIConfig(**{**values, **overrides})


# Anthropic


def test_anthropic_request_carries_key_version_system_and_schema(stub):
    stub.replies.append(Reply(body=anthropic_message('{"answer": "42"}')))
    provider = create_provider(anthropic_config(stub, stream=False, refusal_fallback=False))

    completion = provider.complete("be brief", USER, SCHEMA)

    request = stub.requests[0]
    assert request.path == "/v1/messages"
    assert request.headers["x-api-key"] == "test-key"
    assert "anthropic-version" in request.headers
    assert request.body["system"] == "be brief"
    assert request.body["output_config"]["format"] == {"type": "json_schema", "schema": SCHEMA}
    assert json.loads(completion.text) == {"answer": "42"}
    assert (completion.input_tokens, completion.output_tokens) == (11, 7)


def test_anthropic_streams_text_to_the_callback(stub):
    stub.replies.append(Reply(sse=anthropic_stream(["Hel", "lo"])))
    provider = create_provider(anthropic_config(stub, refusal_fallback=False))
    seen = []

    completion = provider.complete("s", USER, on_text=seen.append)

    assert seen == ["Hel", "lo"]
    assert completion.text == "Hello"
    assert stub.requests[0].body["stream"] is True


def test_anthropic_enables_the_server_side_refusal_fallback_on_current_models(stub):
    stub.replies.append(Reply(body=anthropic_message("ok")))
    provider = create_provider(anthropic_config(stub, stream=False))

    provider.complete("s", USER)

    request = stub.requests[0]
    assert request.body["fallbacks"] == "default"
    assert "server-side-fallback-2026-07-01" in request.headers["anthropic-beta"]


def test_anthropic_drops_temperature_for_models_that_reject_it(stub):
    stub.replies.append(Reply(body=anthropic_message("ok")))
    provider = create_provider(anthropic_config(stub, stream=False, temperature=0.2))

    completion = provider.complete("s", USER)

    assert "temperature" not in stub.requests[0].body
    assert any("temperature ignored" in note for note in completion.notes)


def test_anthropic_passes_temperature_to_models_that_accept_it(stub):
    stub.replies.append(Reply(body=anthropic_message("ok", model="claude-haiku-4-5")))
    config = anthropic_config(stub, model="claude-haiku-4-5", stream=False, temperature=0.2)

    create_provider(config).complete("s", USER)

    assert stub.requests[0].body["temperature"] == 0.2
    assert "fallbacks" not in stub.requests[0].body


def test_anthropic_sends_effort_in_output_config(stub):
    stub.replies.append(Reply(body=anthropic_message("ok")))

    create_provider(anthropic_config(stub, stream=False, effort="high")).complete("s", USER)

    assert stub.requests[0].body["output_config"]["effort"] == "high"


def test_anthropic_refusal_is_an_error(stub):
    stub.replies.append(Reply(body=anthropic_message("", stop_reason="refusal")))
    provider = create_provider(anthropic_config(stub, stream=False, refusal_fallback=False))

    with pytest.raises(ProviderError, match="declined"):
        provider.complete("s", USER)


def test_anthropic_retries_a_rate_limited_request(stub):
    stub.replies += [
        Reply(
            429,
            {"type": "error", "error": {"type": "rate_limit_error", "message": "slow"}},
            headers={"retry-after-ms": "10"},
        ),
        Reply(body=anthropic_message("ok")),
    ]
    provider = create_provider(anthropic_config(stub, stream=False, max_retries=1))

    assert provider.complete("s", USER).text == "ok"
    assert len(stub.requests) == 2


def test_anthropic_client_error_is_reported_without_retry(stub):
    stub.replies.append(
        Reply(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "bad"}})
    )
    provider = create_provider(anthropic_config(stub, stream=False, max_retries=3))

    with pytest.raises(ProviderError, match="HTTP 400"):
        provider.complete("s", USER)
    assert len(stub.requests) == 1


# OpenAI and OpenAI-compatible


def test_openai_uses_strict_json_schema_and_max_completion_tokens(stub):
    stub.replies.append(Reply(body=openai_completion('{"answer": "42"}')))
    provider = create_provider(openai_config(stub, provider="openai", stream=False))

    completion = provider.complete("be brief", USER, SCHEMA)

    request = stub.requests[0]
    assert request.path == "/v1/chat/completions"
    assert request.headers["authorization"] == "Bearer test-key"
    assert request.body["messages"][0] == {"role": "system", "content": "be brief"}
    assert request.body["response_format"]["type"] == "json_schema"
    assert request.body["response_format"]["json_schema"]["strict"] is True
    assert request.body["max_completion_tokens"] == 16000
    assert json.loads(completion.text) == {"answer": "42"}


def test_openai_requires_a_key():
    config = AIConfig(provider="openai", model="m", api_key_env="OPENAI_API_KEY")

    with pytest.raises(ProviderError, match="API key"):
        create_provider(config)


def test_compatible_server_defaults_to_json_object_with_the_schema_in_the_prompt(stub):
    stub.replies.append(Reply(body=openai_completion('{"answer": "x"}')))
    provider = create_provider(openai_config(stub, stream=False))

    provider.complete("be brief", USER, SCHEMA)

    request = stub.requests[0]
    assert request.body["response_format"] == {"type": "json_object"}
    assert '"answer"' in request.body["messages"][0]["content"]
    assert request.body["max_tokens"] == 16000
    assert request.headers["authorization"] == "Bearer not-needed"


def test_compatible_server_can_use_json_schema_when_configured(stub):
    stub.replies.append(Reply(body=openai_completion('{"answer": "x"}')))
    provider = create_provider(openai_config(stub, stream=False, structured_output="json_schema"))

    provider.complete("s", USER, SCHEMA)

    assert stub.requests[0].body["response_format"]["type"] == "json_schema"


def test_prompt_mode_sends_no_response_format(stub):
    stub.replies.append(Reply(body=openai_completion('{"answer": "x"}')))
    provider = create_provider(openai_config(stub, stream=False, structured_output="prompt"))

    provider.complete("s", USER, SCHEMA)

    assert "response_format" not in stub.requests[0].body


def test_compatible_server_ignores_effort_with_a_note(stub):
    stub.replies.append(Reply(body=openai_completion("ok")))
    provider = create_provider(openai_config(stub, stream=False, effort="high"))

    completion = provider.complete("s", USER)

    assert "reasoning_effort" not in stub.requests[0].body
    assert completion.notes


def test_openai_stream_is_assembled(stub):
    stub.replies.append(Reply(sse=openai_stream(['{"ans', 'wer": 1}'])))
    seen = []

    completion = create_provider(openai_config(stub)).complete("s", USER, on_text=seen.append)

    assert completion.text == '{"answer": 1}'
    assert seen == ['{"ans', 'wer": 1}']
    assert completion.stop_reason == "stop"


def test_unreachable_server_is_a_provider_error():
    config = AIConfig(
        provider="openai-compatible",
        model="m",
        base_url="http://127.0.0.1:9/v1",
        max_retries=0,
        timeout_s=2,
    )

    with pytest.raises(ProviderError, match="connection"):
        create_provider(config).complete("s", USER)


def test_suspiciously_small_prompt_count_is_flagged(stub):
    body = openai_completion("ok")
    body["usage"]["prompt_tokens"] = 10
    stub.replies.append(Reply(body=body))
    provider = create_provider(openai_config(stub, stream=False))

    completion = provider.complete("s" * 4000, USER)

    assert any("truncated" in note for note in completion.notes)
