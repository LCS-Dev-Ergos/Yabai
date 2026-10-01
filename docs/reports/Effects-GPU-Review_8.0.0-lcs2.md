# GPU transaction experiment: review

Date: 2026-10-01. Scope: read-only review of the completed experimental task
and selection of the next step. No implementation or new live test was
performed during this review.

## Reviewed evidence

Experimental branch: `lcs-code/effects-buffer-gpu-presentation`. The
uncommitted CMake, experimental source/tests and report remain preserved.

- `docs/reports/effects-buffer-gpu-presentation-2026-09-30.md`, final transaction
  and 2026-10-01 sections.
- `build/buffer-gpu-transaction-20261001-ready2/results.json`.
- `build/buffer-gpu-transaction-20261001-ready2/teardown-post-exit.json`.
- `tools/effects/experimental/buffer_gpu_host.m`, close and comparison teardown.
- The final result of the completed experimental task.

One native A invocation reached positive presentation readiness at 284.569 ms
from host start; cached source decode took a separate 266.428 ms. These are not
GPU execution time or an end-to-end production navigation result. The initialized
guard stayed visible. The cleanup observer failed after its 100 ms window,
stopping the schedule before B. Counts are A=1, B=0, remaining not run=11.
There is no evidence for accepting or rejecting the transaction variant yet.

Both retired window IDs still returned successful bounds queries inside the
host. The record lacks the bounds/error detail needed to classify that outcome.
After process exit, both were absent from public inventory and returned error
1000/null bounds, as did an invalid control; a live window control remained valid.
This supports no observed persistent window leak after exit, not proof of timely
in-process retirement. The owning auxiliary Space returned type 3 both before
and after close; an invalid Space also returned 3. Type-based inference cannot
establish destruction. Empty membership alone would not prove Space destruction
either. The successful destroy call and unchanged managed topology remain
separate, weaker observations.

Apple documents NSWindow.close as screen removal, not unconditional object
destruction, and requires isReleasedWhenClosed=false for ARC clients. The host
already uses false. Switching it to true is not a repair.
Sources: https://developer.apple.com/documentation/appkit/nswindow/close()
and https://developer.apple.com/documentation/appkit/nswindow/isreleasedwhenclosed.

The report also corrects a previous causal interpretation: some legacy
late-order/event cases loaded a cached PNG after acquiring and releasing a
fresh capture, whereas their controls used the fresh source. Their raw failures
remain valid, but they do not isolate ordering. The prepared new A/B path uses
the same cached source in both modes and avoids that particular confound.

Offline reported evidence is Debug/ASan/TSan 4/4 each, runner 2/2, protocol 5/5,
strict build and analyzer clean. These suites were not repeated during this
review; they do not establish native cleanup or presentation.

## Decision and next bounded task

Keep production lcs.2 and experimental defaultOFF. First repair and validate
the cleanup observer and its ownership boundary; do not resume the A/B batch
or abandon transaction presentation on A=1/B=0.

The next implementation task belongs to the experimental GPU work and should
initially be offline:

1. Preserve the failed trace and replay its observations through a small
   classifier that distinguishes visible/ordered, hidden but registered,
   released, and unknown. Missing evidence or an unavailable API must not pass.
2. Record bounds return codes and finite/null values, ordered state, exact owned
   IDs in independent inventory, object lifetime and membership. Validate any
   existence predicate with known live and invalid controls. Do not equate an
   empty membership list with a destroyed auxiliary Space.
3. Separate the observation before and after releasing actual owner references
   and draining a scoped autorelease boundary. Keep the process alive for the
   observation; process exit must not be the cleanup mechanism. Retain resources
   still required by asynchronous GPU/scheduling work. Never flip ARC release
   policy or remove the guard merely to satisfy the check.
4. Add focused regressions for invalid/type 3 ambiguity, null bounds, hidden
   retained resources, observation failure, deferred release and late callbacks.
   The tests must distinguish observer defects from actual native-lifetime proof.
