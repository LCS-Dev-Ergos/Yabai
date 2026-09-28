"""Shared pieces of the live navigation tests in this directory.

They drive the installed daemon through its socket, as skhd does, so they
change the Desktop and focus on screen. Every test waits for five seconds
without keyboard or pointer input before it starts, stops at the next input,
and restores the Desktop and the focused window it started from.

The helpers space_poll and ax_focused are built with the tools
(YABAI_BUILD_TOOLS, the default) into build/<preset>/tools; YABAI_TOOLS
overrides the directory.
"""

import ctypes
import json
import os
import socket
import struct
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get("YABAI_TOOLS", ROOT / "build" / "debug" / "tools"))
SOCKET = f"/tmp/yabai_{os.environ['USER']}.socket"

_cg = ctypes.cdll.LoadLibrary(
    "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"
)
_since = _cg.CGEventSourceSecondsSinceLastEventType
_since.restype = ctypes.c_double
_since.argtypes = [ctypes.c_int32, ctypes.c_uint32]


def tool(name):
    path = TOOLS / name
    if not path.exists():
        raise SystemExit(f"{path} is missing: build the tools, or set YABAI_TOOLS")
    return str(path)


def input_idle():
    """Seconds since the last key, modifier or click."""
    return min(_since(1, event) for event in (10, 12, 1, 3, 25))


def wait_for_idle(seconds=5.0, limit=300.0):
    deadline = time.monotonic() + limit
    while input_idle() < seconds:
        if time.monotonic() > deadline:
            raise SystemExit("the user is active, not starting")
        time.sleep(0.5)
    return time.monotonic()


def interrupted(started):
    """True once there was input after `started` (a time.monotonic value)."""
    return input_idle() + 0.05 < time.monotonic() - started


def yabai(*args):
    return json.loads(subprocess.check_output(["yabai", "-m", *args], text=True))


def now_ms():
    return time.clock_gettime(time.CLOCK_UPTIME_RAW) * 1e3


def request(*tokens):
    body = b"\0".join(t.encode() for t in tokens) + b"\0\0"
    connection = socket.socket(socket.AF_UNIX)
    connection.connect(SOCKET)
    connection.sendall(struct.pack("i", len(body)) + body)
    try:
        # The daemon answers a request that joined a waiting one at once and
        # may have closed the socket already, as the yabai client allows.
        connection.shutdown(socket.SHUT_WR)
    except OSError:
        pass
    return connection


def reply(connection):
    connection.settimeout(5)
    chunks = []
    try:
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    except socket.timeout:
        chunks.append(b"<timeout>")
    connection.close()
    return b"".join(chunks).decode(errors="replace").strip()


def focused():
    """The window Accessibility reports focused in the frontmost application."""
    out = subprocess.check_output([tool("ax_focused")], text=True).split()
    return int(out[1]) if len(out) > 1 else 0


def frontmost():
    out = subprocess.check_output([tool("ax_focused")], text=True).split()
    pid = out[0] if out else "0"
    name = subprocess.run(
        ["ps", "-o", "comm=", "-p", pid], capture_output=True, text=True
    ).stdout.strip()
    return f"{pid} {name.rsplit('/', 1)[-1]}"


def active_space():
    return yabai("query", "--spaces", "--space")["index"]


def focused_window():
    try:
        return yabai("query", "--windows", "--window").get("id")
    except subprocess.CalledProcessError:
        return None


class Topology:
    """The Desktops navigation cycles through: every Desktop of every display,
    in Mission Control order."""

    def __init__(self):
        spaces = yabai("query", "--spaces")
        self.index_of = {space["id"]: space["index"] for space in spaces}
        self.windows_of = {space["index"]: set(space["windows"]) for space in spaces}
        self.count = len(spaces)

    def wrap(self, index):
        return (index - 1) % self.count + 1


def go_to(index, topology, timeout=2.0):
    """Switches to Desktop `index` without an effect and waits until it is the
    active Space. An application that makes its window on another display key
    (Chromium does) can leave that display active; then a window of the
    Desktop is focused directly. False if the Desktop never became active."""
    reply(request("space", "--navigate", "focus", str(index), "1", "0"))
    for attempt in range(2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if active_space() == index:
                time.sleep(0.3)
                if active_space() == index:
                    return True
            time.sleep(0.05)
        windows = sorted(topology.windows_of.get(index, ()))
        if attempt or not windows:
            break
        subprocess.run(
            ["yabai", "-m", "window", "--focus", str(windows[0])], check=False
        )
    return active_space() == index


def restore(index, window):
    reply(request("space", "--navigate", "focus", str(index), "1", "0"))
    if window:
        subprocess.run(["yabai", "-m", "window", "--focus", str(window)], check=False)
