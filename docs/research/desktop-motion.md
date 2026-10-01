# Desktop motion research

Primary-source review, 2026-09-27. This is a design and verification plan;
it does not establish visual quality or expose a supported Spaces animator.

## What Apple documents

Motion should explain state, remain brief, permit interruption and avoid
unnecessary work on frequent interactions. These are design principles, not
a prescribed duration or curve for Desktop switching.
[Apple HIG: Motion](https://developer.apple.com/design/human-interface-guidelines/motion).

Immediate response, reversible spatial paths and redirection during motion
matter more than adding decorative bounce. A keyboard repeat is discrete
intent; it does not provide the velocity of a trackpad gesture.
[Designing Fluid Interfaces](https://developer.apple.com/videos/play/wwdc2018/803/).

Springs can preserve position and velocity when interrupted, including with
zero bounce. Apple distinguishes perceptual duration from the longer settling
tail. Related properties can deliberately have different curves and endings;
coordination does not require every property to finish simultaneously.
[Animate with springs](https://developer.apple.com/videos/play/wwdc2023/10158/).

Core Animation presents intermediate values separately from model values;
use presentation state when retargeting an animation on a layer you own.
Its documented scope is application layers/views, not arbitrary windows
owned by other applications or the Dock's Space transition.
[Core Animation basics](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CoreAnimationBasics/CoreAnimationBasics.html).

`CATransaction` groups changes to owned layers; `CAMediaTiming` defines local
timelines, `beginTime`, `duration`, `speed` and `timeOffset`. These concepts
suggest one transition clock, but wrapping private SkyLight calls in a
transaction has no documented guarantee of atomic or synchronized display.
[Advanced animation timing](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/AdvancedAnimationTricks/AdvancedAnimationTricks.html).

## Public APIs and their limits

Availability below was checked against the installed Xcode SDK headers
(`NSScreen.h`, `NSView.h`, `NSWindow.h`, `NSAccessibility.h`,
`CADisplayLink.h`, `CAAnimation.h`, `CVDisplayLink.h`).

| API | Verified availability and behavior |
| --- | --- |
| `NSScreen/NSView/NSWindow displayLinkWithTarget:selector:` | macOS 14+. Returns `CADisplayLink`; screen-specific callbacks. View/window links follow their display. [NSScreen](https://developer.apple.com/documentation/appkit/nsscreen/displaylink(target:selector:)). |
| `CADisplayLink` | macOS 14+. Add to a run loop; pause/invalidate when idle. The class factory `+displayLinkWithTarget:selector:` is unavailable on macOS: create through AppKit. [CADisplayLink](https://developer.apple.com/documentation/quartzcore/cadisplaylink). |
| `timestamp`, `targetTimestamp`, `preferredFrameRateRange` | Available on macOS with `CADisplayLink`; target the upcoming frame and derive elapsed time dynamically. Rate preference is not a real-time deadline. [Display-link timing](https://developer.apple.com/documentation/quartzcore/cadisplaylink). |
| `CVDisplayLinkCreateWithCGDisplay` and related CVDisplayLink functions | Legacy macOS 10.4 APIs deprecated in macOS 15; Apple directs callers to the AppKit factories above. [CVDisplayLink](https://developer.apple.com/documentation/corevideo/cvdisplaylink). |
| `CASpringAnimation` | macOS 10.11+. Supports `initialVelocity`, mass, stiffness, damping and `settlingDuration`; `initWithPerceptualDuration:bounce:` starts at macOS 14. [CASpringAnimation](https://developer.apple.com/documentation/quartzcore/caspringanimation). |
| `NSWorkspace.accessibilityDisplayShouldReduceMotion` | macOS 10.12+. Observe `NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification` (10.10+) on the workspace notification center. [Reduce Motion](https://developer.apple.com/documentation/appkit/nsworkspace/accessibilitydisplayshouldreducemotion). |

A display link aligns callback opportunities with a display, not arbitrary
SkyLight IPC completion. Keep callbacks bounded; avoid blocking focus work
there. Use one verified clock domain for animation epochs and frame targets.
Keep per-display pacing/lifecycle state, including sleep and reconfiguration;
two displays both reporting 60 Hz need not share a refresh phase.
The last sentence is an engineering precaution, not an Apple synchronization
guarantee. [Apple frame-pacing guidance](https://developer.apple.com/videos/play/wwdc2021/10147/).

`activeSpaceDidChangeNotification` reports a Space change. Its contract does
not promise that every destination application has rendered its first frame;
do not turn it into a presentation fence or add an arbitrary waiting timer.
[Notification contract](https://developer.apple.com/documentation/appkit/nsworkspace/activespacedidchangenotification).

## Applying this to the lcs.11 baseline

The subsequent implementation and its verification limits are in [effects](../EFFECTS.md).

The [navigation path](../../src/navigation/step.c) dims destination windows,
switches Space, focuses/possibly raises a window, then submits fades one by
one. The [worker](../../src/osax/window_fade.c) assigns each request its own start
time and uses `1 - (1 - t)^3`; its timer runs up to 120 Hz without display sync.
These facts imply possible dim holds during slow focus and inter-window start
skew. Actual visibility of either artifact requires observation.

The configured 0.9-to-1 recovery lasts 150 ms: nine 60 Hz intervals. The cubic
formula covers 75% of the change in about 56 ms, leaving a subtle tail.
This arithmetic helps explain a possible abrupt recovery; it cannot explain
all variable durations. Also audit competing ordinary opacity/focus writes:
an immediate opacity setter cancels an active fade in the current worker.

Proposed opacity baseline: monotonic, bounded, no overshoot, deterministic
completion; compare the current cubic with smoothstep `3t² - 2t³` under the
same timeline. Smoothstep has zero endpoint slopes; interruption still needs
explicit continuity handling. These are candidates, not Apple's secret curve.
For a later spatial prototype, compare a critically damped spring preserving
current position and velocity. Do not attach bounce to whole-Desktop opacity.

A fade only changes transparency; it cannot express left/right travel.
Adding translation to real windows would need private transforms and strict
restoration, or a separately rendered representation. Public Core Animation
does not supply that capability for foreign windows. A snapshot prototype
also changes live-content/input semantics and adds capture/compositing cost.

The host reports two 60 Hz Dell 4K displays, each with 3008×1692 logical points
(one portrait) and a 6016×3384 backing mode. That is about 20.36 million backing
pixels per display; it is not proof that all pixels are repainted every frame.
Apple distinguishes points, backing stores and final display scaling.
[High-resolution rendering](https://developer.apple.com/library/archive/documentation/GraphicsAnimation/Conceptual/HighResolutionOSX/Explained/Explained.html).
Transparency can require background blending; persistent 0.975 inactive alpha
therefore deserves its own A/B measurement. It does not follow that opacity
accounts for the measured WindowServer CPU or that alpha 1 eliminates all
compositing. [Core Animation performance](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ImprovingAnimationPerformance/ImprovingAnimationPerformance.html).

## Recommended order and acceptance gates

1. Trace switch, focus, fade submission, cancellation and first/last alpha
   writes on one clock. Compare effects off/on and persistent opacity off/on
   with fixed Desktop IDs, windows, display modes and repeated idle controls.
   Separate command latency, daemon/WindowServer CPU and rendered frame timing.
2. Establish ownership of a temporary navigation effect versus ordinary
   focus opacity; restore the latest intended target on every cancellation.
   Start coordinated recovery without waiting for application focus work.
   Use a shared epoch/group generation; ensure a stale group cannot redim or
   overwrite newer intent. Grouped writes still need rendered validation.
3. Prototype macOS 14+ `NSScreen` display-link pacing at the target display's
   actual cadence. Retain elapsed-time interpolation and skip overdue frames;
   verify fewer writes/wakeups at 60 Hz, no completion drift and no new stalls.
   Reducing 120 Hz writes may save work; its benefit remains unmeasured.
4. Then compare bounded opacity curves and durations visually on both screens.
   Prefer a single calm recovery for a settled destination; cancel obsolete
   effects during repeats without delaying navigation. Do not add debounce
   delays or treat every application render delay as grounds to prolong a fade.
5. Gate spatial springs/private compositor interpolation on an isolated visual
   prototype proving interruption, reversal, focus, live content and cleanup.
   The prior native-alpha probe did not establish rendered interpolation or
   cancellation; see [effects](../EFFECTS.md). It remains experimental.

For each prototype test single presses, held keys, reversals, empty/populated
Desktops, the same app across displays, cancellation, window close and display
sleep/reconfiguration. Include Reduce Motion with immediate or restrained
feedback. Require frame-based evidence and a side-by-side comparison before adopting a
visual default; clean unit tests and low daemon CPU do not certify fluidity.
[Apple reduced-motion guidance](https://developer.apple.com/help/app-store-connect/manage-app-accessibility/reduced-motion-evaluation-criteria).
