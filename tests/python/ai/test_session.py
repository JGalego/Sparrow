"""The task runner end to end with a scripted provider, on a small throwaway project."""

import json
import subprocess
import textwrap

import pytest

from sparrow.ai.config import AIConfig
from sparrow.ai.context import ContextTooLarge
from sparrow.ai.project import load_ai_project
from sparrow.ai.proposal import ProposalError
from sparrow.ai.providers import Capabilities, Completion, Provider
from sparrow.ai.session import build_request, run_task
from sparrow.ai.tasks import TASKS, TaskError

CONFIG = AIConfig(provider="anthropic", model="scripted")


class ScriptedProvider(Provider):
    name = "scripted"
    capabilities = Capabilities(streaming=False, structured_output="json_schema", temperature=False)

    def __init__(self, *replies):
        self.replies = list(replies)
        self.calls = []

    def complete(self, system, messages, schema=None, on_text=None):
        self.calls.append((system, messages, schema))
        return Completion(self.replies.pop(0), "scripted", "end_turn")


def proposal(*edits):
    return json.dumps({"summary": "s", "rationale": "r", "edits": list(edits)})


@pytest.fixture
def project(tmp_path):
    root = tmp_path / "app"
    (root / "requirements").mkdir(parents=True)
    (root / "src").mkdir()
    (root / "tests").mkdir()
    (root / "requirements" / "main.yaml").write_text(
        textwrap.dedent("""\
        requirements:
          - id: REQ-001
            title: Limit
            statement: The controller shall stop at 10.
            component: controller
            priority: must
            verification: test
            implementation: [src/limit.c::limit]
        """)
    )
    (root / "src" / "limit.c").write_text("int limit(int x)\n{\n    return x > 10;\n}\n")
    (root / "tests" / "test_limit.c").write_text('SP_TEST(stops_above, "REQ-001") {}\n')
    (root / "sparrow.yaml").write_text(
        textwrap.dedent("""\
        name: app
        requirements: [requirements/*.yaml]
        tests: [tests]
        ai:
          paths:
            requirements: [requirements/*.yaml]
            code: [src/*.c]
            tests: [tests/*.c]
            docs: [README.md]
          gates:
            - grep -q "x >= 10" app/src/limit.c
        """)
    )
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    subprocess.run(["git", "add", "."], cwd=tmp_path, check=True)
    subprocess.run(
        ["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "init"],
        cwd=tmp_path,
        check=True,
    )
    return load_ai_project(root / "sparrow.yaml")


def test_code_task_context_holds_the_requirement_implementation_and_tests(project):
    user, context = build_request(project, TASKS["code"], "REQ-001", {}, CONFIG)

    titles = [title for title, _ in context.sections]
    assert "requirement REQ-001" in titles
    assert "file app/src/limit.c (implements it)" in titles
    assert "file app/tests/test_limit.c (verifies it)" in titles
    assert "app/src/*.c" in user


def test_unknown_requirement_is_rejected(project):
    with pytest.raises(TaskError, match="REQ-999"):
        build_request(project, TASKS["tests"], "REQ-999", {}, CONFIG)


def test_context_over_budget_fails_instead_of_truncating(project):
    small = AIConfig(provider="anthropic", model="m", max_context_chars=50)

    with pytest.raises(ContextTooLarge, match="exceed 50"):
        build_request(project, TASKS["code"], "REQ-001", {}, small)


def test_requirements_task_suggests_the_next_free_id(project):
    user, _ = build_request(project, TASKS["requirements"], "add a warning", {}, CONFIG)

    assert "REQ-002" in user


def test_proposal_is_validated_saved_and_not_applied_by_default(project):
    provider = ScriptedProvider(
        proposal(
            {
                "path": "app/src/limit.c",
                "action": "replace",
                "search": "x > 10",
                "replace": "x >= 10",
                "content": "",
            }
        )
    )

    outcome = run_task(project, TASKS["code"], "REQ-001", {}, CONFIG, provider)

    assert "+    return x >= 10;" in outcome.text
    assert {p.suffix for p in outcome.saved} == {".patch", ".json"}
    assert "x > 10" in (project.repo / "app/src/limit.c").read_text()
    assert provider.calls[0][2]["required"] == ["summary", "rationale", "edits"]


def test_apply_and_verify_run_the_gates(project):
    provider = ScriptedProvider(
        proposal(
            {
                "path": "app/src/limit.c",
                "action": "replace",
                "search": "x > 10",
                "replace": "x >= 10",
                "content": "",
            }
        )
    )

    outcome = run_task(
        project, TASKS["code"], "REQ-001", {}, CONFIG, provider, apply_changes=True, verify=True
    )

    assert outcome.applied
    assert [g.passed for g in outcome.gates] == [True]


def test_failing_gate_is_reported(project):
    provider = ScriptedProvider(
        proposal(
            {
                "path": "app/src/limit.c",
                "action": "replace",
                "search": "x > 10",
                "replace": "x > 11",
                "content": "",
            }
        )
    )

    outcome = run_task(
        project, TASKS["code"], "REQ-001", {}, CONFIG, provider, apply_changes=True, verify=True
    )

    assert not outcome.gates_passed


def test_rejected_reply_is_sent_back_once_with_the_reason(project):
    bad = proposal(
        {
            "path": "app/src/limit.c",
            "action": "replace",
            "search": "x > 99",
            "replace": "y",
            "content": "",
        }
    )
    good = proposal(
        {
            "path": "app/src/limit.c",
            "action": "replace",
            "search": "x > 10",
            "replace": "x >= 10",
            "content": "",
        }
    )
    provider = ScriptedProvider(bad, good)

    outcome = run_task(project, TASKS["code"], "REQ-001", {}, CONFIG, provider)

    retry_messages = provider.calls[1][1]
    assert retry_messages[-1].role == "user"
    assert "found 0 times" in retry_messages[-1].content
    assert any("attempt 1 rejected" in note for note in outcome.notes)


def test_repeatedly_invalid_reply_fails(project):
    bad = proposal(
        {"path": "app/src/other.h", "action": "create", "search": "", "replace": "", "content": "x"}
    )
    provider = ScriptedProvider(bad, bad)

    with pytest.raises(ProposalError, match="may only change"):
        run_task(project, TASKS["code"], "REQ-001", {}, CONFIG, provider)


def test_code_task_may_not_touch_tests(project):
    provider = ScriptedProvider(
        proposal(
            {
                "path": "app/tests/test_limit.c",
                "action": "replace",
                "search": "stops_above",
                "replace": "x",
                "content": "",
            }
        ),
    )

    with pytest.raises(ProposalError, match="may only change"):
        run_task(project, TASKS["code"], "REQ-001", {}, CONFIG, provider, attempts=1)


def test_review_reports_on_the_working_tree_diff(project):
    (project.repo / "app/src/limit.c").write_text("int limit(int x)\n{\n    return x > 12;\n}\n")
    report = json.dumps(
        {
            "summary": "Limit moved.",
            "findings": [
                {
                    "severity": "error",
                    "location": "app/src/limit.c:3",
                    "requirement": "REQ-001",
                    "text": "Limit no longer 10.",
                }
            ],
            "next_steps": ["Restore the limit."],
        }
    )
    provider = ScriptedProvider(report)

    outcome = run_task(project, TASKS["review"], "", {}, CONFIG, provider)

    user = provider.calls[0][1][0].content
    assert "-    return x > 10;" in user
    assert "requirement REQ-001" in user
    assert "ERROR   app/src/limit.c:3 [REQ-001]: Limit no longer 10." in outcome.text


def test_review_without_changes_is_refused(project):
    with pytest.raises(TaskError, match="no changes"):
        run_task(project, TASKS["review"], "", {}, CONFIG, ScriptedProvider())


def test_explain_collects_failing_tests(project, tmp_path):
    results = project.repo / "results.xml"
    results.write_text(
        '<testsuite><testcase classname="test_limit" name="stops_above">'
        '<failure message="expected 1, got 0"/></testcase></testsuite>'
    )
    report = json.dumps({"summary": "s", "findings": [], "next_steps": []})
    provider = ScriptedProvider(report)

    run_task(project, TASKS["explain"], "results.xml", {}, CONFIG, provider)

    user = provider.calls[0][1][0].content
    assert "expected 1, got 0" in user
    assert "file app/tests/test_limit.c (failing test)" in user
    assert "file app/src/limit.c (implements it)" in user


def test_explain_with_all_tests_passing_is_refused(project):
    (project.repo / "results.xml").write_text(
        '<testsuite><testcase classname="test_limit" name="stops_above"/></testsuite>'
    )

    with pytest.raises(TaskError, match="all tests passed"):
        run_task(project, TASKS["explain"], "results.xml", {}, CONFIG, ScriptedProvider())
