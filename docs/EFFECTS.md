# Opacity effects

Payload `2.1.31-lcs.6` separates temporary Desktop navigation effects from
ordinary focus opacity. This implements the ownership and pacing work from
the [live investigation](reports/Opacity-Conflict_7.1.25-lcs11.md) and
[Apple motion research](research/DESKTOP-MOTION.md).

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
the worker writes a frame itself when a callback is half a frame late, then one
per interval while callbacks stay late, and the original endpoint deadline
still ends the effect. Dock's main thread delivers the callbacks and is busy
right after a Space switch: with the former 50 ms watchdog, lcs.7 fades stood
still for about 54 ms just after they started, and until payload
`2.1.31-lcs.11` the worker waited half a frame before every late frame, so
some crossfades on lcs.20 ran at about 39 frames a second from start to end.
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

The daemon's `crossfade` path now captures one composed frame of the outgoing
display, places it in a noninteractive window owned by yabai, switches Desktop
normally, and fades that image out over the destination. It preserves the full
Desktop blend without changing Space alpha/levels or the alpha of Finder,
wallpaper, SketchyBar or application windows. The installed lcs.22 Space-based
path reproduced both disappearing Finder icons and a black transition into
Desktop 1; the [lcs.23 report](reports/Snapshot-Crossfade_7.1.25-lcs23.md) records the comparison.

Snapshot capture now uses the macOS 26+ `captureScreenshotWithRect` API, with
SDR output in the local display's colour space and explicit physical pixel
dimensions. It requests the cursor, whose visual match with the previous
API still needs a live check. Earlier macOS versions omit this optional
effect: the prior rectangle API produced cumulative WindowServer memory growth
on the tested macOS 27.2 host, and no older-system replacement has been
validated. See the [capture backend report](reports/Crossfade-Capture-Backend_8.0.0-lcs1.md).
Existing Screen Recording permission is required; the navigation path never
requests permission. Unsupported or
refused capture, allocation failure and capture timeout all fall back to an
ordinary Desktop switch. There is one capture in flight and one overlay
globally, including across displays. A timed-out callback can only release its
image; it cannot later create a window. A callback that never comes would have
disabled the crossfade until yabai restarted; after two seconds that capture
counts as lost and a new one may start. A 24-million-pixel limit bounds the
accepted image size. The 150 ms deadline includes starting the capture, but
cannot interrupt the framework's call itself; it is not a hard bound on the
whole navigation request. An image that arrived in time stays usable however
late the event loop comes to it.

The image is drawn into the window's backing before the window is shown: a
Core Animation remote surface presented uninitialized white frames during
rapid preparation and cancellation (see the
[lcs.24 report](reports/Snapshot-Continuity_7.1.25-lcs24.md)). The window takes the capture's
colour space, the display's own profile, before its context is created.
Without it every pixel was converted to the window's default space: the draw
of a 4112 × 2658 capture took 46–69 ms, against 15–18 ms without conversion,
and colours outside that space would have been clipped, so the image would
not have matched the screen it covers. The auxiliary Space that holds the
window is created for each snapshot; the daemon recognises its own Spaces when
WindowServer announces them, instead of asking for their type while a switch
is under way (24–41 ms on the event loop for every step of a burst).

