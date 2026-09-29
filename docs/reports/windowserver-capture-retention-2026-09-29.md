# WindowServer memory growth: isolated to the rectangle capture path

Date: 2026-09-29. Initial investigation: diagnosis and replacement candidates.
Subsequent authorized implementation and temporary daemon/payload validation
are recorded in [the live results](capture-live-results-2026-09-29.md).
The initial diagnostic evidence below predates that candidate.

## Finding

The installed crossfade produces cumulative memory charged to WindowServer.
The same pattern reproduces with `SCScreenshotManager captureImageInRect`,
without switching Spaces, creating an overlay, running a fade, or retaining
the delivered image in client code. An independent ARC reproducer using only
public Apple APIs also reproduces it. This isolates the trigger to this capture
path on the tested OS, rather than to Yabai's snapshot reference counter or the
overlay's teardown being necessary for the growth.

The source-level call is in `src/effects/snapshot_capture.m`, line 105 in the
examined checkout. Yabai triggers server-side resource allocation through the
capture framework. Its own small `calloc` is not a direct allocation inside
WindowServer's address space.

Exact server object type and internal ownership were not established. Initial
unprivileged inspection failed; the later authorized footprint comparison below
identifies an accounting category, not the internal object. The evidence supports a capture-path
retention defect on this OS; it is not an Apple-confirmed diagnosis or a claim
that every supported macOS version has the same defect.

## Environment and provenance

- `sw_vers`: macOS 27.2, build 26B5091g.
- WindowServer PID 453 throughout these measurements; Dock PID 927.
- Installed binary: `/opt/yabai/bin/yabai`, identical to
  `/nix/store/kp8mw9mm66n9a72bf5w7gj8flg7qr8ig-yabai-7.1.25-lcs.32/bin/yabai`.
- SHA-256: `8968238444932a59f078c91eb6f8cb2a6d7c843f21a6ea0e9e59a829466f5283`.
- Displays reported 3008x1692 and 1692x3008 logical points. The tested display
  produced 6016x3384 captured pixels, 81,432,576 bytes for one 4-byte image:
  approximately 77.7 MiB.
- Initial daemon PID 1156. Another agent restarted it at approximately
  12:29:43 local time; replacement PID 27864, same binary checksum. The user
  explicitly confirmed: "Sì, un altro agente ha riavviato Yabai."
- WindowServer then fell from roughly 7.6 GiB to 1.8 GiB without its own PID
  changing. This investigator did not request or perform that restart.
- Working tree base was 4ae5811 with concurrent hardening modifications.
  Compared with tag v7.1.25-lcs.32, the examined snapshot files differ only in
  exported linkage of three hooks; capture and surface implementations match.

## Measurement method

Use `top`'s process MEM field, not `ps` RSS or VSZ. Initial `top` MEM was
7168–7171 MiB while RSS was only about 112 MiB. These metrics are different;
WindowServer's graphics/resource accounting must not be inferred from RSS.
The values below preserve `top`'s coarse displayed units, with normal system
noise; they are not an exact byte-level allocation trace.

The live A/B used six alternating changes between empty Desktops 3 and 4,
spaced by approximately one second, first with effect `1 0`, then with
`crossfade .25`. It waited for idle input and stopped on new user input, with
a 500 MiB growth limit. The no-effect run restored its starting Desktop; the
crossfade run was interrupted after its sixth measurement and deliberately
did not override the user's subsequent input.

The isolated probes keep their process alive after releasing their own image
or surface resources, sample WindowServer, then exit and sample again. No
images are saved. Three-call probes separate capture from hidden overlay
surface creation. Subsequent public-API probes bypass all Yabai code.
The runner limits growth to 300 MiB; longer replacement-API trials record
concurrent input rather than interfering with it.

## Results

Memory is the total attributed to WindowServer, in MiB as displayed by top.

| Experiment | Requests | Before | After requests / while process lives | After process exit |
| --- | ---: | ---: | --- | ---: |
| Live navigation without effect | 6 | 7173 | 7167 after step 6; 7165 after 12 s | Not applicable |
| Live crossfade | 6 | 7165 | 7261, 7335, 7418, 7498, 7564, 7645; 7628 about 34 s later | Daemon subsequently restarted by another agent |
| Production capture code only; images explicitly released | 3 | 1844 | 1937, 2017, 2083; 2080 after 5 s | 1841 |
| Direct rectangle API; no client image retain | 3 | 1845 | 1931, 2011, 2094; 2071 after 5 s | 1844 |
| Hidden overlay surface with synthetic image; no capture or auxiliary Space | 3 | 1839 | 1858, 1842, 1842; 1847 after 5 s | 1846 |
| Filtered CGImage API, same resolution | 3 | 1844 | 1865, 1862, 1857; 1853 after 5 s | 1846 |
| New rectangle screenshot/output API | 12 | 1844 | No cumulative slope; range 1841–1943; 1846 at step 12; 1836 after 5 s | 1929, with concurrent input/background noise |
| Filtered CMSampleBuffer API | 3 | 1684 | 1779, 1775, 1770; 1699 after 5 s | 1698 |
| Independent public-API ARC legacy rectangle reproducer | 3 | 1839 | 1920, 2003, 2073; 1935 after 5 s | 1687 |

