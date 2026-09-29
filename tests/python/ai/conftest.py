import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))

from stub_server import StubServer  # noqa: E402


@pytest.fixture
def stub():
    with StubServer() as server:
        yield server


@pytest.fixture(autouse=True)
def isolated_environment(monkeypatch, tmp_path):
    """No test sees the developer's AI configuration or credentials."""
    for name in list(__import__("os").environ):
        if name.startswith(("SPARROW_AI_", "ANTHROPIC_", "OPENAI_", "GROQ_")):
            monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
