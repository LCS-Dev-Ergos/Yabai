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
effects on other displays continue. Only navigation starts display effects, so
the daemon records when each may still run: its duration plus 100 ms for the
worker's watchdog and scheduling. A preparation with nothing to dim then skips
its synchronous Dock round trip once the display's last effect has ended. Preparation/start failure attempts immediate
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
shared worker. Callbacks take no lock: they set their display's bit in an atomic
frame mask and signal a counting semaphore, so a frame that arrives while the
worker or a request holds the lock is kept rather than left to the watchdog.
Only disposing of an idle link tries the lock. Callbacks never call SkyLight or
wait for application focus, and multiple callbacks cannot queue stale frames. Interpolation uses `CLOCK_MONOTONIC` at
worker execution, without mixing Core Animation timestamps into that clock.

Before callbacks arrive, or on older macOS, the fallback uses the display mode's
refresh rate (60 Hz if unspecified, capped at 240 Hz). After callbacks start,
the worker writes a frame itself when a callback is half a frame late, and the
original endpoint deadline still ends the effect. Dock's main thread delivers
the callbacks and is busy right after a Space switch: with the former 50 ms
watchdog, lcs.7 fades stood still for about 54 ms just after they started.
A stalled run loop, sleep or disconnect cannot leave an active effect. Idle links are
invalidated on the main run loop; the worker sleeps when its list is empty.
Late frames skip ahead. Scheduler and SkyLight delays still affect completion;
display callbacks are opportunities, not presentation fences or real-time guarantees.

Each window gets its own write per frame, which reports its own error: a
window that closed ends its fade. Payloads `2.1.31-lcs.7` to `lcs.9` put the
windows of a frame in one SkyLight transaction and returned to individual
writes for good once a commit looked refused. `SLSTransactionCommit` returns no
status, though: on macOS 27.2 its value is a pointer, never 0, and Dock ignores
it. The first frame with two windows after Dock started therefore always
switched those payloads to individual writes; `2.1.31-lcs.10` keeps only them.
The worker runs at user-interactive QoS instead of inheriting Dock's.

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
Payload `2.1.31-lcs.7` added tests for one transaction per frame and the
fallback after a refused commit, both gone with the transactions in
`2.1.31-lcs.10`, and for a frame recorded while the lock is held; the probe
still records 10 writes per window.
An isolated lcs.11 timer baseline with two simulated windows recorded 33–34
total writes in each of five trials, versus 18–20 for the new probe. These are
call counts, not rendered frames, CPU/GPU measurements or live Dock acceptance.

Raw local logs are under ignored `build/lcs12-*.log`, with the timer comparison
in `build/lcs11-cadence-baseline.log`. After activation, repeat the alpha A/B,
fixed navigation workload, WindowServer/daemon CPU and rendered-frame checks.
Include reversals, held keys, empty Desktops, Edge across displays and actual
monitor sleep/reconfiguration. Live payload display-link behavior and visual
acceptance remain separate gates from the isolated probe.

## Desktop crossfade

Payload `2.1.31-lcs.10` can crossfade the whole display, requested with
`crossfade` in place of the starting opacity (see [navigation](navigation.md)).
The window fade dims the destination's windows, so the wallpaper shows through
them: at alpha 0.7 on this host, VS Code's dark background turned brown-orange
over the wallpaper's bright cloud before it darkened again, which reads as a
flash. Every Desktop has its own wallpaper window (11 `Wallpaper` windows of
WindowManager for 11 Desktops), so a Desktop is an opaque layer, and blending
two of them never shows the wallpaper.

Dock animates its own Space transitions this way. Its binary imports
`SLSTransactionSetSpaceAlpha`, `SLSTransactionSetSpaceAbsoluteLevel`,
`SLSTransactionShowSpace` and `SLSTransactionSetSpaceTransform`; its code
computes a Desktop's alpha from the progress of an animation and ends by
restoring level 0 and alpha 1, the values every Desktop has at rest.

One transaction sets the destination's alpha to 0, puts it a level above the
Desktops shown, shows it and makes it current; the Desktops below are shown
again in the same transaction, in case the change of current Desktop hid them.
Each frame then commits one transaction with the destination's alpha
(smoothstep), and the last one hides the other Desktops and restores alpha 1 and
level 0. The daemon's request returns once the destination is current, and
activation follows at once.

A navigation during a crossfade puts its destination on top, up to three
layers, so the blend on screen continues without a jump. Going back to the
Desktop underneath fades the top one out from where it is, at the same pace;
going forward again turns it around. A fourth Desktop, or one deeper in the
stack, first ends the crossfade where it was heading. Every other Desktop
operation (focus without an effect, create, destroy, move) first ends all
crossfades. When SkyLight cannot create a transaction at the start, the screen
stays as it was and the daemon switches without an effect; a frame without one
is skipped.

No commit is checked, since `SLSTransactionCommit` returns no status. Payload
`2.1.31-lcs.9` read its value as one and took every first transaction for a
refusal: the daemon switched without an effect, and the destination stayed at
alpha 0 and level 1, black whenever it was shown.

Crossfades share the window fades' lock, worker and display links, including the
half-frame fallback. Reduce Motion keeps them, since its own Desktop transition
is a crossfade. Fullscreen Desktops, and destinations already visible on another
display, switch without one.

Signposts in subsystem `com.lcs.yabai`, category `effects`, mark each
crossfade's start, turns and end and every frame written, crossfade or window
fade; [testing](testing.md) describes how to record them.

`fade_tests` checks the transaction order, monotonic alpha, levels, the end
state, turns, stacking, missing transactions, the worker's deadline and display
pacing against a model of WindowServer's Desktops, whose commit, like
SkyLight's, never returns 0. None of this
establishes how WindowServer composites two Desktops shown at once, or the
frames it presents: that is the live check.

## Native compositor investigation

The reconstructed [Mousecape SkyLight declarations](https://github.com/alexzielenski/Mousecape/blob/master/Mousecape/mousecloak/CGSInternal/CGSWindow.h)
include a duration for `CGSSetWindowListAlpha`. Inspection of local macOS 27.2
confirmed forwarding to WindowServer, but an owned-window probe did not establish
rendered interpolation, interruption or behavior from Dock on foreign windows.
That path and spatial spring effects remain experimental.
