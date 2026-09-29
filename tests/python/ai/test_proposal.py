import json

import pytest

from sparrow.ai.proposal import (
    Policy,
    ProposalError,
    apply,
    parse_json_reply,
    parse_proposal,
    render_diff,
    resolve,
)

POLICY = Policy(allowed=("src/*.c", "docs/*.md"), protected=("src/generated.c",))


def reply(*edits, summary="change"):
    return json.dumps({"summary": summary, "rationale": "why", "edits": list(edits)})


def edit(path, action="replace", search="", replace="", content=""):
    return {
        "path": path,
        "action": action,
        "search": search,
        "replace": replace,
        "content": content,
    }


@pytest.fixture
def repo(tmp_path):
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "a.c").write_text("int a = 1;\nint b = 2;\n")
    (tmp_path / "src" / "generated.c").write_text("x\n")
    return tmp_path


def test_replace_edit_produces_a_unified_diff(repo):
    proposal = parse_proposal(reply(edit("src/a.c", search="int b = 2;", replace="int b = 3;")))

    changes = resolve(proposal, repo, POLICY)
    diff = render_diff(changes, repo)

    assert "-int b = 2;" in diff and "+int b = 3;" in diff
    assert "--- a/src/a.c" in diff


def test_create_edit_adds_a_file(repo):
    proposal = parse_proposal(reply(edit("docs/new.md", action="create", content="# New\n")))

    changes = resolve(proposal, repo, POLICY)
    apply(changes)

    assert (repo / "docs" / "new.md").read_text() == "# New\n"


def test_edits_to_one_file_apply_in_order(repo):
    proposal = parse_proposal(
        reply(
            edit("src/a.c", search="int a = 1;", replace="int a = 10;"),
            edit("src/a.c", search="int a = 10;", replace="int a = 11;"),
        )
    )

    ((_, new),) = resolve(proposal, repo, POLICY).values()

    assert new.startswith("int a = 11;")


def test_nothing_is_written_until_apply(repo):
    proposal = parse_proposal(reply(edit("src/a.c", search="int a = 1;", replace="int a = 5;")))

    resolve(proposal, repo, POLICY)

    assert (repo / "src" / "a.c").read_text().startswith("int a = 1;")


@pytest.mark.parametrize(
    ("bad_edit", "message"),
    [
        (edit("src/a.c", search="int c", replace="x"), "found 0 times"),
        (edit("src/a.c", search="int", replace="x"), "found 2 times"),
        (edit("src/a.c", search="", replace="x"), "found 0 times"),
        (edit("src/missing.c", search="a", replace="b"), "does not exist"),
        (edit("src/a.c", action="create", content="x"), "file exists"),
        (edit("src/generated.c", search="x", replace="y"), "generated file"),
        (edit("Makefile", action="create", content="x"), "may only change"),
        (edit("../outside.c", action="create", content="x"), "outside the repository"),
        (edit("src/a.c", action="delete"), "unknown action"),
    ],
)
def test_invalid_edits_are_rejected(repo, bad_edit, message):
    proposal = parse_proposal(reply(bad_edit))

    with pytest.raises(ProposalError, match=message):
        resolve(proposal, repo, POLICY)


def test_one_invalid_edit_rejects_the_whole_proposal(repo):
    proposal = parse_proposal(
        reply(
            edit("docs/ok.md", action="create", content="ok"),
            edit("src/a.c", search="missing", replace="x"),
        )
    )

    with pytest.raises(ProposalError):
        resolve(proposal, repo, POLICY)
    assert not (repo / "docs" / "ok.md").exists()


def test_empty_proposal_is_rejected(repo):
    with pytest.raises(ProposalError, match="no edits"):
        resolve(parse_proposal(reply()), repo, POLICY)


def test_json_is_recovered_from_a_fenced_or_chatty_reply():
    assert parse_json_reply('```json\n{"a": 1}\n```') == {"a": 1}
    assert parse_json_reply('Here it is:\n{"a": 1}\nDone.') == {"a": 1}


def test_reply_without_json_is_rejected():
    with pytest.raises(ProposalError, match="no JSON"):
        parse_json_reply("I cannot do that.")


def test_reply_missing_fields_is_rejected():
    with pytest.raises(ProposalError, match="schema"):
        parse_proposal('{"summary": "x"}')
