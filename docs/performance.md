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

Local regression tests do not establish live latency. The activated lcs.7
measurements below include this signal change and Claude's Desktop-move fix.

## Activated lcs.7 and focus events

The next switch installed lcs.7, with payload `2.1.31-lcs.4` / `0x5D`.
With the focus hook enabled, 16 navigation requests at 150 ms intervals on
spaces 7/6 measured 1.107 s and 261 ms median in two runs; direct focus measured
249 ms. All succeeded. Daemon CPU was 0.03–0.05 s, but WindowServer used
1.96–2.41 s during the navigation trials. An idle control used 0.15 s over
2.65 s (about 6% of one core). Lower daemon CPU did not establish lower latency
or better perceived fluidity; the user reported little visible improvement.

A five-second sample during 24 switches located most event-loop waiting in
the activation handler's AX focused-window lookup (1,145 of 4,095 samples),
the space-change handler (453), and the explicit focus delay (366). Signal
dispatch accounted for 32 samples. Sampling counts describe this workload;
they are not independent timing measurements or predicted speedups.

The follow-up reuses a pending AX focus notification when it already names a
known, valid, visible, non-minimized window of the activated application.
Otherwise it preserves the AX lookup. Handling an older window notification
cannot clear a newer observation, and observations are consumed rather than
retained as an indefinite cache. Stale application activations still deliver
their activation signals but no longer query/apply a background app's focus.
Space-change events read the active display's space from WindowServer instead
of asking the foreground application's AX window. The 40 ms compatibility
delay is unchanged. The new `focus_tests` regression failed on redundant AX
lookup before the fix and exercises observation validity and fallback paths.
Its effect on the live workload still requires activation and measurement.

The user also reported intermittent navigation from the last occupied Desktop
back to the first, skipping empty Desktops. Two controlled `next` sequences
from Desktop 7 traversed 8, 9, 10, 11, 1; 11 belongs to the second display.
That did not reproduce the reported skip. The user confirmed it also happens
with well-spaced individual key presses, so it is not confined to key repeat.
Keep focus/display changes in the investigation; this change does not claim
to fix it.

## Activated lcs.9: order and key repeat

On 2026-09-26 two displays were attached: Desktops 1–10 on the main display
and Desktop 11 on the second, where Edge also had a window. Walking `next`
from Desktop 1 at roughly 0.9 s intervals went from Desktop 7, which held an
Edge window, back to Desktop 1 in both runs. A 5 ms probe of WindowServer
showed the active menu-bar display switching to the second display within
250 ms of reaching Desktop 7, so `next` counted from Desktop 11 and wrapped.
In a separate run, yabai reported Edge's window on the second display as
focused 0.8 s after the switch. Focusing the Desktop 7 window with
`window --focus`, which raises it through Accessibility, kept focus and the
active display there.

Isolated navigations completed in 63–278 ms. WindowServer used 5.3–5.5 CPU
seconds over 12.3–12.5 s runs, the daemon 0.06–0.07 s. A five-second control
without navigation measured 1.9 s, but the user was active and a video was
playing, so it does not isolate the cost of switching.

A 1 ms sample of 13 navigations kept the event loop busy for 2.2 s, about
170 ms per navigation, mostly waiting on WindowServer and applications:

| Work | Samples |
| --- | ---: |
| Navigation request, 549 of them posting focus events to the application | 898 |
| Activation handler's AX focused-window lookup | 706 |
| Space-change window checks | 169 |
| Focus opacity requests and window space lookups | 229 |
| Signal process creation | 70 |

The key repeat interval was 30 ms, so a held key queued several requests per
completed switch, and switching continued after the key was released. The
Dock payload was busy for about 100 ms in total.

The follow-up merges relative requests that arrive while one waits, counts
from the Desktop the last navigation switched to, raises the destination
window when its application has a window visible on another display, and
lets the activation handler reuse the window navigation focused. See
[navigation](navigation.md). The Dotfiles `sketchybar_focus` signal triggered
a SketchyBar event that no item subscribes to. None of this has been measured
live yet.

## Activated lcs.10: navigation verification and interleaved queries

On 2026-09-27 the active daemon was lcs.10. A full `next` lap followed by a
`prev` lap traversed all 11 Desktops in order: 22/22 checks passed, including
7–8, with unchanged Desktop IDs and display assignments. After each command,
the probe waited 850 ms before checking focus. Median client completion was
119 ms, maximum 664 ms. This verifies the observed order at those checkpoints,
not frame timing or every possible focus transition.

Sixteen absolute navigations between Desktops 7/6 at 150 ms intervals measured
572 ms and 194 ms median in two runs, with no failed requests. Daemon CPU was
0.04/0.03 seconds; WindowServer CPU was 2.39/2.41 seconds over 3.05/2.84 seconds
(78–85% of one core). The idle control used 0.66 WindowServer CPU seconds over
2.66 seconds (25%). Absolute selectors deliberately are not merged. These
runs do not establish a uniform latency reduction or elimination of the CPU
peak reported by the user.

