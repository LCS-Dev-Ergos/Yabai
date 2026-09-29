# Effects engine: design alternatives and evaluation plan

Status: proposals, not an implementation specification. Recorded 2026-09-29.

The user requested a design discussion and subsequently asked to preserve its
results here. Implementation is intended for two agents on separate worktrees;
that work has not been dispatched. The reported WindowServer memory growth
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
