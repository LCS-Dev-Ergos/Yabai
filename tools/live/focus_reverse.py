#!/usr/bin/env python3
"""Quick reversals: next three times, then prev three times, GAP ms apart,
from Desktop START (1 by default), RUNS times. Each run prints the Desktop
it ended on and whether the focused window is one of that Desktop's.

A failed run keeps the daemon's debug output of that run in
focus-reverse-fail-RUN.log; turn `yabai -m config debug_output on` on first
and off afterwards. See livetest.py for the input and restore rules.

Usage: focus_reverse.py RUNS GAP_MS [START]
"""

import json
import os
import sys
import time

from livetest import (
    Topology,
    active_space,
    focused,
    focused_window,
    frontmost,
    go_to,
    interrupted,
    now_ms,
    reply,
    request,
    restore,
    wait_for_idle,
)

LOG = f"/tmp/yabai_{os.environ['USER']}.out.log"


def main():
    runs, gap = int(sys.argv[1]), float(sys.argv[2])
    start = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    topology = Topology()
    started = wait_for_idle()
    original, window = active_space(), focused_window()

    try:
        for run in range(runs):
            if interrupted(started):
                raise KeyboardInterrupt("user input")
            if not go_to(start, topology):
                print(
                    json.dumps(dict(run=run, setup_ok=False, active=active_space())),
                    flush=True,
                )
                continue
            time.sleep(0.7)

            offset = os.path.getsize(LOG) if os.path.exists(LOG) else 0
            t0 = now_ms()
            handles = []
            for i, direction in enumerate(["next"] * 3 + ["prev"] * 3):
                delay = t0 + i * gap - now_ms()
                if delay > 0:
                    time.sleep(delay / 1e3)
                handles.append(
                    request(
                        "space", "--navigate", "focus", direction, "crossfade", "0.25"
                    )
                )
            for connection in handles:
                reply(connection)
            time.sleep(1.5)

            space, focus = active_space(), focused()
            ok = space == start and focus in topology.windows_of.get(space, set())
            print(
                json.dumps(
                    dict(
                        run=run,
                        space=space,
                        focus=focus,
                        front=frontmost(),
                        yabai_focus=focused_window(),
                        ok=ok,
                    )
                ),
                flush=True,
            )
            if not ok and os.path.exists(LOG):
                with (
                    open(LOG, errors="replace") as log,
                    open(f"focus-reverse-fail-{run}.log", "w") as out,
                ):
                    log.seek(offset)
                    out.write(log.read())
    except KeyboardInterrupt as error:
        print(f"aborted: {error}", flush=True)
    finally:
        restore(original, window)


if __name__ == "__main__":
    main()