Two 40-request runs through the installed wrapper at 30 ms intervals drained
0.91/1.61 seconds after the last submission. A separate probe sent the same
valid navigation frames directly to the daemon socket, avoiding process
creation. Forty requests at 30 ms intervals drained in 238 ms. Interleaving
four `query --spaces --space` requests increased this to 7.18 seconds. Both
runs completed without command errors; maximum send lateness was 143/59 ms.
An earlier, longer interleaved run exceeded the probe's 12-second deadline.
Merged client replies acknowledge queuing, so their median is not a measure
of switch latency.

The production queue reproduced the ordering problem in a deterministic test:
a query closed the one tracked group; subsequent navigation was no longer
eligible to merge until that first group ran. The fix tracks ordered groups,
allowing later repeats to merge behind the query while preserving its position.
The regression failed before the change and passed afterward. Coverage also
checks direction cancellation, descriptor reuse, allocation failure and 4,000
concurrent admissions with a separate consumer. Debug and ASan/UBSan suites
and the queue's ThreadSanitizer test passed. Live improvement awaits activation.

The spaced-navigation sample still recorded 197 samples inside the check for
another visible window of the destination application, largely in repeated
WindowServer visibility lookups. That is a further candidate for reducing IPC;
this queue correction does not alter the focus or visibility checks.

## Activated lcs.13 as an interactive agent

The lcs.13 switch also gave the launchd agent `ProcessType = Interactive`, as
yabai's own service file does, and set `window_opacity_duration` to zero in
Dotfiles. Without a ProcessType, launchd throttles an agent's CPU and I/O:
before the switch every yabai thread ran at priority 20, against 37 for skhd and
SketchyBar; afterwards the agent reports spawn type `interactive (4)`.

On 2026-09-27, four conditions on Desktops 6/7 alternated the focus opacity
duration 0/0.1/0.1/0 s. Each ran 12 isolated switches 0.8 s apart through the
installed wrapper, a 16 × 150 ms burst and a matched idle interval:

| Duration | Isolated median / max | Burst drain | WindowServer CPU, burst / idle |
| --- | ---: | ---: | ---: |
| 0 s | 83 / 145 ms | 100 ms | 2.34 / 0.38 s |
| 0.1 s | 73 / 247 ms | 135 ms | 2.23 / 0.38 s |
| 0.1 s | 71 / 244 ms | 107 ms | 2.45 / 0.45 s |
| 0 s | 108 / 344 ms | 106 ms | 2.19 / 0.33 s |

Burst and idle intervals lasted about 2.8 and 3.3 s. The duration made no
measurable difference; zero only avoids work. The last burst request finished
100–135 ms after it was sent, so the burst no longer built a queue. The burst
medians of this run are not reported: the probe reaped clients late. A later
run with corrected reaping, while WindowServer used 33% of a core at idle,
measured 239 ms isolated median, 428 ms burst median, 143 ms drain, and 0.86 s
and 1.93 s to drain 40 relative requests at 30 ms without and with interleaved
queries. Load varied between runs; none of this isolates one change.

A 1 ms sample of eight isolated switches placed 434 samples in the navigation
request, about 54 ms each. The cross-display raise check took 196: 104 asking
the Space of each window of the destination application, 81 resolving their
displays. `SLSCopyManagedDisplayForSpace`, behind `space_display_id`, fetches
WindowServer's state of every Desktop on each call; the destination's display
lookup took another 53 samples and selector parsing 8. Focusing the window took
135, the scripting-addition requests 39. In Dock, alpha writes took 14 samples
in total, so the fade worker is not a hot spot with one window per Desktop.

The follow-up answers these lookups from one `SLSCopyManagedDisplaySpaces` reply
per request and lists the application's windows on the Desktops visible on other
displays with one query. A navigation with nothing to dim also skips the Dock
round trip that only cancels effects, once none can still run. Raw evidence is
in ignored `build/lcs13-measurements-*.json` and `build/lcs13-isolated-*.sample.txt`.

## Remaining work

Profile again before changing the focus workaround's 40 ms delay: it exists
for application compatibility. Signal titles already come from `window->title`
through `window_title_ts`; the earlier suggestion of removing AX title queries
there was incorrect. Keep focus correctness, event delivery and multi-display
behavior in scope when measuring the focus-event changes.

The current fade worker is time-based but not synchronized to display refresh.
Display-linked pacing and WindowServer-side opacity interpolation remain
investigations; see [effects](effects.md). Removing avoidable IPC, process
creation and AX work is measurable without changing the compositor. No
Hyprland-equivalent frame latency or rendering quality has been established.
