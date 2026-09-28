#!/usr/bin/env python3
"""Rapid Desktop navigation through the daemon socket, as skhd sends it.

Scenarios, all `space --navigate focus next|prev crossfade DURATION`:
  taps N GAP     N separate presses GAP ms apart (not key repeats).
  held MS        a held key: a request every 30 ms for MS milliseconds.
  reverse N GAP  N next, then N prev, GAP ms apart; ends where it started.
  back N M GAP   N next, then M prev, GAP ms apart: the prev presses take
                 back steps still queued behind one that reached its Desktop.

Each scenario starts on Desktop START and prints one JSON line: when each
request was sent, every change of the active Space (space_poll), the replies,
the Desktop it ended on against the one expected, and the focused window.
next and prev cycle through the Desktops of every display, as navigation does.
See livetest.py for the input and restore rules.

Usage: burst.py START DURATION SCENARIO ARGS... [-- SCENARIO ARGS...]
"""

import json
import subprocess
import sys
import time

from livetest import (
    Topology,
    focused,
    focused_window,
    frontmost,
    go_to,
    interrupted,
    now_ms,
    reply,
    request,
    restore,
    tool,
    wait_for_idle,
    active_space,
)


def parse(args):
    start, duration = int(args.pop(0)), args.pop(0)
    scenarios = []
    while args:
        name = args.pop(0)
        params = []
        while args and args[0] != "--":
            params.append(float(args.pop(0)))
        if args:
            args.pop(0)
        scenarios.append((name, params))
    return start, duration, scenarios


def plan_of(name, params, start, topology):
    if name == "taps":
        n, gap = int(params[0]), params[1]
        return [("next", i * gap) for i in range(n)], topology.wrap(start + n)
    if name == "held":
        return [("next", t) for t in range(0, int(params[0]), 30)], None
    if name == "reverse":
        n, gap = int(params[0]), params[1]
        plan = [("next", i * gap) for i in range(n)] + [
            ("prev", (n + i) * gap) for i in range(n)
        ]
        return plan, start
    if name == "back":
        n, m, gap = int(params[0]), int(params[1]), params[2]
        plan = [("next", i * gap) for i in range(n)] + [
            ("prev", (n + i) * gap) for i in range(m)
        ]
        return plan, topology.wrap(start + n - m)
    raise SystemExit(f"unknown scenario {name}")


def run(name, params, start, duration, topology):
    plan, expect = plan_of(name, params, start, topology)
    if not go_to(start, topology):
        return dict(scenario=name, params=params, setup_ok=False, active=active_space())
    time.sleep(0.7)

    poll = subprocess.Popen(
        [tool("space_poll"), str(plan[-1][1] / 1e3 + 6.0)],
        stdout=subprocess.PIPE,
        text=True,
    )
    time.sleep(0.1)
    t0 = now_ms()
    handles = []
    previous = None
    for direction, at in plan:
        # Separate presses stay apart even when one was sent late: the daemon
        # takes requests without a key release less than 75 ms apart for a
        # held key's repeats.
        due = t0 + at
        if previous is not None and at - previous[0] >= 100:
            due = max(due, previous[1] + 90)
        delay = due - now_ms()
        if delay > 0:
            time.sleep(delay / 1e3)
        previous = (at, now_ms())
        handles.append(
            (
                now_ms() - t0,
                request(
                    "space", "--navigate", "focus", direction, "crossfade", duration
                ),
            )
        )
    released = now_ms() - t0
    replies = [(round(at), reply(connection)) for at, connection in handles]
    time.sleep(max(0.0, (t0 + released + 5000 - now_ms()) / 1e3))
    poll.terminate()

    trace = [
        (round(float(ms) - t0), topology.index_of.get(int(sid), f"sid{sid}"))
        for ms, sid in (line.split() for line in poll.communicate()[0].splitlines())
    ]
    trace = [t for t in trace if t[0] >= -50]
    final = trace[-1][1] if trace else None
    focus = focused()
    return dict(
        scenario=name,
        params=params,
        setup_ok=True,
        requests=len(plan),
        sent=[round(at) for at, _ in handles],
        released_ms=round(released),
        changes=len(trace) - 1,
        final=final,
        expect=expect,
        last_change_ms=trace[-1][0] if trace else None,
        after_release_ms=round(trace[-1][0] - released) if trace else None,
        focus=focus,
        front=frontmost(),
        focus_ok=not topology.windows_of.get(final)
        or focus in topology.windows_of.get(final, set()),
        errors=[r for r in replies if r[1]],
        trace=trace,
    )


def main():
    start, duration, scenarios = parse(sys.argv[1:])
    topology = Topology()
    started = wait_for_idle()
    original, window = active_space(), focused_window()

    try:
        for name, params in scenarios:
            if interrupted(started):
                raise KeyboardInterrupt("user input")
            print(json.dumps(run(name, params, start, duration, topology)), flush=True)
    except KeyboardInterrupt as error:
        print(f"aborted: {error}", flush=True)
    finally:
        restore(original, window)


if __name__ == "__main__":
    main()
