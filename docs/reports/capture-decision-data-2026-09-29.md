# Capture replacement: decision data after the focused fix

Date: 2026-09-29. Worktree `lcs-code/crossfade-capture-retention` at
`ad3826e` before these diagnostics; capture fix `2131b94` on `9a56727`.
The user authorized one measurement campaign with a temporary unsigned
daemon/payload and restoration. The first long runner stopped on a fixture
error; a guarded continuation and a final small block were needed. No release
or permanent install occurred.

## Decision state

Keep the focused capture fix as a candidate. The earlier 12-crossfade memory
gate passed, and the current 30-request headless control with the candidate's
exact public-API configuration stayed bounded. The planned 50-crossfade
client-alive gate **did not complete**: after 20 effects a conservative
WindowServer `top MEM` threshold stopped new work. A later short block showed
a larger peak that fell back to baseline while the daemon remained alive.
These peaks do not establish persistent retention, and the headless control
does not isolate overlay, Dock, driver or workload effects. The long gate is
open, not a pass or a causal failure verdict.

The primary measured preparation cost is an approximately 18 ms image draw.
The 72 ms capture, 21 ms deliberate preswitch wait and variable Dock roundtrip
also contribute. A buffer/GPU prototype is a reasonable isolated experiment,
but these data do not support replacing the renderer as the sole latency fix.
First bound transient WindowServer peaks during an effect and isolate the
occasional multi-second application events; then compare a GPU prototype with
the same dimensions, visual contract and end-to-end presentation metric.

## Identity and method

- Primary display: logical `3008×1692`, physical capture `6016×3384`, ID 3.
  Portrait display ID 2: logical `1692×3008` at `(3008,-660)`, physical
  `3384×6016`; it has only Space 11.
- Baseline Release `build/live-release/bin/yabai` SHA-256
  `b4bc68c8319503020ac4ba3b1615178f460ead41c013815528c0c01d4b5aafef`.
  Opt-in diagnostic Release `build/live-diag/bin/yabai` SHA-256
  `08f01d25fef2247fd82d194371ac395b8835f56d6bb54397bf724244fbc222d9`.
  Both used the matching local `lcs.14` payload; diagnostic macros compile out
  of the ordinary build. Their workload order was A1–B1–A2–B2, with the same
  payload loaded once. The long continuation and final mini block used separate
  temporary daemon lifetimes; do not add their captures as one lifetime.
- The diagnostic events carry `CLOCK_UPTIME_RAW` nanoseconds and a capture
  generation/token. Recorder `SCStreamFrameInfoDisplayTime` uses mach time
  converted to the same uptime scale. The request has an arrival event and
  a deadline event with the same token; analysis uses the first event and an
  explicit token→generation map. `snapshot: prepare` is cumulative from the
  request, so it is not called draw time.
- Raw ignored fixtures/logs: `build/live-validation/dossier-session/`,
  `dossier-remainder/`, `mini-session/`, `exact-headless-session/`, and the
  `run-dossier.py`, `run-mini.py`, `run-exact-headless.py`,
  `analyze-dossier.py`, `analyze-dossier-visual.py` fixtures. These are
  host-specific, input-guarded live tools, not portable CI tests. Re-run only
  in a coordinated window: they can change focus and restart Dock.

## Phase and presentation data

Diagnostic daemon PID 60769: 27/27 capture requests received callbacks,
produced ready snapshots, wrote first alpha and reached teardown end; no
capture was skipped. Medians below are milliseconds for these 27 generations
without a simultaneous frame recorder during the 20-effect memory segment.
The first seven included visual recordings, so this is a mixed sample.