The independent ARC run experienced a concurrent background drop in the total
footprint, so its final delta is not a clean attribution. Its three immediate
increments repeat the same approximately one-frame-per-request slope. The
earlier two isolated capture runs have clearer before/hold/exit comparisons.

The 12-call new-rectangle test has transient approximately 100 MiB peaks and
input recorded at its last step. It demonstrates absence of the legacy API's
linear accumulation in this bounded run, not zero allocations, a zero retained
cache, or a completed long-duration acceptance test. The sample-buffer path
temporarily holds about one frame before settling; it does not add one frame
for every request in the observed three-call run.

Earlier interrupted buffer and three-call new-rectangle trials are retained
in the raw logs, but not used as completed hold/exit comparisons.

Observed capture latencies (small samples, not a benchmark under controlled load):

- Direct legacy rectangle: 82.2, 36.1, 35.7 ms; independent ARC: 75.5, 35.3,
  31.1 ms.
- Filtered CGImage: 141.3, 66.7, 68.0 ms.
- New rectangle, twelve calls: 54.1–72.4 ms.
- Filtered sample buffer: 160.0, 55.7, 62.7 ms.

The last path's cold call exceeds the production capture budget of 150 ms.
Replacing the API therefore requires preserving timeout/fallback behavior,
not extending the budget blindly.

## What the code audit establishes

The ordinary successful capture owns two references to its request (caller and
callback), retains the callback image, returns another retained image to the
consumer, releases the request's image on final request destruction, and
releases the consumed image after preparing the surface. The direct API
reproducer bypasses all of this, yet still grows WindowServer.

Snapshot cancellation releases its CGContext, owned window, auxiliary Space,
UUID and timer. The hidden-surface control exercises context/window release
and does not reproduce cumulative growth. It does not exhaustively validate
every visible overlay, auxiliary-Space, fade, or failure path; those are not
necessary to reproduce the demonstrated defect.

The previously identified stale-capture retry concern remains separate:
timeouts cannot cancel a framework operation and may leave unresolved
callbacks. This experiment uses successful, sequential captures, so lost
callbacks are not needed to explain the measured accumulation.

## Reproducer and raw evidence

Portable, independent source: `tools/effects/capture_memory_probe.m`.
It uses ARC and public Cocoa/ScreenCaptureKit APIs only, with an AppKit run
loop. It never retains images beyond the callback, writes screenshots, or
changes the installed daemon. Permission is checked without prompting.

Build from the repository root:

```sh
xcrun clang -O1 -fobjc-arc -fblocks -framework Cocoa -framework ScreenCaptureKit \
  tools/effects/capture_memory_probe.m -o build/capture-memory-probe
```

Run `build/capture-memory-probe direct 3` for the legacy entry point, or
`build/capture-memory-probe newrect 3` for the macOS 26+ entry point. At READY,
measure WindowServer; press Enter once per STEP, measuring between each.
At DONE, keep the process alive several seconds and measure again; then press
Enter to exit and remeasure. Use `top -l 1 -pid <WindowServer-PID> -stats
pid,command,mem,cmprs,purg`. Avoid unbounded legacy loops on a loaded system.

Actual automated invocations used in this session:

```sh
python3 build/diagnostics/windowserver-20260929/measure.py none 6
python3 build/diagnostics/windowserver-20260929/measure.py crossfade 6
python3 build/diagnostics/windowserver-20260929/resource-run.py capture
python3 build/diagnostics/windowserver-20260929/resource-run.py direct
python3 build/diagnostics/windowserver-20260929/resource-run.py window
python3 build/diagnostics/windowserver-20260929/resource-run.py filtered
python3 build/diagnostics/windowserver-20260929/resource-run.py newrect 12 --observe-input
YABAI_CAPTURE_PROBE=build/diagnostics/windowserver-20260929/capture-memory-probe \
  python3 build/diagnostics/windowserver-20260929/resource-run.py direct 3 --observe-input
python3 build/diagnostics/windowserver-20260929/resource-run.py buffer 3 --observe-input
```

The automated runners and JSONL are local ignored diagnostic artifacts under
`build/diagnostics/windowserver-20260929/`, not dependencies of the portable
reproducer. Original early runner verdicts assessed only the post-exit delta;
that is insufficient for this bug. Use the recorded STEP and hold values. The
runner was updated to report `retained_while_alive_mib`; the standalone ARC run
reported `RETAINED_WHILE_ALIVE`.

## Correction dispatched; live acceptance pending