5. Prepare a new bounded native cleanup plan only after offline review. First
   validate the observer/lifetime boundary in one invocation; no automatic
   A/B loop. Preserve the original twelve-invocation cap and account for the
   already consumed A case. Changed ownership/setup means the old case cannot
   silently become a matched control. A fresh native invocation counts against
   that cap, even if used primarily for cleanup diagnosis. If the original
   six-pair qualification is no longer feasible, report the new checkpoint as
   diagnostic/incomplete rather than silently weakening its acceptance rule.

Candidate explanations to discriminate are (a) insufficient/private observer
semantics, (b) deferred AppKit/autorelease or still-owned objects, and (c) native
resources actually surviving close until connection/process teardown. None is
established as the whole cause. Both the native Metal and raw guard IDs answered
the old check, so AppKit lifetime alone must not be presumed sufficient.

No test was launched by this review. The selected next task is cleanup
validation; transaction comparison, exposed full-display pixels,
navigation integration and performance acceptance remain pending.

## Later review: closed campaign and architectural proposal

The subsequent task is now closed. This review read the experimental branch's
`docs/reports/effects-buffer-gpu-decision-2026-10-01.md`, raw
`build/buffer-gpu-cleanup-20261001/native-ready2/results.json`, the latest
analysis, historical capture phase report and production `f33d1a` snapshot
sources. Total native invocations in the bounded comparison are 2/12; B remains
untested. The new process-alive probe observed owned windows and weak client
owners absent by 41.948 ms after visible-close start, within the unchanged
100 ms observation bound. This resolves the old cleanup observation question for
that invocation. It does not identify a unique server-internal cause or prove
all allocations reclaimed. Auxiliary-Space existence remains unknown; this is
not a demonstrated Space leak. The native window still read ordered after the
guard was hidden in intermediate samples. Actual exposed pixels were not
recorded; coupled close cannot be treated as atomic. The dual-window
replacement remains no-go for production.

The experiment's analysis proposes persistent per-display hosting, a single
initialized host, and eliminating the CPU draw. These are separate hypotheses.
Persistence can amortize setup but does not establish initial-pixel readiness,
Dock coordination or final Space teardown. At 6016x3384, one tightly packed
BGRA8 image contains 77.66015625 MiB; object-count bounds are not
server-footprint guarantees. Prefer exploring lazy, bounded reuse and
distinguish reusable device/queue metadata from retained full-resolution
surfaces, without exceeding current admission.

Historical medians 72 ms capture, 18.08 ms draw, 20.86 ms preswitch wait and
149.52 ms to first alpha come from mixed samples of a September 29 candidate,
not installed lcs.2. Subtracting phase medians from a total median is only an
illustrative budget, not a measured counterfactual. First alpha is not first
visible change. Space setup median 1.11 ms alone does not justify large
resident buffers. CPU draw happens once per transition, not once per opacity
frame.

Production capture returns `SCScreenshotOutput.sdrImage` (CGImage), whereas the
experimental importer accepts IOSurface-backed CVPixelBuffer. Keeping capture
unchanged does not by itself provide a zero-copy buffer. Any conversion/upload
must be included; changing to filtered sample-buffer capture is a second contract
and requires separate content/color/lifetime validation.

Recommended research direction, not scheduled or implemented: first determine
whether one native host can display initialized current-generation content and
hide/cancel safely, without an independently ordered guard. Compare the simplest
supported image presentation (`CGImage` in a privately owned CALayer) with explicit
Metal only if it offers necessary capability; both need actual pixel evidence.
Do not restore the earlier remote-CA approach that exposed white backing. A
single native host is a new hypothesis, not a fix proven by that distinction.
Begin in an ordinary static scene to isolate hosting; prove continuity through
the required Space arrangement separately before navigation integration. Retain
the production capture as the initial input and count all conversion cost.
Only qualified visual/lifecycle behavior warrants bounded reuse and comparative
performance work. No continuous capture stream, timer-driven idle rendering,
speculative private API sweep or new runtime integration is justified now.

Useful public contracts checked during review:
https://developer.apple.com/documentation/screencapturekit/scscreenshotoutput/sdrimage
https://developer.apple.com/documentation/QuartzCore/CALayer/contents
https://developer.apple.com/documentation/quartzcore/cametallayer/allowsnextdrawabletimeout

This is proposal evaluation only; no new tests, implementation or publication
followed. Mainline runtime GPU integration
remains unsupported; reusable ownership/import tests stay experimental.
