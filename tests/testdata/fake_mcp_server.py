#!/usr/bin/env python3

import json
from pathlib import Path
import sys
import time


def read_message():
    buffer = b""
    while b"\r\n\r\n" not in buffer:
        chunk = sys.stdin.buffer.read(1)
        if not chunk:
            return None
        buffer += chunk

    header_block, remainder = buffer.split(b"\r\n\r\n", 1)
    headers = {}
    for raw_line in header_block.split(b"\r\n"):
        key, value = raw_line.decode("utf-8").split(":", 1)
        headers[key.strip().lower()] = value.strip()

    content_length = int(headers["content-length"])
    body = remainder
    while len(body) < content_length:
        chunk = sys.stdin.buffer.read(content_length - len(body))
        if not chunk:
            return None
        body += chunk
    return json.loads(body[:content_length].decode("utf-8"))


def send_message(message):
    body = json.dumps(message).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def send_invalid_json_frame():
    body = b"{invalid json"
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def send_stderr(text):
    sys.stderr.write(text)
    if not text.endswith("\n"):
        sys.stderr.write("\n")
    sys.stderr.flush()


def append_call_record(path, tool_name):
    if not path:
        return
    Path(path).write_text(
        (Path(path).read_text(encoding="utf-8") if Path(path).exists() else "")
        + tool_name
        + "\n",
        encoding="utf-8",
    )


def all_tools():
    return [
        {
            "name": "adder",
            "description": "Add two integers.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "a": {"type": "integer"},
                    "b": {"type": "integer"},
                },
                "required": ["a", "b"],
            },
        },
        {
            "name": "echo",
            "description": "Echo a string.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text": {"type": "string"},
                },
                "required": ["text"],
            },
        },
        {
            "name": "big_text",
            "description": "Return a large blob.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "size": {"type": "integer"},
                },
            },
        },
        {
            "name": "paged_echo",
            "description": "Echo with pagination coverage.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text": {"type": "string"},
                },
                "required": ["text"],
            },
        },
    ]


def initialize_response(request_id):
    send_message(
        {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "fake-mcp", "version": "0.0.1"},
            },
        }
    )


def tool_list_page(scenario, cursor):
    tools = all_tools()
    if scenario != "paged_tools":
        return {"tools": tools[:3]}

    if cursor is None:
        return {"tools": tools[:2], "nextCursor": "page-2"}
    if cursor == "page-2":
        return {"tools": tools[2:]}
    return {"error": {"code": -32602, "message": f"Unknown cursor: {cursor}"}}


def send_protocol_like_stderr(label):
    send_stderr(f"{label}: Content-Length: 31\\r\\n\\r\\n{{\"stderr_only\":true}}")


def handle_tool_call(message, aux_path):
    params = message.get("params", {})
    tool_name = params.get("name")
    arguments = params.get("arguments", {})
    append_call_record(aux_path, str(tool_name))

    if tool_name == "adder":
        total = int(arguments.get("a", 0)) + int(arguments.get("b", 0))
        send_message(
            {
                "jsonrpc": "2.0",
                "id": message["id"],
                "result": {
                    "content": [{"type": "text", "text": str(total)}],
                    "structuredContent": {"sum": total},
                },
            }
        )
        return True
    if tool_name in {"echo", "paged_echo"}:
        text = str(arguments.get("text", ""))
        send_message(
            {
                "jsonrpc": "2.0",
                "id": message["id"],
                "result": {
                    "content": [{"type": "text", "text": text}],
                },
            }
        )
        return True
    if tool_name == "big_text":
        size = int(arguments.get("size", 4096))
        payload = "x" * size
        send_message(
            {
                "jsonrpc": "2.0",
                "id": message["id"],
                "result": {
                    "content": [{"type": "text", "text": payload}],
                },
            }
        )
        return True

    send_message(
        {
            "jsonrpc": "2.0",
            "id": message["id"],
            "error": {"code": -32601, "message": f"Unknown tool: {tool_name}"},
        }
    )
    return True


def handle_successful_flow(message, scenario, aux_path, state):
    method = message.get("method")
    if method == "initialize":
        if scenario == "stderr_noise":
            send_protocol_like_stderr("stderr before initialize")
        initialize_response(message["id"])
        if scenario == "stderr_noise":
            send_protocol_like_stderr("stderr after initialize")
        return True

    if method == "notifications/initialized":
        state["initialized_notified"] = True
        if scenario == "stderr_noise":
            send_protocol_like_stderr("initialized notification received")
        return True

    if method == "tools/list":
        if scenario == "lifecycle_order" and not state["initialized_notified"]:
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": message["id"],
                    "error": {
                        "code": -32001,
                        "message": "tools/list arrived before notifications/initialized",
                    },
                }
            )
            return True

        page = tool_list_page(scenario, message.get("params", {}).get("cursor"))
        if "error" in page:
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": message["id"],
                    "error": page["error"],
                }
            )
            return True
        send_message(
            {
                "jsonrpc": "2.0",
                "id": message["id"],
                "result": page,
            }
        )
        if scenario == "write_stall_after_init":
            send_stderr("server will stop reading stdin after tool discovery")
            time.sleep(5)
            raise SystemExit(0)
        return True

    if method == "tools/call":
        return handle_tool_call(message, aux_path)

    return True


def main():
    scenario = sys.argv[1] if len(sys.argv) > 1 else "success"
    aux_path = sys.argv[2] if len(sys.argv) > 2 else None

    if scenario == "broken_pipe":
        send_stderr("server is exiting early to simulate broken pipe")
        return 0

    state = {"initialized_notified": False}

    while True:
        message = read_message()
        if message is None:
            return 0

        if scenario == "malformed_json":
            send_stderr("server is about to send malformed json")
            send_invalid_json_frame()
            return 0

        if scenario == "timeout":
            send_stderr("server is intentionally not responding")
            time.sleep(5)
            return 0

        handle_successful_flow(message, scenario, aux_path, state)


if __name__ == "__main__":
    raise SystemExit(main())
