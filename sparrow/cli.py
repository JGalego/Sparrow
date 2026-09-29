"""Command line entry point: `sparrow <command>`."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .ai.cli import add_parser as add_ai_parser
from .codegen.generate import generate
from .codegen.model import ModelError
from .requirements.report import render, render_requirement
from .requirements.results import load_results
from .requirements.schema import ProjectError, load_project
from .requirements.trace import build_trace


def _cmd_gen(args: argparse.Namespace) -> int:
    try:
        stale = generate(Path(args.model), check=args.check)
    except ModelError as error:
        print(f"model error: {error}", file=sys.stderr)
        return 2
    if args.check and stale:
        for path in stale:
            print(f"out of date: {path}", file=sys.stderr)
        print("run `sparrow gen` and commit the result", file=sys.stderr)
        return 1
    for path in stale:
        print(f"wrote {path}")
    return 0


def _cmd_trace(args: argparse.Namespace) -> int:
    try:
        trace = build_trace(load_project(Path(args.project)))
    except ProjectError as error:
        print(f"project error: {error}", file=sys.stderr)
        return 2
    if args.requirement:
        text = render_requirement(trace, args.requirement)
        if text is None:
            print(f"unknown requirement {args.requirement}", file=sys.stderr)
            return 2
        print(text)
        return 0
    if args.file:
        for requirement in trace.requirements_for_file(Path(args.file)):
            print(f"{requirement.id}  {requirement.title}")
        return 0
    for error in trace.errors:
        print(f"trace error: {error}", file=sys.stderr)
    results = load_results([Path(p) for p in args.results]) if args.results else None
    report = render(trace, results)
    if args.write:
        Path(args.write).write_text(report, encoding="utf-8")
    elif args.check:
        current = Path(args.check).read_text(encoding="utf-8") if Path(args.check).exists() else ""
        if current != report:
            print(f"out of date: {args.check}; run `sparrow trace --write`", file=sys.stderr)
            return 1
    else:
        print(report)
    return 1 if trace.errors else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="sparrow")
    commands = parser.add_subparsers(dest="command", required=True)

    gen = commands.add_parser("gen", help="generate C and Python interface code from a model")
    gen.add_argument("model", help="path to the model YAML file")
    gen.add_argument("--check", action="store_true", help="fail if generated files are stale")
    gen.set_defaults(handler=_cmd_gen)

    trace = commands.add_parser("trace", help="requirements traceability report and queries")
    trace.add_argument("project", help="path to the project descriptor (sparrow.yaml)")
    trace.add_argument("--write", metavar="FILE", help="write the Markdown report to FILE")
    trace.add_argument("--check", metavar="FILE", help="fail if FILE differs from the report")
    trace.add_argument("--results", nargs="+", metavar="XML", help="JUnit results to include")
    trace.add_argument("--requirement", metavar="ID", help="show one requirement and its tests")
    trace.add_argument(
        "--file", metavar="PATH", help="list the requirements a source file implements"
    )
    trace.set_defaults(handler=_cmd_trace)

    add_ai_parser(commands)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
