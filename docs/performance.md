# Performance investigation

Measure client completion, daemon CPU and WindowServer CPU separately. Client
completion includes time in the event queue and waiting for response EOF; it
does not measure when a frame reaches the display. Percent CPU is expressed
relative to one core. Keep the same Desktop pair and workload for comparisons.

## Activated lcs.5

On 2026-09-26 the installed daemon was verified as lcs.5 and the loaded payload
as `2.1.31-lcs.3`, attributes `0x4D`. Sixteen navigation requests, 150 ms apart,
on spaces 5/4 completed without failures, with median client latency of
1.28 s and 1.39 s in two runs. Plain `space --focus` measured 1.10 s. Earlier
lcs.4 runs were 4.4–4.6 s, but system load differed between sessions, so this
is not an isolated estimate of the release's improvement.

WindowServer used 0.40 CPU seconds during a 2.66 s idle control, about 15% of
one core. Its earlier 82% idle observation did not recur in this control.
Neither result establishes that the user's intermittent 90% peak is fixed.

Stack sampling identified the focus path's explicit 40 ms delay, AX queries
in event handlers, and substantial time in `event_signal_flush`'s `fork`.

## Signal dispatch

A subsequent paired experiment on spaces 7/6 kept the same 16 requests at
150 ms intervals and temporarily removed only the `sketchybar_focus` signal:

| Focus signal | Median completion | Maximum | Daemon CPU seconds |
| --- | ---: | ---: | ---: |
| Enabled | 392 ms | 541 ms | 0.49 |
| Temporarily disabled | 41 ms | 67 ms | 0.03 |
| Restored | 200 ms | 557 ms | 0.44 |

All requests succeeded. The original signal, Desktop and window were restored.
This comparison isolates the hook's combined dispatch/action cost, not the
cost of `fork` alone. These runs used blocking `waitpid` via Python; preceding
runs used timeout polling, which adds up to approximately 50 ms of timing
quantization. The changed Desktop pair also prevents direct comparison.

The old dispatcher forks a copy of the daemon, then forks each matching
action. Accepted client sockets lack close-on-exec, so those actions can
keep other pending requests open after the daemon has answered them. A test
using the production dispatcher, a queued socket pair and a one-second
action reproduced this: EOF was absent after 100 ms. It passed after the fix.
A separate live probe also observed response bytes preceding EOF, though
waiting for the first response byte remained the larger cost in that run.

The dispatcher now uses `posix_spawn` without waiting for the action to exit.
`POSIX_SPAWN_CLOEXEC_DEFAULT` closes daemon resources in the child; explicit
inheritance preserves stdin/stdout/stderr. Event variables go through the
existing `/usr/bin/env sh -c` convention without mutating the daemon's
environment. Subscribers and filters are retained; events are not dropped.
See Apple's [spawn flag documentation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/man/man3/posix_spawnattr_setflags.3).

Local regression tests are not live acceptance of the candidate. Repeat the
paired workload with the focus hook enabled after activating the release.

## Remaining work

Profile again before changing the focus workaround's 40 ms delay: it exists
for application compatibility. Title queries made for signals without title
filters and repeated AX lookups in space-change handlers are further targets.
Keep focus correctness, event delivery and multi-display behavior in scope.

The current fade worker is time-based but not synchronized to display refresh.
Display-linked pacing and WindowServer-side opacity interpolation remain
investigations; see [effects](effects.md). Removing avoidable IPC, process
creation and AX work is measurable without changing the compositor. No
Hyprland-equivalent frame latency or rendering quality has been established.
