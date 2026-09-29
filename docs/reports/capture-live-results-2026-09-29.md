# Capture replacement: live results and next decision

**Latest evidence:** The [extended campaign](capture-extended-results-2026-09-29.md) completed the longer memory, GPU, first-Space and portrait measurements. The follow-up status below is historical and superseded.

**Follow-up on the same date:**
[capture-decision-data-2026-09-29.md](capture-decision-data-2026-09-29.md)
supersedes the next-experiment plan below. The longer 50-effect client-alive
gate stopped at 20 on a conservative WindowServer memory threshold. A later
short block showed an even larger transient peak that returned to baseline
while the daemon remained alive, and an exact-configuration headless control
remained bounded for 30 captures. The long gate and first-Space/GPU acceptance
are still open; do not extrapolate the 12-effect pass below to them.

Date: 2026-09-29. Candidate `2131b94`, based on `9a56727`, branch
`lcs-code/crossfade-capture-retention`. The user authorized local unsigned
validation and continuing work in this worktree. No release was created.

## Decision

Retain the focused capture replacement. It passes the bounded client-alive
memory regression and the tested visual/navigation cases. It is not yet an
unqualified multi-display, HDR, cursor or long-duration acceptance.

The next performance experiment should split the interval between prepared
snapshot and first presented change. The measured preparation copy is not the
whole latency problem; a GPU rewrite is premature. Separately isolate cold
Space reconciliation. Keep renderer experiments out of the memory-fix patch.

## Reproducible setup

- Worktree `/Users/lcs-dev/.codex/worktrees/4953/Yabai`.
- Release configured in `build/live-release` with
  `-DYABAI_ALLOW_UNSIGNED_LOCAL=ON -DYABAI_LINK_COMPILE_COMMANDS=OFF`.
- Candidate SHA-256:
  `b4bc68c8319503020ac4ba3b1615178f460ead41c013815528c0c01d4b5aafef`.
- Snapshot, scheduling and queue CTests passed 3/3 in this Release build.
  Previous agent's Debug and targeted sanitizer evidence remains separate.
- Temporary matching local payload `2.1.31-lcs.14`; private daemon/payload
  sockets. The test config copied the ordinary user's config and omitted only
  its automatic installed-payload reload commands. Candidate clients were
  resolved first in the test environment; ordinary on-disk config was unchanged.
- macOS 27.2, WindowServer PID 453 throughout. Both external displays connected:
  landscape 3008x1692 logical / 6016x3384 captured pixels; portrait display at
  x=3008, y=-660, 1692x3008 logical. Captured transition tests used the landscape
  display only. Space 2 contained VS Code; Spaces 3/4 were empty. Activity Monitor
  windows were open on Space 11 of the portrait display.
- Three bounded temporary sessions: memory PID 49580, visual/bursts PID 50254,
  repeated A/B PID 50902. Signed daemon/payload restored after each session.
- Native administrator authorization loaded each pair; replacing it restarted
  Dock. No WindowServer restart, SIP change, Nix switch or production install
  of the candidate occurred.

Raw artifacts remain ignored under `build/live-validation/`: `run.py`,
`run-visual.py`, `run-ab.py`, configuration copies, initial topology, JSONL,
`signposts.log`, `visual-session/`, `ab-session/`, `memory-summary.json`,
`analyze-presented.py`, `ab-summary.json`. These runners are host-specific
session fixtures, not CI tests or a portable installer. They restore the
signed service/payload in `finally`; native authorization can still be required
for restoration, and abrupt process death is outside that guarantee.

Run the fixtures only during a coordinated live interval; they change Spaces
and restart Dock. The portable analysis command after recording is:

```sh
python3 build/live-validation/analyze-presented.py
```

The first invocation failed before changing the service because this tool's
process environment lacked `USER`; the runner now fills the actual login name
from `getpwuid`. This is a harness environment issue, not candidate behavior.

## Memory gate: passed within the measured workload

After warming Spaces 3/4, six effect-free switches were followed by twelve
isolated 250 ms crossfades. Every destination was correct. The latter emitted
12 `snapshot: ready` and zero `no capture` events, excluding a false pass caused
by omitted effects.

| Checkpoint | WindowServer top MEM |
| --- | ---: |
| Initial warmed sample | 2080 MiB |
| After six effect-free switches and 8-second hold | 1981 MiB |
| During twelve crossfades | 1992–2092 MiB |
| After twelve crossfades and 8-second hold, daemon alive | 1981 MiB |

This lacks the previous approximately 80 MiB-per-capture cumulative slope.
A later planned 20-second hold was interrupted by user input after about seven
seconds; the completed eight-second hold is the acceptance evidence. Restoration
preserved the user's new focus. Do not report a completed 20-second hold.

Across the three sessions there were 26 ready snapshots and zero missing-capture
signposts. The daemon lifetime was reset between sessions: 26 is not a continuous
26-capture retention trial. The last A/B session ended at 1890 MiB after a
10-second hold; differences between sessions are not attributed to the patch.

## Capture and preparation timing

For the twelve crossfades without a concurrent screen recorder:

| Phase | Min | Median | Max |
| --- | ---: | ---: | ---: |
| Capture | 67.7 ms | 75.0 ms | 82.7 ms |
| Preparation after capture | 22.8 ms | 24.7 ms | 27.6 ms |

