#!/usr/bin/env python3
"""Independent DBGp IDE server for the Linux AutoHotkey debugger.

The runtime is the DBGp TCP client. This oracle listens, launches it, validates
protocol framing and state transitions, then writes a JSON evidence summary.
"""
from __future__ import annotations

import argparse
import base64
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.parse
import xml.etree.ElementTree as ET


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def recv_exact(conn: socket.socket, count: int) -> bytes:
    chunks: list[bytes] = []
    left = count
    while left:
        chunk = conn.recv(left)
        if not chunk:
            raise RuntimeError(f"DBGp EOF with {left} bytes remaining")
        chunks.append(chunk)
        left -= len(chunk)
    return b"".join(chunks)


def recv_packet(conn: socket.socket) -> tuple[ET.Element, str]:
    digits = bytearray()
    while True:
        char = recv_exact(conn, 1)
        if char == b"\0":
            break
        if not char.isdigit() or len(digits) > 19:
            raise RuntimeError(f"invalid DBGp length prefix: {digits + char!r}")
        digits.extend(char)
    if not digits:
        raise RuntimeError("empty DBGp length prefix")
    length = int(digits)
    payload = recv_exact(conn, length)
    terminator = recv_exact(conn, 1)
    if terminator != b"\0":
        raise RuntimeError("DBGp packet missing NUL terminator")
    text = payload.decode("utf-8")
    return ET.fromstring(text), text


def send_command(conn: socket.socket, command: str) -> None:
    conn.sendall(command.encode("utf-8") + b"\0")


def command_response(conn: socket.socket, command: str, tx: int) -> tuple[ET.Element, str]:
    send_command(conn, f"{command} -i {tx}")
    root, text = recv_packet(conn)
    if local_name(root.tag) != "response":
        raise AssertionError(f"expected response for {command}, got {text}")
    if root.attrib.get("transaction_id") != str(tx):
        raise AssertionError(f"transaction mismatch for {command}: {text}")
    if root.find(".//error") is not None or any(local_name(e.tag) == "error" for e in root.iter()):
        raise AssertionError(f"DBGp error for {command}: {text}")
    return root, text


def property_value(root: ET.Element, name: str) -> str:
    for prop in root.iter():
        if local_name(prop.tag) != "property" or prop.attrib.get("name") != name:
            continue
        data = prop.text or ""
        if prop.attrib.get("encoding") == "base64":
            return base64.b64decode(data).decode("utf-8")
        return data
    raise AssertionError(f"property {name!r} absent")


def stack_line(root: ET.Element) -> int:
    for item in root.iter():
        if local_name(item.tag) == "stack" and item.attrib.get("level") == "0":
            return int(item.attrib["lineno"])
    raise AssertionError("top stack frame absent")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime", type=pathlib.Path)
    parser.add_argument("fixture", type=pathlib.Path)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    fixture = args.fixture.resolve()
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    marker = pathlib.Path("/tmp/ahk-dbgp-fixture.out")
    marker.unlink(missing_ok=True)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(15)
    port = listener.getsockname()[1]
    proc = subprocess.Popen(
        [str(runtime), "--debug", f"127.0.0.1:{port}", str(fixture), "oracle-arg"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    transcript: list[str] = []
    try:
        conn, _ = listener.accept()
        conn.settimeout(10)
        with conn:
            init, init_text = recv_packet(conn)
            transcript.append(init_text)
            assert local_name(init.tag) == "init", init_text
            assert init.attrib.get("protocol_version") == "1.0", init_text
            assert init.attrib.get("language") == "AutoHotkey", init_text
            file_uri = init.attrib.get("fileuri", "")
            decoded_path = urllib.parse.unquote(urllib.parse.urlsplit(file_uri).path)
            assert pathlib.Path(decoded_path).resolve() == fixture, init_text

            feature, text = command_response(conn, "feature_set -n max_children -v 16", 1)
            transcript.append(text)
            assert feature.attrib.get("success") == "1", text

            bp, text = command_response(
                conn, f"breakpoint_set -t line -f {file_uri} -n 3 -s enabled", 2
            )
            transcript.append(text)
            assert bp.attrib.get("state") == "enabled" and bp.attrib.get("id"), text

            send_command(conn, "run -i 3")
            stopped, text = recv_packet(conn)
            transcript.append(text)
            assert stopped.attrib.get("command") == "run", text
            assert stopped.attrib.get("transaction_id") == "3", text
            assert stopped.attrib.get("status") == "break", text

            stack, text = command_response(conn, "stack_get", 4)
            transcript.append(text)
            first_line = stack_line(stack)
            assert first_line == 3, text

            names, text = command_response(conn, "context_names", 5)
            transcript.append(text)
            contexts = {e.attrib.get("name"): e.attrib.get("id") for e in names.iter() if local_name(e.tag) == "context"}
            assert contexts == {"Local": "0", "Global": "1"}, text

            context, text = command_response(conn, "context_get -c 1 -d 0", 6)
            transcript.append(text)
            assert property_value(context, "x") == "10", text

            prop_x, text = command_response(conn, "property_get -n x -c 1 -d 0", 7)
            transcript.append(text)
            assert property_value(prop_x, "x") == "10", text

            send_command(conn, "step_into -i 8")
            stepped, text = recv_packet(conn)
            transcript.append(text)
            assert stepped.attrib.get("command") == "step_into", text
            assert stepped.attrib.get("transaction_id") == "8", text
            assert stepped.attrib.get("status") == "break", text

            stack2, text = command_response(conn, "stack_get", 9)
            transcript.append(text)
            second_line = stack_line(stack2)
            assert second_line == 4, text

            prop_y, text = command_response(conn, "property_get -n y -c 1 -d 0", 10)
            transcript.append(text)
            assert property_value(prop_y, "y") == "15", text

            detach, text = command_response(conn, "detach", 11)
            transcript.append(text)
            assert detach.attrib.get("command") == "detach", text
            assert detach.attrib.get("status") == "stopped", text

        stdout, stderr = proc.communicate(timeout=10)
        assert proc.returncode == 0, (proc.returncode, stdout, stderr)
        deadline = time.monotonic() + 2
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        result = marker.read_text(encoding="utf-8").strip()
        assert result == "value=30", result
        summary = {
            "schema": 1,
            "result": "pass",
            "protocol": "DBGp/1.0",
            "init": True,
            "breakpoint_line": first_line,
            "step_line": second_line,
            "context_x": 10,
            "property_y": 15,
            "detach": True,
            "script_result": result,
            "packets": len(transcript),
        }
        args.summary.write_text(json.dumps(summary, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps(summary, sort_keys=True))
        return 0
    except Exception:
        proc.kill()
        stdout, stderr = proc.communicate(timeout=5)
        print("--- DBGp transcript ---", file=sys.stderr)
        for packet in transcript:
            print(packet, file=sys.stderr)
        print("--- runtime stdout ---", stdout, file=sys.stderr)
        print("--- runtime stderr ---", stderr, file=sys.stderr)
        raise
    finally:
        listener.close()


if __name__ == "__main__":
    raise SystemExit(main())
