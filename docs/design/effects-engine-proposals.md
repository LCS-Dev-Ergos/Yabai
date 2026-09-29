# Effects engine: design alternatives and evaluation plan

Status: proposals, not an implementation specification. Recorded 2026-09-29.

The user requested a design discussion and subsequently asked to preserve its
results here. The two broader engine workstreams remain undispatched. The user
has now authorized a focused memory correction in a new GPT-6 Sol / High chat,
on isolated worktree `4953/Yabai`, branch `lcs-code/crossfade-capture-retention`.
The reported WindowServer memory growth
takes priority over selecting or implementing a replacement renderer.

Update after the same day's investigation: the installed lcs.32 crossfade
reproduces cumulative WindowServer growth. The growth is independently
reproduced by `SCScreenshotManager captureImageInRect`, without Yabai code,
overlay windows or Space changes. Alternative capture APIs did not show that
same pattern in bounded trials. See the [diagnostic report](../reports/windowserver-capture-retention-2026-09-29.md).
Replacing or containing this capture path is a prerequisite for the two
workstreams below; a GPU renderer alone would not address the measured trigger.

## Problem and boundaries

Keep full-Desktop crossfades visually coherent without compromising navigation,
focus correctness, memory bounds, or WindowServer responsiveness. Rapid
navigation already suppresses effects in the reported active lcs.32 binary.
The source checkout is undergoing a separate structural/concurrency refactor;
source observations are not proof of installed-binary behavior.

There are three distinct costs:

1. Navigation: Dock requests, application activation, pacing, AX queries and
   window reconciliation, including changes made without effects.
2. Effects: capture, image preparation, overlay presentation and fade.
3. Cold state: first-visit reconciliation after daemon restart.

Historical lcs.29 measurements on two scaled 4K displays reported median phase
times of 51 ms capture, 58 ms preparation and 97 ms switch/activation. These are
not measurements of the current binary. The refactor report recorded first
SPACE_CHANGED handlers up to 0.9 s and APPLICATION_FRONT_SWITCHED at 200–340 ms.

Sources: [performance](../performance.md), [navigation](../navigation.md),
[effects](../effects.md), and the 2026-09-28 external
`~/Desktop/Yabai-Refactor-Report.md`, section 6.

## What can be borrowed from Hyprland

Separate logical destination from visual interpolation; keep bounded pending
work; invalidate superseded visual work; make resource ownership explicit.
Hyprland controls composition of client surfaces. Yabai controls its own
overlay and requests changes from Dock/WindowServer. A Metal renderer does not
give Yabai ownership of other applications' surfaces or an atomic transaction
combining its overlay with Dock's Space switch.

