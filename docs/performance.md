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

## Display configurations on lcs.14

On 2026-09-27, after a restart, the same workload ran with the MacBook display
alone (120 Hz, 4112×2658 backing), one Dell alone (landscape 6016×3384 or
rotated 3780×6720, 60 Hz) and both Dells (6016×3384 each), with only kitty and
VS Code open. Desktops 3/4 both held VS Code. GPU figures are IOAccelerator
device utilization sampled every 150 ms:

| Setup | Idle WindowServer / GPU | Isolated median | Burst drain | 40 × 30 ms drain | Burst GPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| MacBook | 5% / 1% | 70 ms | 75 ms | 31 ms | 37% |
| Landscape Dell | 19% / 26% | 64 ms | 74 ms | 44 ms | 57% |
| Rotated Dell | 19% / 22% | 64 ms | 99 ms | 18 ms | 58% |
| Both Dells | 7% / 1% | 89 ms | 72 ms | 119 ms | 47% |

No setup built a queue. With both displays the request took about 12 ms more,
mostly WindowServer delivering the focus event records more slowly. The
40 ms same-application focus delay was 77% of the request on every setup.

With Edge, ChatGPT and TIDAL also open, on both displays: isolated 6/7 median
53 ms, burst drain 153 ms (maximum 304 ms), 40 × 30 ms drain 323 ms, and
WindowServer 76% of a core during the burst, when VS Code used 147% and ChatGPT
120%. A `next` lap from Desktop 1 to 11 on the other display and a `prev` lap
back landed correctly on all 20 steps. Empty Desktops took 13–20 ms, Edge's
134–254 ms: Edge also had a window on the other display, so navigation raised
through Accessibility (304 of 1,259 busy samples). The cross-display raise
query took 218, 151 of them classifying windows. The lcs.15 follow-up skips
that query when no other window could take focus, reads only window numbers,
and defers the same-application activation instead of sleeping.

Two displays alone did not reproduce the slowdown; applications repainting
large backing surfaces did. Raw evidence is in ignored `build/displays-*.json`
and `build/displays-*-yabai.sample.txt`. Lower-resolution modes remain to be
compared.

## Activated lcs.16 and lcs.17

On 2026-09-27 lcs.16 ran with payload `2.1.31-lcs.7` / `0x5D` and the main
display at "looks like 2560×1440". Two kitty windows showed intermediate alpha
values at the same timestamps. That does not show that transactions set other
applications' window alpha: after the first frame, these payloads wrote each
window on its own (see [effects](effects.md)).
A fade trace wrote its first frame about 2 ms after the start, then stood still
for 54 ms: Dock's main thread delivered the display callbacks late after the
switch, and the display-paced watchdog was 50 ms. lcs.17 writes a frame from
the worker half a frame late instead.

A focus check per Desktop pair runs 20 slow alternations and 5 bursts of 8
requests 30 ms apart, and reads the frontmost application's Accessibility focus.
On lcs.16, 3/4 (two VS Code windows) ended wrong in 1 of 5 bursts (4 of 5 on
lcs.15), with a 206 ms median time to focus; 7/8 (two Edge windows) had no
misses at 220 ms. Four `next`/`prev` requests 120 ms apart across Desktops 5–9
ended short in 2 of 6 sequences through `space.sh` and 2 of 10 straight to the
socket, every request succeeding. With "switch to a Space with open windows"
disabled, 6 of 10 did. The debug log showed Edge reactivating itself on its
windows after later navigation and pulling the main display back.

lcs.17 ran with payload `2.1.31-lcs.8` / `0x5D` and the main display back at
3008×1692 (20.4 Mpx per display):

| Check | Result |
| --- | --- |
| Edge sequences, `space.sh` / socket | 8/10 and 9/10 correct |
| Focus 3/4 | no misses, 231.5 ms median |
| Focus 7/8 | every step to Desktop 7 left focus on Edge's Desktop 8 window |
| Fade 0.7/250 ms on 6/7 | first two switches: 62–67 and 39 ms gaps; next four: 16 values about 16.7 ms apart |
| Workload 6/7 | isolated 53 ms median, burst drain 74 ms, lap 20/20 at 59.5 ms, 40 repeats 284 ms |