| Phase | n | Median | Range |
| --- | ---: | ---: | ---: |
| Request → callback | 27 | 72.0 | 60.5–88.1 |
| Callback → first event-loop handler | 27 | 0.047 | 0.033–0.068 |
| Handler → draw start, including window setup | 27 | 2.48 | 2.18–14.98 |
| `CGContextDrawImage` | 27 | 18.08 | 13.13–24.91 |
| `CGContextFlush` call | 27 | 0.052 | 0.034–0.077 |
| Overlay Space creation/assignment | 27 | 1.11 | 0.71–5.64 |
| Window order call | 27 | 0.46 | 0.37–3.58 |
| Deliberate preswitch sleep | 27 | 20.86 | 17.68–20.90 |
| Dock focus request → reply | 27 | 18.60 | 7.19–78.83 |
| Dock reply → first alpha write | 27 | 7.14 | 0.10–16.70 |
| Request → first alpha write | 27 | 149.52 | 111.62–221.93 |

`CGContextFlush` is the call's duration, not a GPU presentation fence. First
alpha write is likewise not the first changed pixel on screen. Three
instrumented recordings per direction/mode, without PNG output, gave:

| Pair | Mode | First ≥5% change, ms | First ≥95% change, ms |
| --- | --- | --- | --- |
| 2→3 | none | 32.3, 32.8, 58.5 | same frame |
| 2→3 | crossfade 250 ms | 274.4, 282.5, 313.3 | 457.7, 480.0, 482.6 |
| 3→2 | none | 33.6, 42.0, 50.3 | same frame |
| 3→2 | crossfade 250 ms | 249.4, 301.2, 317.0 | 449.4, 451.2, 467.0 |

The blend checks passed with 5–10 intermediate complete frames and zero
measured backtrack. Empty 3→4 PNG checks passed for both modes; bar excursion
≤0.1/255 and bright-icon loss ≤0.023/255 for the crossfade. The projection
thresholds include the smoothstep easing curve, recorder/compositor work and
approximately 60 Hz frame quantization. They are not a pure capture delay.
One early visual runner falsely marked `none` as failed because it demanded
intermediate blend frames from an immediate change; its raw result is retained.
The corrected `none` checks passed and the candidate did not change.

## CPU, memory and system power

The A/B CPU-time deltas came from `ps -o time=`, not instantaneous `top CPU%`
or client return time. Each block began with a fresh daemon. The ongoing
WindowServer process and other apps remained active; idle is a local control.

| Block | Idle 8 s: daemon / WS CPU-s | 10 none, ~6.4 s: daemon / WS CPU-s | 10 crossfades, ~6.4 s: daemon / WS CPU-s |
| --- | --- | --- | --- |
| A1 baseline | 0.01 / 3.33 | 0.02 / 2.93 | 0.22 / 3.76 |
| B1 diagnostic | 0.00 / 3.47 | 0.02 / 2.89 | 0.23 / 3.75 |
| A2 baseline | 0.01 / 3.47 | 0.02 / 2.82 | 0.23 / 3.74 |
| B2 diagnostic | 0.01 / 3.50 | 0.02 / 2.86 | 0.22 / 3.73 |

The diagnostic overhead was below the resolution/noise of this CPU-time
comparison. Crossfade exceeded no-effect by roughly 0.20 daemon CPU-s and
0.8–0.9 WindowServer CPU-s per ten switches in this matched workload;
background work prevents attributing all of that difference to the effect.
The reverse burst (six requests followed by a five-second settle) reached
Space 2 with VS Code focus in all four blocks. Its 7-second wall interval
must not be compared directly with the ten-switch intervals.

| Workload, same WindowServer PID 453 | Before | Checkpoints | Hold while client alive | After client exit |
| --- | ---: | --- | --- | ---: |
| Diagnostic daemon PID 60769, planned 50 effects | 1851 MiB | 10: 1954; 20: 2351 (+500), stopped | **Not measured** in this runner | 1847 MiB |
| Exact headless screenshot control PID 62803, 30 requests | 1895 MiB | 1–30: 1908–2006 (max +111) | 15 s: 1902; 30 s: 1904 | 1898 MiB |
| Final diagnostic daemon PID 63729, short block | 1900 MiB | four none: 1981–2100; crossfade 1: 1991; crossfade 2: 2574 (+674), stopped | 0 s: 2009; 15 s: 1903; 30 s: 1905 | 1908 MiB |

