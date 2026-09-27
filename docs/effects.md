# Opacity effects

Payload `2.1.31-lcs.6` separates temporary Desktop navigation effects from
ordinary focus opacity. This implements the ownership and pacing work from
the [live investigation](effects-investigation.md) and
[Apple motion research](apple-motion-research.md).

## Navigation lifecycle

The daemon sends two bounded window lists: preparation before switching Space,
then recovery immediately after the switch, before application activation or
AXRaise. Recovery has one monotonic start time and duration for the whole list.
It reuses the alpha just applied during preparation, avoiding a synchronous
`SLSGetWindowAlpha` round trip per window. Custom opacity below the preparation
alpha is preserved. More than 400 affected windows disables the whole effect
instead of animating only part of a Desktop.

The focus-policy opcode leaves a navigation effect intact when its target is
unchanged. A different focus target continues from the last written alpha and
keeps the original completion time. Explicit opacity commands still cancel an
effect immediately. No claim is made that the last written alpha equals the
last presented frame.

Every navigation cancels obsolete effects on its destination display, including
rapid repeats and empty destinations. Cancellation restores the latest target;
effects on other displays continue. Preparation/start failure attempts immediate
restoration and is reported. Transport loss or a failed WindowServer write can
still prevent restoration; this protocol is not a compositor transaction.

Batch validation completes before any mutation: finite values, bounded length,
nonzero window/display IDs and unique window IDs. Batch replies explicitly
report success/failure. The existing Space-focus protocol's acknowledgement
does not establish that the destination application has rendered a frame.

## Curve and pacing

Navigation uses bounded smoothstep `3t² - 2t³`, without overshoot and with zero
endpoint slopes. A real target change rebases this curve from its current value;
it does not preserve velocity. Ordinary non-navigation fades retain cubic
ease-out `1 - (1 - t)^3` and their existing duration behavior.

The installed wrapper's 0.9 alpha and 150 ms duration remain the first comparison
baseline. At 60 Hz this spans about nine intervals. Removing premature focus
cancellation and changing the curve already changes perceived duration; the
final preference still needs comparison on the active release. There is no
claim that this is Apple's own curve or its chosen Desktop duration.

On macOS 14+, one `NSScreen` display link per active target display wakes the
shared worker. Callbacks use a nonblocking lock attempt, set a latest-frame flag
and signal; they never call SkyLight or wait for application focus. Multiple
callbacks cannot queue stale frames. Interpolation uses `CLOCK_MONOTONIC` at
worker execution, without mixing Core Animation timestamps into that clock.

Before callbacks arrive, or on older macOS, the fallback uses the display mode's
refresh rate (60 Hz if unspecified, capped at 240 Hz). After callbacks start, a
50 ms watchdog and the original endpoint deadline prevent a stalled run loop,
sleep or disconnect from leaving an indefinitely active effect. Idle links are
invalidated on the main run loop; the worker sleeps when its list is empty.
Late frames skip ahead. Scheduler and SkyLight delays still affect completion;
display callbacks are opportunities, not presentation fences or real-time guarantees.

Reduce Motion suppresses navigation fades, as do rapid repeats, visible
destinations and fullscreen Spaces. Navigation itself remains immediate.
Resolution/scaling settings are neither changed nor used to choose the curve.

## Verification of this revision

The pre-fix navigation test failed because window focus began before recovery.
It now passes for ordinary focus and the cross-display raise path. The unity
test also exercises the actual focus setter and socket encoding: focus policy,
explicit opacity and batch acknowledgement have distinct verified behavior.

`fade_tests` covers redundant focus, real retargeting, explicit cancellation,
shared epochs, display isolation, fallback cadence, missed callbacks, allocation
and system-call failure, concurrent requests and idle behavior. Debug and
ASan/UBSan passed 6/6; TSan passed fade and navigation-queue tests. Static analysis
passed both payload architectures and the daemon baseline. Two 60-second fuzz
campaigns passed, including the new opacity parsers with scheduling disabled.

The opt-in [display probe](../tests/fade/display.m) uses real AppKit callbacks
with simulated alpha writes and creates no windows. On this host's two 60 Hz
Dell U3223QE displays, it recorded 9–10 writes per window for a 150 ms recovery,
then zero retained links. Stalling its main run loop also recovered; TSan passed.
An isolated lcs.11 timer baseline with two simulated windows recorded 33–34
total writes in each of five trials, versus 18–20 for the new probe. These are
call counts, not rendered frames, CPU/GPU measurements or live Dock acceptance.

Raw local logs are under ignored `build/lcs12-*.log`, with the timer comparison
in `build/lcs11-cadence-baseline.log`. After activation, repeat the alpha A/B,
fixed navigation workload, WindowServer/daemon CPU and rendered-frame checks.
Include reversals, held keys, empty Desktops, Edge across displays and actual
monitor sleep/reconfiguration. Live payload display-link behavior and visual
acceptance remain separate gates from the isolated probe.

## Native compositor investigation

The reconstructed [Mousecape SkyLight declarations](https://github.com/alexzielenski/Mousecape/blob/master/Mousecape/mousecloak/CGSInternal/CGSWindow.h)
include a duration for `CGSSetWindowListAlpha`. Inspection of local macOS 27.2
confirmed forwarding to WindowServer, but an owned-window probe did not establish
rendered interpolation, interruption or behavior from Dock on foreign windows.
That path and spatial spring effects remain experimental.
