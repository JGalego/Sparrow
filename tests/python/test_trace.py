import subprocess
import sys
import textwrap

import pytest

from sparrow.requirements.results import load_results
from sparrow.requirements.scan import scan_c_file, scan_python_file
from sparrow.requirements.schema import ProjectError, load_project, load_requirements
from sparrow.requirements.trace import build_trace

REQUIREMENTS = """
requirements:
  - id: REQ-001
    title: Limit
    statement: The controller shall stop at 10.
    component: controller
    priority: must
    verification: test
    implementation: [src/limit.c::limit]
  - id: REQ-002
    title: Manual
    statement: The manual shall describe the limit.
    component: docs
    priority: should
    verification: inspection
"""


@pytest.fixture
def project(tmp_path):
    (tmp_path / "requirements").mkdir()
    (tmp_path / "requirements" / "main.yaml").write_text(textwrap.dedent(REQUIREMENTS))
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "limit.c").write_text("int limit(int x) { return x > 10; }\n")
    (tmp_path / "tests").mkdir()
    (tmp_path / "tests" / "test_limit.c").write_text('SP_TEST(stops_above, "REQ-001") {}\n')
    (tmp_path / "tests" / "test_py.py").write_text(
        'import pytest\n\n\n@pytest.mark.verifies("REQ-001")\ndef test_stop():\n    pass\n'
    )
    (tmp_path / "sparrow.yaml").write_text(
        "name: demo\nrequirements: [requirements/*.yaml]\ntests: [tests]\n"
    )
    return tmp_path


def run_trace(project, *args):
    return subprocess.run(
        [sys.executable, "-m", "sparrow", "trace", str(project / "sparrow.yaml"), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def test_consistent_project_has_no_errors(project):
    trace = build_trace(load_project(project / "sparrow.yaml"))

    assert trace.errors == []
    assert [t.key for t in trace.tests_for("REQ-001")] == [
        "test_limit::stops_above",
        "test_py::test_stop",
    ]


def test_scanners_read_requirement_ids(project):
    (c_ref,) = scan_c_file(project / "tests" / "test_limit.c")
    (py_ref,) = scan_python_file(project / "tests" / "test_py.py")

    assert c_ref.requirements == ("REQ-001",) and c_ref.line == 1
    assert py_ref.requirements == ("REQ-001",)


def test_requirement_without_tests_is_an_error(project):
    (project / "tests" / "test_limit.c").write_text("")
    (project / "tests" / "test_py.py").write_text("")

    errors = build_trace(load_project(project / "sparrow.yaml")).errors

    assert errors == ["REQ-001 is verified by test but no test declares it"]


def test_unknown_requirement_and_non_test_verification_are_errors(project):
    (project / "tests" / "test_limit.c").write_text(
        'SP_TEST(a, "REQ-001") {}\nSP_TEST(b, "REQ-009") {}\nSP_TEST(c, "REQ-002") {}\n'
    )

    errors = build_trace(load_project(project / "sparrow.yaml")).errors

    assert any("verifies unknown REQ-009" in e for e in errors)
    assert any("REQ-002, which is verified by inspection" in e for e in errors)


def test_stale_implementation_reference_is_an_error(project):
    (project / "src" / "limit.c").write_text("int other(void);\n")

    errors = build_trace(load_project(project / "sparrow.yaml")).errors

    assert errors == ["REQ-001: src/limit.c::limit: symbol not found"]


def test_duplicate_ids_are_rejected(project):
    (project / "requirements" / "more.yaml").write_text(textwrap.dedent(REQUIREMENTS))

    with pytest.raises(ProjectError, match="duplicate"):
        load_requirements(load_project(project / "sparrow.yaml"))


def test_results_mark_parametrized_failures_and_missing_tests(project, tmp_path):
    junit = tmp_path / "r.xml"
    junit.write_text(
        '<testsuite><testcase classname="tests.test_py" name="test_stop[a]"/>'
        '<testcase classname="tests.test_py" name="test_stop[b]"><failure message="x"/></testcase>'
        "</testsuite>"
    )

    assert load_results([junit]) == {"test_py::test_stop": "fail"}


def test_cited_test_that_did_not_run_fails_the_trace(project, tmp_path):
    junit = tmp_path / "r.xml"
    junit.write_text(
        '<testsuite><testcase classname="tests.test_py" name="test_stop"/></testsuite>'
    )

    result = run_trace(project, "--results", str(junit), "--write", str(tmp_path / "t.md"))

    assert result.returncode == 1
    assert "test_limit::stops_above verifies REQ-001 but did not run" in result.stderr


def test_check_fails_on_a_stale_report(project, tmp_path):
    report = tmp_path / "t.md"
    assert run_trace(project, "--write", str(report)).returncode == 0

    report.write_text(report.read_text() + "edit\n")

    assert run_trace(project, "--check", str(report)).returncode == 1


def test_requirement_query_lists_implementation_and_tests(project):
    result = run_trace(project, "--requirement", "REQ-001")

    assert "src/limit.c::limit" in result.stdout
    assert "test_limit.c:1  test_limit::stops_above" in result.stdout