The final mini block demonstrates a large **transient** `top MEM` peak:
WindowServer returned to within 5 MiB of baseline with the daemon still
alive. Its `footprint --swapped --wired` total was 1914 MB before and 1911 MB
after the 30-second hold; graphics owned dirty memory remained 669 MB and
IOSurface dirty memory 613 MB. Region counts shifted slightly, without a
category-level dirty-byte increase. There is no privileged footprint sample
at the instantaneous 2574 MiB peak, so its category cannot be assigned.
The first long runner exited at the stop threshold without holding the daemon
alive: its +500 MiB peak remains inconclusive. The headless control used the
same `captureScreenshotWithRect` rectangle, physical dimensions, cursor YES,
SDR/local intent and approximately 0.9-second cadence, but no overlay/Dock
switch. Its stable result does not prove which extra operation caused the
daemon-run peak. `vmmap` lacked privileges; the later privileged `footprint`
samples supplied the baseline/held category comparison.

`powermetrics` produced 30 one-second **system-wide** GPU samples. The
recording ended at 14:21:12 local time; crossfades began at 14:21:15, after
the sample window. The no-effect interval overlapped the samples but contained
multi-second `APPLICATION_LAUNCHED` handlers and GPU power ranged from 30 to
1763 mW. This is not a valid GPU/energy comparison among idle, none and
crossfade, and no per-process energy claim is made.

## Other gates and next bounded experiment

- The formerly observed 404–416 ms cold `SPACE_CHANGED` did not recur.
  The longest phase-timed handler in the 27-generation session was 20.53 ms;
  refresh and validation were small in that event. The validation call can
  flush layout internally, so its marker includes that work. In the mini
  session, several `APPLICATION_LAUNCHED` handlers took 3.9–7.4 seconds.
  Their source and effect on first navigation require a separate cold-start
  trace; do not attribute them to AX from this record.
- An independent current-display probe captured the portrait bounds
  `(3008,-660,1692,3008)` at `3384×6016`, error 0, using the same public
  screenshot configuration. Only one Space existed on that display; no
  portrait **transition** was tested. Current capture intent is SDR/local;
  HDR fidelity and display reconfiguration were not tested. The available
  Computer Use control had click/drag but no free pointer motion, so moving
  cursor coverage was not claimed. The first Space 3→1/1→3 visual cases were
  prepared but skipped when the final mini block crossed its memory cap.
- Final mini block requested two captures: one became ready and one callback
  arrived at 170.6 ms, beyond the 150 ms deadline, so the latter correctly
  switched without the effect. The 27-generation long runner had zero missing
  captures. The existing mock proves a single lost callback can be superseded
  after two seconds and a later callback safely releases its image. It does
  not prove bounded lifetime resources under repeatedly missing callbacks:
  each never-returning callback retains its own request/configuration. Bound
  that separately from the now-avoided legacy-API retention trigger.

Next long-memory protocol: retain the diagnostic daemon when `top MEM` rises
over the conservative 300 MiB threshold, stop *new* capture requests, and
sample `top` plus privileged `footprint` immediately and at 15/30/60-second
holds before exit. This distinguishes a transient from sustained retention.
Run a cold first-visit trace without concurrent privileged samplers, then a
bounded 3→1/1→3 visual case and a truly overlapping idle/none/crossfade GPU
sample. Do not call the 50-effect gate, first-Space visuals, cursor, portrait
transition or HDR accepted until those checks complete. A renderer prototype
should remain isolated from the focused capture fix and judged by first
presented change, visual continuity, focus and memory under identical output.

Final restoration: `/opt/yabai/bin/yabai` SHA-256
`8968238444932a59f078c91eb6f8cb2a6d7c843f21a6ea0e9e59a829466f5283`,
signed daemon PID 64222, payload handshake on the installed legacy socket
`2.1.31-lcs.13 / 0x5D`, WindowServer still PID 453, initial Space 1 restored.
No candidate daemon or diagnostic process remains active.