A bounded timer applies the [fade curve](#fade-curve), smoothstep by default,
to the owned window's alpha, using the display mode's reported interval (60 Hz
when unspecified, capped at 240 Hz).
The timer releases everything at the endpoint; a one-second watchdog also
retires an overlay if Dock has not replied. This is not display-link
synchronization or a presentation fence. Fading the CALayer's opacity itself
was rejected after captured frames showed a luminosity dip on the test host.

A queued step requests the capture and returns. The capture's callback, or
its deadline, posts `SPACE_NAVIGATION_CAPTURED`; the event loop then draws the
image, switches and activates. Other events run meanwhile, and the schedule
starts no other step until this one ends. With pacing off a request still
prepares synchronously and holds the event loop through the capture. On the
MacBook display, capture took 26–63 ms warm and 76–150 ms for a process's
first capture (one of them past the deadline); window, draw and Space
together, about 20 ms, and the one-frame presentation opportunity still run
on the event loop. A signpost in category `effects` records each snapshot's
capture and preparation time and, when none was used, why.

New navigation, mouse input, other non-query commands, display reconfiguration,
Mission Control, Dock restart and wake cancel the overlay, and the capture a
step still waits for. That step then stops before switching, as it does after
a click, Mission Control or a display animation since the capture, and the
queue behind it is dropped; another command abandons it together with the
queue. One bounded refresh opportunity precedes the switch; it does not prove
the image was presented.
The outgoing frame is a still image, so video and changing application content
freeze within that image for the short blend.

The pacing lets each blend end before the next step starts; steps queued
behind others, and those a held key repeats, blend in 125 ms at most (see
[navigation](NAVIGATION.md#pacing)). With pacing disabled, interruption
discards the previous image; seamless retargeting in that mode is not promised. Reduce Motion retains this nonspatial
blend. Fullscreen Desktops and already-visible destinations switch without it.
So does every step while macOS reports memory pressure (warning or critical,
`kern.memorystatus_vm_pressure_level`): under pressure WindowServer once put
the overlay on screen about 110 ms after it was ordered, after Dock had
switched, so the destination showed before the outgoing image returned and
faded. The capture and the overlay's copy take 81 MB each on a 6016 × 3384
display. The veil captures nothing and is kept. The white seen in the same
conditions came from a browser's window on the destination, with or without
the crossfade (see the [flash report](reports/Navigation-Flash_8.0.0-lcs4.md)).
No blur is applied.

### Fade curve

The crossfade's and the veil's alpha follows one of two curves, chosen with
`yabai -m config navigation_fade_curve` (`smooth` by default). Both run over
the requested duration and are read when the overlay is created, so changing
the setting affects the next overlay only.

- `smooth` is smoothstep, `1 - (3t² - 2t³)`. It starts slowly: 5% of the change
  takes 13.5% of the duration.
- `ease_out` is `(1 - t)²`. It changes visibly from the first frame, 5% of the
  change taking 2.5% of the duration, so the switch reads as answered sooner.

The window fade of the destination (the opacity effect above) keeps its own
curve.

### Space-alpha crossfade (removed)

Through lcs.22 the payload crossfaded Desktops itself, as Dock animates its own
Space transitions: one SkyLight transaction per frame set the destination's
alpha, level and visibility. The snapshot crossfade replaced it, and payload
`2.1.31-lcs.13` removed it along with opcode `0x16`, which stays reserved. What
it established still holds:

- WindowServer applies a Desktop's alpha to each of its windows, not to the
  Desktop as a whole, so a half-transparent destination shows its own wallpaper
  through its windows.
- `SLSTransactionCommit` returns no status: on macOS 27.2 its value is a
  pointer, never 0. A transaction's effect can only be read back or seen in
  captured frames, which is why window fades write each window on its own.
- A Desktop left at another alpha or level stays that way: Dock's own switches
  and Mission Control do not reset them.

The [lcs.22 report](reports/Space-Crossfade_7.1.25-lcs22.md) has the measurements.

## Desktop veil

`veil` is the effect for when the capture is the cost. The crossfade's capture
takes 70-100 ms, a floor of the platform, before anything changes on the
screen. The veil skips it: a solid black window, created and ordered exactly
like the crossfade's overlay (an auxiliary Space of its own, no activation or
mouse events), appears at 0.4 opacity; Dock then switches Desktop, and the veil
fades out over the destination. Nothing is captured, so it needs neither Screen
Recording permission nor macOS 26, and no image is held. It shares the
crossfade's overlay, timer, watchdog, cancellation and Space-notification
handling, one overlay at a time, and its fade follows
[`navigation_fade_curve`](#fade-curve).

A standalone probe (`tools/effects/veil_probe.m`) measured the first visible
response after 35-60 ms, against about 280 ms for the crossfade, and the
transition complete after 255-295 ms, against about 455 ms. Opacity 0.3 and 0.5
were not perceptibly different from 0.4.

Two display refreshes pass between ordering the veil and asking Dock to switch
(`min(2 × interval, 1/30 s)`; 33 ms at 60 Hz). WindowServer can present a new
window on a new auxiliary Space after Dock has switched: switching straight
after the order showed the destination unveiled in 2 of 6 trials, while
waiting 17 or 34 ms did so in none of 18. Two refreshes covered every flash
observed, but the wait is not a presentation fence, and the veil's window may
still reach the screen late. A veil still shown when Dock fails, or that Dock
never answers, goes with the crossfade's one-second watchdog.

The veil follows the crossfade's conditions: a duration (a quick burst has
none), a destination that is hidden, and ordinary Desktops on both sides.
Reduce Motion keeps it, as it keeps the crossfade, whose own Desktop
transition under Reduce Motion is a crossfade.
A veil step runs to its end within one request: unlike a queued
crossfade it waits for no capture and never leaves the schedule pending.
Mission Control and a display animation stop it when the step is planned; a
click during its two refreshes removes the veil and stops the step, as it does
during a synchronous crossfade's capture. Window moves keep their effect.

### Veil background blur

`yabai -m config navigation_veil_blur RADIUS` (0 to 100, `0` and off by
default) makes the veil blur what lies below it, so the switch underneath
stays soft. WindowServer does the blur on the GPU and live
(`SLSSetWindowBackgroundBlurRadiusStyle`, style 1); nothing is captured. The
radius is read when the veil is prepared. With a radius above 0 the veil is:

- a window at alpha 0 when it is ordered in, filled with black at a tint of
  0.25 (the fill's own alpha, so the blurred Desktop shows through), whose
  window alpha carries the blur and the darkening;
- faded in on the event loop over 100 ms, a quadratic ease-out
  (`1 - (1 - u)²`), one alpha write per display refresh, each under the
  overlay's lock and only while this veil is still the active one: a
  cancellation during the fade-in stops it and the step switches without a
  veil;
- followed by the plain veil's two refreshes, after which Dock switches. It
  then fades out from alpha 1 with [`navigation_fade_curve`](#fade-curve).

The fade-in comes before the switch because of what was measured with the
blur at full strength straight away: it popped in, the destination's change in
brightness still showed as a jump, and the blur doubled Dock's switch time
(about 65 to 135 ms) by contending for the GPU. Once the blurred window also
reached the screen later than the two refreshes. Showing it completely first
keeps both the pop and the contention out of the switch, at the price of the
event loop being held for about 130 ms at 60 Hz (100 ms of fade-in, up to one
refresh of rounding and the two refreshes), against 33 ms for the plain veil.
That cost, and the GPU load of a live blur, are why the option is off by
default. A radius of 0 is exactly the plain veil: opacity 0.4, an opaque fill
and no fade-in.

With Reduce Transparency on, the veil is the plain one whatever the radius.
The trace in subsystem `com.lcs.yabai`, category `effects`, reports
`veil blur` for a blurred veil and `veil` for a plain one. A failure to set
the blur is a failure to set up the veil, like any other, and the step
switches without it.

## Native compositor investigation

The reconstructed [Mousecape SkyLight declarations](https://github.com/alexzielenski/Mousecape/blob/master/Mousecape/mousecloak/CGSInternal/CGSWindow.h)
include a duration for `CGSSetWindowListAlpha`. Inspection of local macOS 27.2
confirmed forwarding to WindowServer, but an owned-window probe did not establish
rendered interpolation, interruption or behavior from Dock on foreign windows.
That path and spatial spring effects remain experimental.