All three short sequences went forward from 5 toward the empty Desktop 9 and
ended on Edge's Desktop 7 or 8; every backward sequence, which ends by
activating VS Code, was correct. An intermediate step's late Edge activation is
only undone when the final Desktop activates a window of its own.

Without the synthesized click, activating Edge while it is already active does
not move its key window. A replay from Desktop 8 to 7 left focus on the Desktop
8 window in 1 of 3 trials with activation and AXRaise, and in none of 3 with the
order of `window --focus`. The following release keeps that order for an
application that is already active.

WindowServer used 44% of a core at rest and 76% during the burst; ChatGPT,
VS Code and Edge used 78–131% of a core during bursts and repeats. Alpha reads
are WindowServer state, not presented frames. The visible flash of the 0.7
fade is photometric: the destination windows pass 30% of the wallpaper, whose
bright orange cloud turns VS Code's `#1a1b26` background brown-orange before
it darkens again; at 0.9 they passed 10%. Raw evidence is in ignored
`build/lcs17-*.log`, `build/displays-lcs17-3008-apps.json` and the probes next
to them.

## Activated lcs.19: the crossfade that never ran

On 2026-09-27 lcs.19 ran with payload `2.1.31-lcs.9` / `0x5D`, pacing on. A
live check of the crossfade (`crossfade-check.py`: one switch, a return
midway, three stacked Desktops, four paced `next`) found none: every request
left its destination at alpha 0 and level 1, and the Desktop switched without
an effect. Dock logged no crossfade signpost. In a client process,
`SLSTransactionCommit` returned the transaction's address plus `0x48` in 3 of 3
empty commits, and Dock never uses the value: the payload had read it as a
refusal after committing, and the daemon's fallback switched instantly. The
four Desktops involved (6 to 9) were black when shown. Neither a native
Control-number switch nor Mission Control restored them, nor could a client
connection set their alpha; a Dock restart was not tried. lcs.20 checks no
commit and restores such Desktops when the payload loads.

The paced `next` steps began 255, 372 and 262 ms apart; each step itself took
34 to 100 ms. The first waited, as designed, until 150 ms after the activation
of ChatGPT that ended the step before it. The second and third began 272 and
162 ms after they were due at the 100 ms rhythm: something else held the event
loop. lcs.20 adds a signpost for every event handled in more than 10 ms, and
each step reports how late it ran. Raw evidence is in ignored
`build/lcs19-*.log`.

## Activated lcs.20: first live crossfades

On 2026-09-27 lcs.20 ran with payload `2.1.31-lcs.10` / `0x5D`, pacing on. When
it loaded, the payload restored the four Desktops lcs.19 had left transparent
("restored 4 desktops"); every Desktop read alpha 1 and level 0. The crossfade
check passed all four scenarios: during a crossfade three wallpaper windows
were on screen (the source stays shown below the destination), the
destination's alpha rose, a return midway faded it back out, three Desktops
stacked, four paced `next` ran as four crossfades, and each ended with every
Desktop at rest and one shown per display. Dock's signposts put the frames
16.7 ms apart while display callbacks arrived and 25.6 ms apart when they came
late, sometimes for a whole crossfade: the worker waited half a frame before
each late frame.

`effect-ab.py` recorded display 3 with ScreenCaptureKit while switching
between Desktops 3 and 5 (VS Code), 6 and 7 (ChatGPT, Edge) and 7 and 8 (Edge),
four switches per pair and effect, 1.2 s apart through the socket. For the
centre of the screen it reports when the first changed frame appeared, when
the last one did, and how far luminance or any colour channel went outside
the range between the two Desktops (0-255):