The user subsequently authorized implementation in a new GPT-6 Sol / High
chat. Its isolated worktree is `/Users/lcs-dev/.codex/worktrees/4953/Yabai`,
branch `lcs-code/crossfade-capture-retention`, based on `9a56727`.
Scope: replace the capture backend, preserve the renderer and request lifetime
contract, add meaningful offline tests and prepare a local candidate. No live
capture, service replacement, Dock restart or release is part of that initial
dispatch. This investigation continues separately in the ordinary checkout.

First replace or contain the problematic capture entry point while preserving
the current renderer. On macOS 26+, `captureScreenshotWithRect:configuration:`
returns an `SCScreenshotOutput` owning its images and is the most direct
candidate. `captureImageWithFilter` is another candidate; cache/invalidate
display/filter metadata correctly. Sample-buffer/GPU rendering remains a
separate experiment, not a prerequisite for addressing the measured defect.

Revalidate pixel dimensions, display-local colors/SDR/HDR behavior, cursor,
multi-display coordinates, captured content, timeout/cancellation and actual
Desktop/SketchyBar continuity. The diagnostic alternatives explicitly omit
the cursor and use different configuration APIs, so their visual output has
not been accepted as equivalent to the current effect.

Require a longer fixed-workload hold/exit memory test before release. Do not
silently fall back to the legacy rectangle path on an affected OS. A bounded
capture helper process is a possible containment mechanism if supported APIs
cannot be made reliable, with separate startup/IPC/TCC costs to evaluate.

Temporary containment is navigation without crossfade. Restarting Yabai
released the observed accumulation, but periodic restarts are not a fix. No
temporary setting was applied during this investigation.

The [engine proposals](../design/effects-engine-proposals.md) now put this
capture-lifetime gate before the planned two parallel implementation worktrees.

## Authorized WindowServer footprint comparison

The user offered administrative access to obtain meaningful evidence. A native
macOS authorization dialog allowed read-only `/usr/bin/footprint -p WindowServer
--swapped --wired`. No binary patching, debugger attachment or protection change
was needed. WindowServer stayed PID 453. The same standalone ARC reproducer ran
three requests per backend, releasing each client image, then remained alive
for five seconds before the held measurement. The final measurement followed
client exit. No Space changes or overlays were generated by the probe.

Numbers below are rounded MB as printed by `footprint`; the expected raw image
payload is 77.7 MiB per frame. The category is **Owned physical footprint
(unmapped)**, without the separate `(graphics)` suffix.

| Backend / checkpoint | Total physical footprint | Category dirty | Category regions |
| --- | ---: | ---: | ---: |
| Legacy rectangle / before | 2074 MB | 233 MB | 633 |
| Legacy rectangle / held after 3 | 2307 MB | 466 MB | 636 |
| Legacy rectangle / exited | 2054 MB | 233 MB | 633 |
| New rectangle / before | 2073 MB | 233 MB | 633 |
| New rectangle / held after 3 | 2077 MB | 233 MB | 633 |
| New rectangle / exited | 2059 MB | 233 MB | 633 |

The legacy path adds 233 MB and three regions in that category, then returns
exactly to its category baseline on client exit. Its parenthesized swapped
column also rises from 153 to 386 MB and returns to 153 MB; do not add that
column again to the footprint. The separately named IOSurface dirty column is
613 MB throughout both runs, although its wired/reclaimable values fluctuate.
The new rectangle path does not add category bytes or regions in this trial.
This is stronger allocation-accounting evidence for the replacement API; it
does not name the private backing object or prove there are no IOSurfaces
involved internally. Other applications remained active, so small total and
other-category fluctuations are not attributed to the probe.

Legacy callback times were 88.9, 44.1 and 37.8 ms; new rectangle times were
81.8, 64.9 and 82.0 ms. Three calls under different memory conditions are not
a latency benchmark and do not establish that the replacement is faster.

Local raw summaries, orchestrator and checkpoint log are under ignored
`build/diagnostics/idle-20260929/`: `capture-footprint.py`,
`differential.jsonl`, `direct-{before,held,exited}.txt` and
`newrect-{before,held,exited}.txt`. The orchestrator invokes the native
authorization mechanism for the footprint summaries; screen capture itself
runs as the ordinary user. The diagnostic processes exited normally.

## Idle baseline and optimization implications

The installed daemon PID 40179 was sampled for six seconds with `sample`
(10 ms interval) and four `top` samples at two-second intervals. Its binary
SHA-256 still matches the lcs.32 value above. Observed CPU was 0.0–0.1%,
top MEM 21 MiB, seven threads and 181 ports. The sample reported 20.5 MiB
physical footprint and a 190.3 MiB historical peak. The event-loop thread was
in `sem_wait` in 543 of 544 samples; the main thread and message acceptor were
also predominantly waiting. This is a short idle baseline, not whole-day or
transition acceptance. Raw files are `yabai-idle.sample.txt` and the adjacent
idle diagnostics. It prioritizes work per transition and server-side resources
over a speculative rewrite to reduce already-small idle CPU use.