References:
- [Wayland architecture](https://wayland.freedesktop.org/architecture.html).
- [Hyprland workspace animation controller, pinned source](https://github.com/hyprwm/Hyprland/blob/4bb6844b0351e4fbf2e3d4e46ae71b551a0e0a42/src/animation/WorkspaceAnimationController.cpp).

## Alternatives

| Alternative | Potential benefit | Main uncertainty |
| --- | --- | --- |
| Simplify the existing snapshot path | Small, attributable changes; preserve existing presentation behavior | Capture and full-image drawing remain |
| On-demand sample-buffer capture with GPU presentation | Avoid unnecessary CGImage conversion/CPU drawing | End-to-end capture cost, buffer compatibility, overlay hosting and presentation |
| SCStream retaining the latest useful frame | Remove capture acquisition from the input path when a fresh frame exists | Idle cost, startup, freshness, bounded buffers and WindowServer load |
| Private animation of foreign windows or Spaces | Potentially avoid screenshots | System-owned state, OS compatibility and previous visual regressions |
| Yabai-owned virtual workspaces | Reduce dependence on native Space switching | A different product architecture: visibility, focus, fullscreen and Mission Control |

The preferred experiment is a single on-demand screenshot delivered as a
CMSampleBuffer, then CVPixelBuffer/Metal presentation where compatible. This
is a hypothesis, not a demonstrated zero-copy or faster path. A continuous
stream is a later alternative if capture remains dominant; keeping one active
during effect-free bursts may spend work on frames that will never be shown.

The retention investigation adds a smaller candidate before that experiment:
on supported macOS versions, compare `captureScreenshotWithRect` and its
owned `SCScreenshotOutput` with the old `captureImageInRect` entry point.
Keep the existing renderer for this comparison. Initial headless tests are
encouraging; they do not establish visual equivalence or production acceptance.

Reusing an overlay and auxiliary Space per display may reduce allocations and
server operations, but earlier preparation benchmarks did not show window
reuse eliminating the dominant image cost. A smaller filtered capture was
slower than the full-resolution rectangle path in those measurements. Compare
complete paths rather than assuming that fewer pixels always wins.

Apple references:
- [Single screenshots as CGImage or CMSampleBuffer](https://developer.apple.com/videos/play/wwdc2023/10136/).
- [ScreenCaptureKit sample and IOSurface-backed buffers](https://developer.apple.com/documentation/screencapturekit/capturing-screen-content-in-macos).
- [Core Video Metal texture cache](https://developer.apple.com/documentation/corevideo/cvmetaltexturecache).

## Proposed module contract

- Navigation owns destination, display anchor and focus. The effect reports
  readiness or omission; it does not choose the destination.
- One effect lifetime owns image/buffer, overlay, timers and cancellation.
  Its public interface hides their ordering and cleanup.
- Every result carries a generation and relevant display configuration;
  validate freshness and relevance again before expensive preparation.
- Superseded visual work can be abandoned without losing the logical meaning
  of relative presses, reversals, numbered destinations or move commands.
- A burst should be able to abandon an already pending effect, not only mark
  later queued effects as disabled. Late callbacks must remain safe.
- Effects have a bounded preparation budget and may be omitted. Fresh capture
  on demand, no advance acquisition, and guaranteed zero added latency cannot
  all be promised together.
- Request completion, GPU completion and actual presentation are distinct.
  The current one-refresh sleep is an opportunity, not a presentation fence.
- Avoid global locks around unrelated blocking operations. Define ownership
  and ordering for operations that can affect the same server-side resources.

Timeout is not cancellation of the framework's capture. The current stale
request retry policy deserves an explicit bound on unresolved callbacks;
repeated loss should suspend effects while leaving navigation usable. This is
a code-path concern, not an observed explanation of the WindowServer growth.

Socket authentication and private socket creation remain separate security
work. Replacing the renderer does not resolve them; check the concurrent
hardening work before assigning overlapping changes.

## Two candidate workstreams, not yet dispatched

**A — Navigation and lifecycle baseline.** Reduce redundant reconciliation,
discard superseded visual preparation, and establish bounded ownership and
cleanup. Batching already exists in parts of the code; identify repeated
external queries before adding more machinery. Preserve final state and focus.

**B — Buffer/GPU feasibility experiment.** Use the same navigation behavior and
effect admission policy while comparing on-demand buffer capture and GPU
presentation with the existing path. Validate hosting, colors, transparency,
layering and Space continuity before integration. Do not silently reintroduce
previous white-surface or remote Core Animation failures.

For parallel work, agree on the minimal effect interface and a common base
first. A owns navigation/lifecycle policy; B owns an isolated rendering adapter
and comparative harness. Shared interface changes require coordination.
Integration, visual acceptance and release are later, explicit steps.

## Evaluation gates

- Separate cold first visits, warm isolated transitions and rapid bursts.
- Measure input-to-visible-change, final correct focus, tail latency, memory
  retention and WindowServer load, not just command return or alpha values.
- Use identical display geometry, color configuration and workload for A/B.
- Cover cancellation, late callbacks, monitor changes and backend failure.
- Recheck Desktop icons, SketchyBar, black/white frames and cross-display focus.
- Establish whether repeated transitions reach a memory plateau before
  increasing capture frequency or adding retained rendering resources.

Blur is a later aesthetic option. It does not correct stale frames, resource
retention or unsynchronised presentation and adds processing cost.

## Resource priorities after the privileged inspection

The report now includes a privileged before/held/exit comparison: three legacy
rectangle captures add 233 MB and three regions in WindowServer's `Owned
physical footprint (unmapped)` category; the new rectangle API adds neither
in the same bounded test. The precise private object remains unknown. The
installed lcs.32 daemon used 0.0–0.1% CPU and approximately 21 MiB at idle in a
six-second sample. These observations prioritize the following work:

1. **Close the capture lifetime defect first.** Validate the delegated backend
   with the daemon alive, then visual continuity and longer bounded workloads.
   Stable post-exit memory alone is insufficient. Do not require a renderer
   rewrite to deliver this fix or assume the new API is faster.
2. **Avoid work that will never be presented.** Revalidate effect admission
   before capture and before full-frame preparation. When a pending effect is
   superseded by a quick burst, continue logical navigation safely and discard
   late visual output. Preserve generation ownership and correct final focus.
   Measure how many captures/draws this actually avoids. Logical cancellation
   does not cancel an outstanding ScreenCaptureKit operation.
3. **Bound missing callbacks.** The current two-second stale-request escape
   permits another request even when the previous callback never returned.
   Generation safety prevents stale adoption but does not bound all retained
   callback contexts. Consider suspending effects after a bounded number of
   unresolved requests, with an explicit recovery policy. This is a separate
   failure-path concern, not the demonstrated source of the capture growth.
4. **Reduce full-frame preparation only with a visual gate.** A 6016x3384
   four-byte image is 77.7 MiB. The current path draws it into a WindowServer
   window context before ordering the overlay. Direct image/layer handoff
   previously exposed uninitialized white backing; removing the copy without
   a reliable readiness mechanism is not an accepted optimization. An
   on-demand buffer/GPU experiment remains useful, but include all server
   work, color conversion and presentation in its comparison.
5. **Optionally cap effect resolution.** Scaling both dimensions by 0.75
   would reduce the image pixel payload to 56.25% (43.7 MiB for this frame),
   if the capture backend honors those output dimensions. This arithmetic is
   not a measured CPU or latency saving: capture may still composite at native
   size, and upscaling may blur text/icons. Make it a separate opt-in quality
   experiment, never an implicit fix for the memory bug.
6. **Profile cold reconciliation before caching it.** Source at `9a56727`
   refreshes only `applications_to_refresh`, not all applications unconditionally
   on every Space change. Tree changes already use dirty flags and batching.
   Identify repeated AX/SkyLight queries and cache only within a clearly
   invalidated reconciliation pass. Measure cold first visits separately.
7. **Measure redundant event wakeups.** Producers currently `sem_post` for
   every event, including merged mouse moves, while the consumer drains the
   entire queue before waiting. Remaining semaphore tokens can cause empty
   iterations and autorelease-pool churn. This follows from the source, but
   its material cost has not been measured. A wake-coalescing protocol would
   need queue/consumer synchronization and a lost-wakeup regression test.
   Do not prioritize it over the measured capture and preparation costs.

Avoid continuous capture solely to hide on-demand latency: a persistent stream
may trade a faster click for greater idle server/GPU work. Likewise, retaining
one overlay per Space trades allocation time for resident resources. Prefer
on-demand work, bounded resources, and prompt release until measurements show
a worthwhile alternative. A smaller pacing interval alone is not evidence of
less work and may increase contention; assess input-to-presentation and final
focus rather than command return time.

## Candidate checkpoint

Candidate `2131b94` now has bounded live acceptance for the capture-memory
regression, primary-display icons/bar and the tested focus/burst cases.
The [live report](../reports/capture-live-results-2026-09-29.md) records the
remaining limits and orders the next experiments. No-effect presentation was
33–43 ms; 250 ms crossfades reached 5% change in 248–292 ms. Keep the focused
capture fix and measure the remaining phase delays before choosing a GPU
renderer or continuous stream.