| Effect | First change (median) | Last change (median) | Gaps over two frames | Worst channel excursion |
| --- | --- | --- | --- | --- |
| None | 30.8 ms | 30.8 ms | 0 of 12 | 0.2 |
| Window fade, 0.7, 250 ms | 38.7 ms | 224.3 ms | 2 of 12 | 15.3 |
| Crossfade, 250 ms | 95.1 ms | 249.3 ms | 2 of 12 | 11.3 |

The crossfade's first change comes later partly by design, since smoothstep
starts slowly, and partly because WindowServer sometimes presented nothing for
100 to 200 ms after the switch while Dock kept writing frames on time; alpha
reads from a client blocked for up to 280 ms meanwhile. Its excursion, like the
window fade's, is the destination's wallpaper showing through its windows:
WindowServer applies a Desktop's alpha to each window. With Desktop 7's
wallpaper window at alpha 0 during the crossfade from 6, two switches stayed
within both Desktops on every channel, against 8.6 and 10.9 without it
(`build/lcs20-wallpaper-experiment.log`). lcs.21 hides the destination's
wallpaper this way, and writes one frame per interval while callbacks are
late.

`edge-burst.py` ended 10 of 10 socket sequences and 7 of 10 through
`space.sh` on the right Desktop, with the right focus. All three misses went
forward to Desktop 9, now empty again, and ended on Edge's 7 or 8. Signposts
and Dock's log show the cause: with presses 120 ms apart, the step to 7 was the
last one queued when it ran, so it activated Edge; Edge confirmed focus, the
following steps ran, and about 200 ms later Dock logged "switching to space 6
for window(d1c) ... ordered on non-visible space" and switched back to 7.
`AppleSpacesSwitchOnActivate` is already off; Dock also reads
`workspaces-auto-swoosh`, which is unset here. `focus-check.py` measured 186.5
ms median focus for 3/4 with one of five bursts ending on the wrong window, and
239.5 ms for 7/8 with none: the lcs.18 fix holds.

The daemon's event loop was held by single events for up to 512 ms (a
navigation step raising Edge), 212 ms (`APPLICATION_FRONT_SWITCHED`), 301 ms
(`SPACE_NAVIGATION_FOCUS`) and 577 ms (`SPACE_CHANGED`, during the user's own
navigation). A paced step ran 186 ms late behind two of them. A 1 ms profile
of ten Edge switches spent the loop's time in WindowServer queries (the
visibility snapshot, window lists per Desktop, a window's Desktop) and in
`AXRaise`, most of it waiting for replies. Raw evidence is in ignored
`build/lcs20-*.log`.

## Activated lcs.21

On 2026-09-28 lcs.21 ran with payload `2.1.31-lcs.11` and `space.sh`
requesting `crossfade 0.25`. Two crossfades from Desktop 6 to 7 stayed within
both Desktops on every channel, moved monotonically and presented a frame
every 16.7 ms. First and last visible change, two runs per duration: 150 ms
63-130 and 147-163 ms (one run shrank to 2 frames behind a WindowServer
stall), 200 ms 59-96 and 193-196 ms, 250 ms 73-81 and 231-240 ms. Edge bursts
ended 6 of 6 right through `space.sh` and 6 of 6 through the socket, with no
"ordered on non-visible space" switch in Dock's log. The user reports that
SketchyBar briefly disappears during a crossfade to an empty or an Edge
Desktop; not measured yet. Raw evidence is in ignored `build/lcs21-*.log`.

## Remaining work

The 40 ms same-application focus delay is kept for application compatibility
but no longer blocks the event loop. Signal titles already come from `window->title`
through `window_title_ts`; the earlier suggestion of removing AX title queries
there was incorrect. Keep focus correctness, event delivery and multi-display
behavior in scope when measuring the focus-event changes.

The current fade worker is time-based but not synchronized to display refresh.
Display-linked pacing and WindowServer-side opacity interpolation remain
investigations; see [effects](effects.md). Removing avoidable IPC, process
creation and AX work is measurable without changing the compositor. No
Hyprland-equivalent frame latency or rendering quality has been established.
