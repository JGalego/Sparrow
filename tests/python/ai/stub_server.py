"""A local HTTP server that imitates the Anthropic and OpenAI APIs closely enough for the SDKs.

Each test queues responses; the server records every request so tests can
check exactly what an adapter sent.
"""

from __future__ import annotations

import json
import threading
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


@dataclass
class Reply:
    status: int = 200
    body: dict | None = None
    sse: list[tuple[str | None, dict | str]] | None = None  # (event name, data)
    headers: dict[str, str] = field(default_factory=dict)


@dataclass
class Recorded:
    path: str
    headers: dict[str, str]
    body: dict


class StubServer:
    def __init__(self) -> None:
        self.replies: list[Reply] = []
        self.requests: list[Recorded] = []
        stub = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                length = int(self.headers.get("content-length", 0))
                body = json.loads(self.rfile.read(length) or b"{}")
                stub.requests.append(
                    Recorded(self.path, {k.lower(): v for k, v in self.headers.items()}, body)
                )
                reply = stub.replies.pop(0) if stub.replies else Reply(500, {"error": "empty"})
                self.send_response(reply.status)
                for key, value in reply.headers.items():
                    self.send_header(key, value)
                if reply.sse is not None:
                    self.send_header("content-type", "text/event-stream")
                    self.end_headers()
                    for event, data in reply.sse:
                        text = data if isinstance(data, str) else json.dumps(data)
                        prefix = f"event: {event}\n" if event else ""
                        self.wfile.write(f"{prefix}data: {text}\n\n".encode())
                    return
                payload = json.dumps(reply.body or {}).encode()
                self.send_header("content-type", "application/json")
                self.send_header("content-length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self._server.server_port}"

    def __enter__(self) -> StubServer:
        self._thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self._server.shutdown()
        self._server.server_close()


def anthropic_message(text: str, stop_reason: str = "end_turn", model: str = "claude-opus-5-5"):
    return {
        "id": "msg_1",
        "type": "message",
        "role": "assistant",
        "model": model,
        "content": [{"type": "text", "text": text}],
        "stop_reason": stop_reason,
        "stop_sequence": None,
        "usage": {"input_tokens": 11, "output_tokens": 7},
    }


def anthropic_stream(text_parts: list[str], model: str = "claude-opus-5-5"):
    events: list[tuple[str | None, dict | str]] = [
        (
            "message_start",
            {
                "type": "message_start",
                "message": {
                    **anthropic_message("", model=model),
                    "content": [],
                    "stop_reason": None,
                    "usage": {"input_tokens": 11, "output_tokens": 0},
                },
            },
        ),
        (
            "content_block_start",
            {
                "type": "content_block_start",
                "index": 0,
                "content_block": {"type": "text", "text": ""},
            },
        ),
    ]
    for part in text_parts:
        events.append(
            (
                "content_block_delta",
                {
                    "type": "content_block_delta",
                    "index": 0,
                    "delta": {"type": "text_delta", "text": part},
                },
            )
        )
    events += [
        ("content_block_stop", {"type": "content_block_stop", "index": 0}),
        (
            "message_delta",
            {
                "type": "message_delta",
                "delta": {"stop_reason": "end_turn", "stop_sequence": None},
                "usage": {"output_tokens": 7},
            },
        ),
        ("message_stop", {"type": "message_stop"}),
    ]
    return events


def openai_completion(text: str, model: str = "test-model"):
    return {
        "id": "chatcmpl-1",
        "object": "chat.completion",
        "created": 0,
        "model": model,
        "choices": [
            {
                "index": 0,
                "message": {"role": "assistant", "content": text},
                "finish_reason": "stop",
            }
        ],
        "usage": {"prompt_tokens": 13, "completion_tokens": 5, "total_tokens": 18},
    }


def openai_stream(text_parts: list[str], model: str = "test-model"):
    def chunk(delta: dict, finish: str | None = None):
        return {
            "id": "chatcmpl-1",
            "object": "chat.completion.chunk",
            "created": 0,
            "model": model,
            "choices": [{"index": 0, "delta": delta, "finish_reason": finish}],
        }

    events: list[tuple[str | None, dict | str]] = [(None, chunk({"role": "assistant"}))]
    events += [(None, chunk({"content": part})) for part in text_parts]
    events += [(None, chunk({}, "stop")), (None, "[DONE]")]
    return events
