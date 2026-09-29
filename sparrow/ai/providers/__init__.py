"""Provider adapters. Only the selected adapter's SDK is imported."""

from __future__ import annotations

from ..config import AIConfig
from .base import Capabilities, Completion, Message, Provider, ProviderError


def create_provider(config: AIConfig) -> Provider:
    if config.provider == "anthropic":
        from .anthropic_provider import AnthropicProvider

        return AnthropicProvider(config)
    from .openai_provider import OpenAIProvider

    return OpenAIProvider(config)


__all__ = ["Capabilities", "Completion", "Message", "Provider", "ProviderError", "create_provider"]
