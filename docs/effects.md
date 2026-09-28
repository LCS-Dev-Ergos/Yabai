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
Desktop 1; the [lcs.23 report](effects-lcs23-validation.md) records the comparison.

ScreenCaptureKit capture requires macOS 15.2+ and existing Screen Recording
permission. The navigation path never requests permission. Unsupported or
refused capture, allocation failure and capture timeout all fall back to an
ordinary Desktop switch. There is one capture in flight and one overlay
globally, including across displays. A timed-out callback can only release its
image; it cannot later create a window. A callback that never comes would have
disabled the crossfade until yabai restarted; after two seconds that capture
counts as lost and a new one may start. A 24-million-pixel limit bounds the
accepted image size. The 150 ms deadline includes starting the capture, but
cannot interrupt the framework's call itself; it is not a hard bound on the
whole navigation request.

The image is drawn into the window's backing before the window is shown: a
Core Animation remote surface presented uninitialized white frames during
rapid preparation and cancellation (see the
[lcs.24 report](effects-lcs24-validation.md)). The window takes the capture's
colour space, the display's own profile, before its context is created.
Without it every pixel was converted to the window's default space: the draw
of a 4112 × 2658 capture took 46–69 ms, against 15–18 ms without conversion,
and colours outside that space would have been clipped, so the image would
not have matched the screen it covers. The auxiliary Space that holds the
window is created for each snapshot; the daemon recognises its own Spaces when
WindowServer announces them, instead of asking for their type while a switch
is under way (24–41 ms on the event loop for every step of a burst).

A bounded timer applies smoothstep to the owned window's alpha, using the
display mode's reported interval (60 Hz when unspecified, capped at 240 Hz).
The timer releases everything at the endpoint; a one-second watchdog also
retires an overlay if Dock has not replied. This is not display-link
synchronization or a presentation fence. Fading the CALayer's opacity itself
was rejected after captured frames showed a luminosity dip on the test host.

Preparation runs on the daemon's event loop and holds it: on the MacBook
display, capture took 26–63 ms warm and 76–150 ms for a process's first
capture (one of them past the deadline), and window, draw and Space together
about 20 ms, before the one-frame presentation opportunity. A
signpost in category `effects` records each snapshot's capture and preparation
time and, when none was used, why.

New navigation, mouse input, other non-query commands, display reconfiguration,
Mission Control, Dock restart and wake cancel the overlay. A click during
capture cancels the pending step before switching. One bounded refresh
opportunity precedes the switch; it does not prove the image was presented.
The outgoing frame is a still image, so video and changing application content
freeze within that image for the short blend.