The existing `prepare` signpost is cumulative from request start. The second
row subtracts capture from that cumulative value. It includes event-loop
resumption and overlay setup/drawing/ordering; it does not isolate memcpy or
GPU execution time. The subsequent one-refresh sleep, Dock request, focus work
and presentation are outside that row. CLI return was only about 10–21 ms
because navigation proceeds asynchronously; it is not an effect latency metric.

## Presented frames and burst correctness

Empty 3→4: bar excursion 0.01/255, bright-icon loss 0.011/255, full-Desktop
excursion 0.01/255. The automated check passed, and baseline/intermediate/final
PNGs retained the Desktop icons and bar on inspection. The metric samples a
bright-content mask and does not independently prove every icon pixel.

Initial non-PNG 2→3 and 3→2 captures passed monotonic blending: zero measured
backtrack/excursion, 11 and 9 intermediate frames respectively. This detects
large regressions; repeated progress values still occur, so it does not promise
perfect frame pacing or every-refresh updates.

| Scenario from Space 2 | Final / expected | Time from last request to last Space change | Focus |
| --- | --- | ---: | --- |
| Five presses, 100 ms apart | 7 / 7 | 216 ms | Correct empty Space |
| Three next, three previous, 100 ms apart | 2 / 2 | 6 ms | VS Code window 3873 |
| Three presses, 450 ms apart | 5 / 5 | 154 ms | Correct empty Space |

No command errors. Fast bursts still prepared their first snapshot, then used
zero-duration steps. This is evidence to investigate admission/cancellation
of work already in progress, not proof all of that first capture was avoidable.

## Repeated presentation A/B

Three trials per direction and mode, identical recording method, no PNG output.
The central RGB projection onto the settled source→destination difference
reached the following thresholds after the request timestamp:

| Direction / mode | First ≥5% change, three trials | First ≥95% change, three trials |
| --- | --- | --- |
| 2→3, no effect | 33.3, 42.0, 40.2 ms | 33.3, 42.0, 40.2 ms |
| 2→3, crossfade 250 ms | 286.5, 267.1, 291.5 ms | 469.8, 450.4, 458.2 ms |
| 3→2, no effect | 35.9, 34.3, 43.0 ms | 35.9, 34.3, 43.0 ms |
| 3→2, crossfade 250 ms | 274.9, 280.9, 248.1 ms | 458.3, 430.9, 431.5 ms |

All bar checks passed. Crossfades contained 6–10 intermediate complete frames
in these repeats. About 60 Hz recording quantizes observation, and recording
itself adds compositor work. Fixed ordering (none then crossfade), small sample
size and active apps limit generalization. These are observed ranges, not p95
or confidence intervals. Threshold timing includes the easing curve and cannot
be called raw capture delay. Still, the approximately 205–253 ms paired gap to
5% change is much larger than the measured preparation phase alone.

Cold `SPACE_CHANGED` events of 403.8 ms and 416.4 ms occurred on the first VS Code
visit after separate daemon restarts. Existing whole-handler timing does not
identify the AX/window-query operation responsible. Attributing it to a global
application scan would be wrong: source refreshes only unresolved applications.

## Next experiments, in order

1. Add opt-in phase timing around callback arrival/event-loop consumption,
   context drawing/flush, overlay Space creation/order, the pre-switch wait,
   Dock request/reply, first alpha write and teardown. Record only a few phase
   events, not every frame, and correlate them with presented frames. Split cold
   SPACE_CHANGED into refresh, validation and layout flush in the same diagnostic
   build. No scheduling or rendering behavior changes in this experiment.
2. Compare the instrumented candidate against this baseline to bound observer
   overhead. If repeated server calls or queued callback handling dominate,
   optimize that path first. If capture latency dominates, compare on-demand
   alternatives with identical output configuration; no permanent stream yet.
3. Measure WindowServer and daemon CPU-time deltas over matched no-effect,
   isolated-crossfade and fast-burst workloads, with idle control; add bounded
   GPU/energy sampling separately if available. Current data establishes memory
   and latency, not reduced CPU/GPU energy consumption.
4. Bound unresolved capture callbacks as a separate robustness patch. Preserve
   logical navigation and safe late callback cleanup. Do not conflate it with
   the now-avoided legacy-API retention trigger.
5. Consider one on-demand buffer/GPU prototype only if the phase data suggests
   material savings after capture. Compare it with the same output dimensions,
   focus semantics, timing and visual gates. A lower-resolution effect is an
   optional quality/performance experiment, not a default change.

Remaining acceptance: longer uninterrupted daemon lifetime; moving cursor;
portrait-display capture, HDR/color fidelity and display reconfiguration;
application-heavy workloads and CPU/GPU cost. No such coverage is implied here.

## Restored state

Final signed daemon PID 51294, `/opt/yabai/bin/yabai`, SHA-256
`8968238444932a59f078c91eb6f8cb2a6d7c843f21a6ea0e9e59a829466f5283`.
Installed payload and live handshake: `2.1.31-lcs.13 / 0x5D`.
Original Space 11 and Activity Monitor focused window 3414 restored and verified.
WindowServer remained PID 453. No temporary local daemon remains running.
