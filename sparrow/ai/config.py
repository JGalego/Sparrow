"""AI provider configuration: profiles in a local YAML file, overridden by environment variables.

Credentials are read from the environment variable named by `api_key_env`.
A profile may also carry `api_key` directly, but only in a file that Git
ignores; loading refuses a key from a tracked file.
"""

from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass, fields, replace
from pathlib import Path

import yaml

PROVIDERS = ("anthropic", "openai", "openai-compatible")
STRUCTURED_MODES = ("auto", "json_schema", "json_object", "prompt")
LOCAL_FILE = "sparrow-ai.local.yaml"
USER_FILE = Path("~/.config/sparrow/ai.yaml")

DEFAULTS = {
    "anthropic": {"model": "claude-opus-5-5", "api_key_env": "ANTHROPIC_API_KEY"},
    "openai": {"model": "", "api_key_env": "OPENAI_API_KEY"},
    "openai-compatible": {"model": "", "api_key_env": ""},
}


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class AIConfig:
    provider: str
    model: str
    base_url: str = ""
    api_key_env: str = ""
    api_key: str = ""
    max_tokens: int = 16000
    temperature: float | None = None
    effort: str = ""  # Anthropic output_config.effort: low, medium, high, xhigh, max
    timeout_s: float = 300.0
    max_retries: int = 2
    stream: bool = True
    structured_output: str = "auto"
    refusal_fallback: bool = True  # Anthropic server-side fallback on a policy decline
    max_context_chars: int = 400_000
    profile: str = ""

    def resolve_api_key(self) -> str:
        if self.api_key:
            return self.api_key
        if self.api_key_env:
            return os.environ.get(self.api_key_env, "")
        return ""

    def describe(self) -> dict[str, object]:
        """The configuration with the credential replaced by where it comes from."""
        shown = {f.name: getattr(self, f.name) for f in fields(self) if f.name != "api_key"}
        if self.api_key:
            shown["api_key"] = "(set in config file)"
        elif self.api_key_env:
            state = "set" if os.environ.get(self.api_key_env) else "NOT SET"
            shown["api_key"] = f"${self.api_key_env} ({state})"
        return shown


ENV_OVERRIDES = {
    "SPARROW_AI_PROVIDER": ("provider", str),
    "SPARROW_AI_MODEL": ("model", str),
    "SPARROW_AI_BASE_URL": ("base_url", str),
    "SPARROW_AI_API_KEY_ENV": ("api_key_env", str),
    "SPARROW_AI_MAX_TOKENS": ("max_tokens", int),
    "SPARROW_AI_TEMPERATURE": ("temperature", float),
    "SPARROW_AI_EFFORT": ("effort", str),
    "SPARROW_AI_TIMEOUT_S": ("timeout_s", float),
    "SPARROW_AI_MAX_RETRIES": ("max_retries", int),
    "SPARROW_AI_STREAM": ("stream", lambda v: v.lower() in ("1", "true", "yes")),
    "SPARROW_AI_STRUCTURED_OUTPUT": ("structured_output", str),
}


def _is_tracked(path: Path) -> bool:
    result = subprocess.run(
        ["git", "ls-files", "--error-unmatch", path.name],
        cwd=path.parent,
        capture_output=True,
        check=False,
    )
    return result.returncode == 0


def _config_files(root: Path) -> list[Path]:
    candidates = [root / LOCAL_FILE, USER_FILE.expanduser()]
    explicit = os.environ.get("SPARROW_AI_CONFIG")
    if explicit:
        candidates.insert(0, Path(explicit))
    return [p for p in candidates if p.is_file()]


def _load_profiles(path: Path) -> tuple[dict[str, dict], str]:
    raw = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    profiles = raw.get("profiles", {})
    if not isinstance(profiles, dict) or not profiles:
        raise ConfigError(f"{path}: no profiles defined")
    for name, body in profiles.items():
        if "api_key" in body and _is_tracked(path):
            raise ConfigError(
                f"{path}: profile '{name}' contains api_key but the file is tracked by Git; "
                "use api_key_env instead"
            )
    return profiles, raw.get("default", next(iter(profiles)))


def _validate(config: AIConfig) -> AIConfig:
    if config.provider not in PROVIDERS:
        raise ConfigError(f"provider must be one of {PROVIDERS}, not '{config.provider}'")
    if config.structured_output not in STRUCTURED_MODES:
        raise ConfigError(f"structured_output must be one of {STRUCTURED_MODES}")
    if not config.model:
        raise ConfigError(f"profile '{config.profile}': model is required")
    if config.provider == "openai-compatible" and not config.base_url:
        raise ConfigError(f"profile '{config.profile}': openai-compatible needs base_url")
    if config.max_tokens <= 0 or config.timeout_s <= 0 or config.max_retries < 0:
        raise ConfigError("max_tokens and timeout_s must be positive, max_retries >= 0")
    return config


def load_config(root: Path, profile: str | None = None) -> AIConfig:
    """Resolves the active configuration.

    Order: the first config file found (SPARROW_AI_CONFIG, ./sparrow-ai.local.yaml,
    ~/.config/sparrow/ai.yaml), the selected profile (argument, SPARROW_AI_PROFILE,
    or the file's default), then SPARROW_AI_* environment variables. Without any
    file, SPARROW_AI_PROVIDER alone is enough.
    """
    body: dict = {}
    selected = profile or os.environ.get("SPARROW_AI_PROFILE", "")
    files = _config_files(root)
    if files:
        profiles, default = _load_profiles(files[0])
        selected = selected or default
        if selected not in profiles:
            raise ConfigError(f"{files[0]}: no profile '{selected}' (have {sorted(profiles)})")
        body = dict(profiles[selected])
    elif selected:
        raise ConfigError(f"profile '{selected}' requested but no config file found")

    for variable, (name, convert) in ENV_OVERRIDES.items():
        if variable in os.environ:
            body[name] = convert(os.environ[variable])
    if "provider" not in body:
        raise ConfigError(
            f"no AI provider configured: create {LOCAL_FILE} (see ai.example.yaml) "
            "or set SPARROW_AI_PROVIDER"
        )
    known = {f.name for f in fields(AIConfig)}
    unknown = sorted(set(body) - known)
    if unknown:
        raise ConfigError(f"unknown setting(s) {unknown}")
    defaults = DEFAULTS.get(body["provider"], {})
    merged = {**defaults, **body, "profile": selected or "(environment)"}
    return _validate(AIConfig(**merged))


def with_overrides(config: AIConfig, **values: object) -> AIConfig:
    return _validate(replace(config, **{k: v for k, v in values.items() if v is not None}))
