import subprocess

import pytest

from sparrow.ai.config import ConfigError, load_config

PROFILES = """
default: local
profiles:
  local:
    provider: openai-compatible
    base_url: http://localhost:11434/v1
    model: qwen2.5:7b-instruct
  groq:
    provider: openai-compatible
    base_url: https://api.groq.com/openai/v1
    api_key_env: GROQ_API_KEY
    model: llama-3.3-70b-versatile
  claude:
    provider: anthropic
"""


@pytest.fixture
def repo(tmp_path):
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    (tmp_path / "sparrow-ai.local.yaml").write_text(PROFILES)
    return tmp_path


def test_default_profile_is_used(repo):
    config = load_config(repo)

    assert config.profile == "local"
    assert config.model == "qwen2.5:7b-instruct"


def test_profile_can_be_selected_by_argument_or_environment(repo, monkeypatch):
    assert load_config(repo, "groq").base_url.startswith("https://api.groq.com")

    monkeypatch.setenv("SPARROW_AI_PROFILE", "claude")
    assert load_config(repo).provider == "anthropic"


def test_anthropic_profile_gets_provider_defaults(repo):
    config = load_config(repo, "claude")

    assert config.model == "claude-opus-5-5"
    assert config.api_key_env == "ANTHROPIC_API_KEY"


def test_key_is_read_from_the_named_environment_variable(repo, monkeypatch):
    monkeypatch.setenv("GROQ_API_KEY", "gsk-placeholder")

    config = load_config(repo, "groq")

    assert config.resolve_api_key() == "gsk-placeholder"
    assert "gsk-placeholder" not in str(config.describe())


def test_environment_overrides_the_profile(repo, monkeypatch):
    monkeypatch.setenv("SPARROW_AI_MODEL", "other")
    monkeypatch.setenv("SPARROW_AI_TIMEOUT_S", "12")

    config = load_config(repo)

    assert (config.model, config.timeout_s) == ("other", 12.0)


def test_environment_alone_is_enough(tmp_path, monkeypatch):
    monkeypatch.setenv("SPARROW_AI_PROVIDER", "anthropic")

    assert load_config(tmp_path).model == "claude-opus-5-5"


def test_missing_configuration_explains_what_to_do(tmp_path):
    with pytest.raises(ConfigError, match="SPARROW_AI_PROVIDER"):
        load_config(tmp_path)


def test_unknown_profile_is_rejected(repo):
    with pytest.raises(ConfigError, match="no profile 'nope'"):
        load_config(repo, "nope")


def test_compatible_provider_needs_a_base_url(tmp_path, monkeypatch):
    monkeypatch.setenv("SPARROW_AI_PROVIDER", "openai-compatible")
    monkeypatch.setenv("SPARROW_AI_MODEL", "m")

    with pytest.raises(ConfigError, match="base_url"):
        load_config(tmp_path)


def test_openai_needs_an_explicit_model(tmp_path, monkeypatch):
    monkeypatch.setenv("SPARROW_AI_PROVIDER", "openai")

    with pytest.raises(ConfigError, match="model is required"):
        load_config(tmp_path)


def test_unknown_setting_is_rejected(repo):
    (repo / "sparrow-ai.local.yaml").write_text(
        "profiles:\n  a: {provider: anthropic, temprature: 1}\n"
    )

    with pytest.raises(ConfigError, match="temprature"):
        load_config(repo)


def test_key_in_a_tracked_file_is_refused(repo):
    path = repo / "sparrow-ai.local.yaml"
    path.write_text("profiles:\n  a: {provider: anthropic, api_key: sk-placeholder}\n")
    subprocess.run(["git", "add", "-f", path.name], cwd=repo, check=True)

    with pytest.raises(ConfigError, match="tracked by Git"):
        load_config(repo)


def test_key_in_an_untracked_file_is_accepted(repo):
    (repo / "sparrow-ai.local.yaml").write_text(
        "profiles:\n  a: {provider: anthropic, api_key: sk-placeholder}\n"
    )

    config = load_config(repo)

    assert config.resolve_api_key() == "sk-placeholder"
    assert config.describe()["api_key"] == "(set in config file)"
