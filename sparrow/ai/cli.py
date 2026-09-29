"""`sparrow ai ...` subcommands."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from .config import ConfigError, load_config, with_overrides
from .context import ContextTooLarge
from .project import AIProjectError, load_ai_project
from .proposal import ProposalError
from .providers import Message, ProviderError, create_provider
from .session import build_request, run_task
from .tasks import TASKS, TaskError

DEFAULT_PROJECT = "sparrow.yaml"


def _project_path(args: argparse.Namespace) -> Path:
    return Path(args.project or os.environ.get("SPARROW_PROJECT", DEFAULT_PROJECT))


def _config(args: argparse.Namespace, repo: Path):
    config = load_config(repo, args.profile)
    return with_overrides(config, model=args.model)


def _cmd_config(args: argparse.Namespace) -> int:
    config = _config(args, Path.cwd())
    print(json.dumps(config.describe(), indent=2))
    provider = create_provider(config)
    print(f"capabilities: {provider.capabilities}")
    return 0


def _cmd_ping(args: argparse.Namespace) -> int:
    config = _config(args, Path.cwd())
    provider = create_provider(config)
    reply = provider.complete(
        "Reply with the single word: ready", [Message("user", "Are you there?")]
    )
    print(
        f"{config.provider} {reply.model}: {reply.text.strip()!r} "
        f"({reply.input_tokens} in, {reply.output_tokens} out)"
    )
    return 0


def _cmd_tasks(args: argparse.Namespace) -> int:
    for task in TASKS.values():
        writes = ", ".join(task.paths) if task.paths else "nothing (report)"
        print(f"{task.name:13} stage {task.stage:13} <{task.argument}>  writes: {writes}")
    return 0


def _cmd_task(args: argparse.Namespace) -> int:
    ai = load_ai_project(_project_path(args))
    task = TASKS[args.task]
    options = {"scenario": args.scenario, "requirements": args.requirement}
    if args.dry_run:
        config = load_config(ai.repo, args.profile) if args.profile else None
        budget = config.max_context_chars if config else 400_000
        from .config import AIConfig

        request_config = config or AIConfig(
            provider="anthropic", model="-", max_context_chars=budget
        )
        user, context = build_request(ai, task, args.argument, options, request_config)
        for title, body in context.sections:
            print(f"{len(body):8d}  {title}")
        print(f"{len(user):8d}  total characters in the request")
        return 0
    config = _config(args, ai.repo)
    provider = create_provider(config)
    outcome = run_task(
        ai,
        task,
        args.argument,
        options,
        config,
        provider,
        apply_changes=args.apply or args.verify,
        verify=args.verify,
        attempts=args.attempts,
    )
    for note in outcome.notes:
        print(f"note: {note}", file=sys.stderr)
    for index, (model, tokens_in, tokens_out) in enumerate(outcome.usage, 1):
        print(f"request {index}: {model}, {tokens_in} tokens in, {tokens_out} out", file=sys.stderr)
    print(outcome.text)
    for path in outcome.saved:
        print(f"saved {path.relative_to(ai.repo)}", file=sys.stderr)
    if outcome.applied:
        print("applied to the working tree; review with `git diff`", file=sys.stderr)
    for gate in outcome.gates:
        status = "pass" if gate.passed else "FAIL"
        print(f"gate {status}: {gate.command}", file=sys.stderr)
        if not gate.passed:
            print(gate.output[-4000:], file=sys.stderr)
    return 0 if outcome.gates_passed else 1


def _guarded(handler):
    def run(args: argparse.Namespace) -> int:
        try:
            return handler(args)
        except (
            ConfigError,
            ProviderError,
            ProposalError,
            TaskError,
            AIProjectError,
            ContextTooLarge,
        ) as error:
            print(f"sparrow ai: {error}", file=sys.stderr)
            return 2

    return run


def add_parser(commands) -> None:
    ai = commands.add_parser("ai", help="optional AI assistance (needs the `ai` extra)")
    ai.add_argument("--profile", help="profile in the AI config file")
    ai.add_argument("--model", help="override the profile's model")
    ai.add_argument(
        "-p",
        "--project",
        help=f"project descriptor (default $SPARROW_PROJECT or {DEFAULT_PROJECT})",
    )
    sub = ai.add_subparsers(dest="ai_command", required=True)

    sub.add_parser("config", help="show the resolved configuration").set_defaults(
        handler=_guarded(_cmd_config)
    )
    sub.add_parser("ping", help="send a minimal request to check the provider").set_defaults(
        handler=_guarded(_cmd_ping)
    )
    sub.add_parser("tasks", help="list tasks").set_defaults(handler=_guarded(_cmd_tasks))

    for task in TASKS.values():
        parser = sub.add_parser(task.name, help=f"stage {task.stage} ({task.kind})")
        optional = task.name in ("review", "explain")
        parser.add_argument(
            "argument", nargs="?" if optional else None, default="", help=task.argument
        )
        parser.add_argument("--dry-run", action="store_true", help="show the context, send nothing")
        if task.kind == "proposal":
            parser.add_argument(
                "--apply", action="store_true", help="write the change to the working tree"
            )
            parser.add_argument(
                "--verify", action="store_true", help="apply, then run the project gates"
            )
        parser.add_argument(
            "--attempts", type=int, default=2, help="retries after a rejected reply"
        )
        if task.name == "analyze":
            parser.add_argument("--scenario", help="scenario file the trace was recorded from")
            parser.add_argument("--requirement", action="append", help="requirement to focus on")
        parser.set_defaults(
            handler=_guarded(_cmd_task),
            task=task.name,
            apply=False,
            verify=False,
            scenario=None,
            requirement=None,
        )