The pacing lets each blend end before the next step starts; steps queued
behind others, and those a held key repeats, blend in 125 ms at most (see
[navigation](navigation.md#pacing)). With pacing disabled, interruption
discards the previous image; seamless retargeting in that mode is not promised. Reduce Motion retains this nonspatial
blend. Fullscreen Desktops and already-visible destinations switch without it.
No blur is applied. The payload's legacy crossfade opcode remains for protocol
compatibility, but this daemon no longer calls it.

### Legacy Space crossfade (through lcs.22)

The following describes the retained payload implementation, not the current
daemon renderer.

Payload `2.1.31-lcs.10` can crossfade the whole display, requested with
`crossfade` in place of the starting opacity (see [navigation](navigation.md)).
The window fade dims the destination's windows, so the wallpaper shows through
them: at alpha 0.7 on this host, VS Code's dark background turned brown-orange
over the wallpaper's bright cloud before it darkened again, which reads as a
flash. Every Desktop has its own wallpaper window (11 `Wallpaper` windows of
WindowManager for 11 Desktops), at a level below the desktop window level.

WindowServer applies a Desktop's alpha to each of its windows, not to the
Desktop as a whole: with the destination at alpha 0.5, its windows are
half transparent over its own wallpaper, which shows through them with a
weight of up to a quarter. On lcs.20 a crossfade from ChatGPT to Edge raised
the red channel of the centre of the screen up to 11 levels above both
Desktops, against 15 for the window fade. Payload `2.1.31-lcs.11` turns the
destination's wallpaper window transparent for the crossfade when the
destination's ordinary windows cover at least 80% of the display; with the
wallpaper hidden by hand, the same crossfade stayed within both Desktops on
every channel. A Desktop with less covered, or empty, keeps its wallpaper and
fades in whole, since hiding it would show the Desktop below around its
windows until the end.

Dock animates its own Space transitions this way. Its binary imports
`SLSTransactionSetSpaceAlpha`, `SLSTransactionSetSpaceAbsoluteLevel`,
`SLSTransactionShowSpace` and `SLSTransactionSetSpaceTransform`; its code
computes a Desktop's alpha from the progress of an animation and ends by
restoring level 0 and alpha 1, the values every Desktop has at rest.

One transaction sets the destination's alpha to 0, shows it and makes it
current; the Desktops below are shown again in the same transaction, in case
the change of current Desktop hid them. The `2.1.31-lcs.12` candidate keeps the
destination at level 0 and lowers older layers to -1/-2. Earlier versions
raised it above level 0, allowing an empty destination's wallpaper to obscure
global windows such as SketchyBar. The new ordering still needs live validation
for the bar and Finder's shared Desktop windows; see the
[candidate report](effects-lcs22-validation.md).
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
is skipped. Since `2.1.31-lcs.12`, a separate empty transaction is reserved
before starting. Completion and cancellation use it, so allocation failure
after the first frame no longer discards the state without restoring Desktops.

The same revision measures the union of window rectangles instead of adding
overlapping areas. The bounded scan uses at most 128 rectangles; excess windows
can underestimate coverage, keeping the wallpaper visible. A window at desktop
level is eligible for wallpaper alpha changes only if its owner executable is
the system WindowManager. An unreadable or different owner is left untouched.
The 80% threshold remains a heuristic: transparent content and the uncovered
margin still need visual checks, and wallpaper writes are separate from Space
transactions.

No commit is checked, since `SLSTransactionCommit` returns no status. Payload
`2.1.31-lcs.9` read its value as one and took every first transaction for a
refusal: the daemon switched without an effect, and the destination stayed at
alpha 0 and level 1, black whenever it was shown. Dock's own switches and
Mission Control leave a Desktop's alpha and level as they are, so since
`2.1.31-lcs.10` the payload, when it loads, puts every user Desktop that is not
at alpha 1 and level 0 back there and logs how many it restored; on lcs.20 it
restored the four Desktops lcs.19 had left transparent. Since `2.1.31-lcs.11`
it also makes their wallpaper windows opaque again.

Crossfades share the window fades' lock, worker and display links, including the
half-frame fallback. Reduce Motion keeps them, since its own Desktop transition
is a crossfade. Fullscreen Desktops, and destinations already visible on another
display, switch without one.

Signposts in subsystem `com.lcs.yabai`, category `effects`, mark each
crossfade's start, turns and end and every frame written, crossfade or window
fade; [testing](testing.md) describes how to record them.

`fade_tests` checks the transaction order, monotonic alpha, levels, the end
state, turns, stacking, missing transactions, the restore at load, the hidden
wallpaper, the worker's deadline and display pacing against a model of
WindowServer's Desktops, whose commit, like SkyLight's, never returns 0. None
of this establishes how WindowServer composites two Desktops shown at once, or
the frames it presents: that is the live check, described in
[performance](performance.md).

## Native compositor investigation

The reconstructed [Mousecape SkyLight declarations](https://github.com/alexzielenski/Mousecape/blob/master/Mousecape/mousecloak/CGSInternal/CGSWindow.h)
include a duration for `CGSSetWindowListAlpha`. Inspection of local macOS 27.2
confirmed forwarding to WindowServer, but an owned-window probe did not establish
rendered interpolation, interruption or behavior from Dock on foreign windows.
That path and spatial spring effects remain experimental.
