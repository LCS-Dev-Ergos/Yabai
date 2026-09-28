# Effects investigation on lcs.11

Measured on 2026-09-27. Read this alongside the primary-source
[Apple motion research](../research/desktop-motion.md). This pass investigates
the current visual complaint; it does not change the production animator.

## Host and current policy

The active daemon is lcs.11, with payload `2.1.31-lcs.5 / 0x5D`.
The host is an M1 Pro with 16 GPU cores and two Dell U3223QE displays at 60 Hz.
`system_profiler` reports 6016×3384 pixels and 3008×1692 logical resolution
per display, with one rotated 90 degrees. The two reported pixel surfaces
total about 40.72 million pixels; this is not a measured repaint count or
proof of GPU saturation. Apple explicitly notes that scaled display modes
[may affect performance](https://support.apple.com/en-me/guide/mac-help/mchl86d72b76/mac).
Display modes were not changed during this investigation.

The installed wrapper requests alpha 0.9 and duration 150 ms. Window opacity
is enabled, with active alpha 1, normal alpha 0.975 and ordinary opacity
duration **zero**. The shared worker uses elapsed time and cubic ease-out,
up to 120 Hz, without display synchronization.

## Performance recheck

Forty relative requests sent directly to the socket at 30 ms intervals
drained 696 ms after the last send. Interleaving four space queries drained
901 ms, versus 7.18 seconds in the lcs.10 observation. All requests succeeded.
These are separate live runs, not an isolated causal speedup measurement;
the deterministic regression establishes the queue correction itself.

Sixteen absolute 7/6 switches at 150 ms intervals had a 100 ms median and
238 ms maximum command completion, no failures. Yabai used 0.02 CPU seconds;
WindowServer used 1.91 over 2.79 seconds, about 68% of one core. The idle
control used 0.40 over 2.66 seconds, about 15%. CPU samples and client replies
do not identify missed presentation deadlines or prove compositor saturation.
Apple distinguishes application commit hitches from render-server hitches;
both require frame/deadline evidence.
[Apple render-loop explanation](https://developer.apple.com/videos/play/tech-talks/10855/).

## Live opacity A/B/A

The probe alternated Desktop 6 (ChatGPT) and 7 (Edge), restoring the initial
Desktop/window afterward. A separate process read `SLSGetWindowAlpha` with
a requested 5 ms interval. Timestamps use `CLOCK_MONOTONIC` immediately after
each read. This observes WindowServer alpha state, **not presented frames**.
Individual read calls took as long as 95 ms; sampling can perturb the system
and cannot establish exact per-frame timing. Earlier traces also queried the
current Space; those were retained separately and superseded by alpha-only
traces for the comparison below.

The control temporarily disabled `window_opacity`, leaving the navigation
fade itself enabled at the same 0.9/150 ms. Its endpoints on the selected
focused windows remained 1. This also makes inactive windows opaque, so the
control is not an isolation of total compositor CPU. Configuration was restored
in a `finally` block and queried afterward (`window_opacity = on`).

| Condition | Switches | First observed endpoint after command reply | Median |
| --- | ---: | ---: | ---: |
| Existing focus opacity policy | 6 | 1.4–85.5 ms | 11.0 ms |
| Focus opacity disabled; navigation fade retained | 6 | 148.5–156.3 ms | 154.7 ms |
| Existing policy restored | 2 | 13.4–19.3 ms | 16.4 ms |

The normal runs exposed 0–7 sampled intermediate values; the control exposed
11–19. This is consistent with focus updates cutting off the navigation fade,
not just with an unattractive easing formula. It does not establish how many
of these values were scanned out by either monitor.

## Source-backed explanation

1. `space_navigation_run` dims the destination, switches Space, performs focus
   and any required AXRaise, and only then submits the recovery fades. Variable
   focus latency therefore postpones recovery; each submitted window also
   receives a separate epoch in `window_fade_set`.
2. `window_did_receive_focus` requests ordinary opacity through
   `window_manager_set_window_opacity`. With duration zero, this reaches the
   immediate branch of `window_fade_set`, which removes a pending fade and
   sets the endpoint. Activation/focus notifications can arrive just after
   navigation starts its recovery.
3. The worker then loses its opportunity to apply the intended easing curve.
   Changing cubic to spring, or merely lowering the timer rate, would leave
   this ownership conflict in place.

Source pointers: [navigation](../../src/space_navigation.c),
[focus handling](../../src/event_loop.c), [opacity dispatch](../../src/window_manager.c),
[fade state](../../src/osax/window_fade.c).

## Proposed implementation sequence

First separate the ordinary opacity target from a temporary navigation effect.
Repeated focus confirmation of the same target must not cancel or restart the
effect. Real target changes, explicit opacity commands and failed switches
still need deterministic cancellation and restoration to the latest target.
Preserve custom-opacity behavior and protect against stale generations.

Then give a navigation group one epoch and start recovery after the Space
switch without waiting for application focus work. Preserve the focus fix for
applications spanning displays. A Space-change notification is not a rendered
first-frame fence; do not hide that uncertainty behind an arbitrary delay.

Prototype display-linked pacing on macOS 14+ after the ownership tests pass.
For these monitors, target 60 Hz and skip late frames. A callback must not run
blocking focus operations or enqueue a backlog of alpha writes. Verify actual
presentation before interpreting callback alignment as visual synchronization.

Only then compare the existing cubic against bounded smoothstep for opacity,
with the same duration and workload. Consider a critically damped spring with
velocity continuity for a later spatial experiment, not opacity bounce.
These are design candidates justified in the research, not an Apple default.

## Reproduction and next measurement

Local probes and raw evidence are in ignored `build/`: `measure-repeat.py`,
`measure-space.py`, `observe-alpha.m`, `measure-alpha.py`, and
`lcs11-alpha-traces{,-without-focus-opacity,-restored}.json`.
`plot-alpha.py` produces `lcs11-alpha-comparison.png` and `.svg`.

Instruments is available with Animation Hitches and Metal System Trace
templates. A controlled trace or frame capture remains necessary to distinguish
application rendering, compositor work and presentation irregularity. The
alpha probe does not replace that check. Keep display-mode scaling and
persistent inactive transparency as separate future controls; coordinate
those measurements with any parallel performance work by Claude.
