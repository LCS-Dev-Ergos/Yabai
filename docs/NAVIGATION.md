# Space navigation with a fade

This fork adds one daemon request for keyboard and bar navigation:

```sh
yabai -m space --navigate focus next
yabai -m space --navigate focus next crossfade 0.25
yabai -m space --navigate focus next veil 0.25
yabai -m space --navigate focus prev 0.95 0.1
yabai -m space --navigate focus 3 crossfade 0.25
yabai -m space --navigate move 3 0.95 0.1
```

Arguments are the action (`focus` or `move`), a space selector and, optionally,
the effect and its duration in `[0,1]` seconds. The effect is `crossfade`, a
[crossfade of the display](EFFECTS.md#desktop-crossfade), `veil`, a
[veil over the display](EFFECTS.md#desktop-veil), or the starting
opacity in `(0,1]` of the destination's windows. A zero duration disables it.
A request that names neither takes both from the daemon's settings when it is
read, so a key binding can stay the same while they change.

### Effect settings

| Setting | Values | Default | Applies to |
| --- | --- | --- | --- |
| `navigation_effect` | `on`, `off` | `on` | every step: `off` shows no effect, whatever the request named, and still switches |
| `navigation_effect_type` | `crossfade`, `veil` | `crossfade` | requests that name no effect |
| `navigation_effect_duration` | seconds in `[0,1]` | `0.25` | requests that name no effect |
| `navigation_pressure_fallback` | `veil`, `keep`, `none` | `veil` | crossfade steps while macOS reports memory pressure (see [effects](EFFECTS.md#desktop-crossfade)) |
| `navigation_fade_curve` | `smooth`, `ease_out` | `smooth` | how the crossfade and the veil fade out (see [effects](EFFECTS.md#fade-curve)) |
| `navigation_veil_blur` | `0` to `100` | `0` | blur below the veil, at about 130 ms on the event loop (see [effects](EFFECTS.md#veil-background-blur)) |

Each is read and set with `yabai -m config` and lasts until the daemon exits;
the configuration file sets them at start. `navigation_effect` and
`navigation_pressure_fallback` are read when a step starts, so they also reach
steps already queued. The type and duration are taken when a request is read,
the curve and the blur when an overlay is created.
`next` and `prev` wrap to the first/last space, like the previous shell
script. Other selectors follow the ordinary space selectors.
`move` sends the focused window to the destination and follows it. It does
not move or reorder the Desktop itself; that is what `space --move` and
`space --display` do.

The daemon resolves the selector, optionally moves the window, finds the
frontmost eligible window, sets opacity, switches space and focuses the
window within one event-loop request. The active display is read from
WindowServer, and the already frontmost window is focused without an AXRaise
round trip. One `SLSCopyManagedDisplaySpaces` reply per request answers the
Desktop order, displays, visibility and type; a Desktop it does not know is
asked about individually. It does not spawn `jq` or child yabai
clients, and never changes `window_opacity_duration` or per-window overrides.
The `move` action still raises the moved window at its destination.
Custom opacity is preserved, including windows already dimmer than the fade's
starting value. Hidden, minimized and sticky windows are excluded.

Only a hidden destination can fade. A destination already visible on another
display is focused without dimming its windows. Fullscreen destinations do
not fade, and moving windows into or out of native fullscreen is rejected.
Selecting the current space is a successful no-op that preserves focus.

On repeated navigation less than 180 ms apart, the daemon skips starting a
new fade and still executes the navigation. There is no sleeping debounce or
animation queue; requests that arrive while another waits are merged as
described below. Once navigation pauses, fades resume. An already running fade
may finish within its requested duration.

The scripting addition must be available. A failed space-focus request
restores any dimmed windows immediately instead of falling back to a gesture.
As with separate move/focus commands, a successful window move is not rolled
back if the subsequent focus request fails.

## Pacing

Requests can arrive faster than a switch completes: a held key repeats every
30 ms, and a busy Edge or VS Code handles an activation hundreds of
milliseconds after it was sent. An activation handled after the user had moved
on brought its Desktop back into view. Navigation therefore runs as steps of one
Desktop from a bounded queue, in the order requested:

- A step runs when at least 100 ms have passed since the previous step
  completed, including synchronous focus work. A crossfade or veil also
  reserves its duration from Dock's acknowledgement, so a blend always ends
  before the next one starts. This time guard does not establish frame
  presentation.
- After a step that activated an application, the next one also waits until
  that application reports the window focused, or 150 ms have passed. A focus
  confirmation schedules the next step after its event handler finishes.
- `next` and `prev` count from the Desktop the previous step switched to. Each
  Desktop number is one step; the same number twice in a row queues once.
- Only the last queued step activates an application. The steps before it
  switch Desktop and show their effect. When the last step finds its Desktop
  current already, as a jump over a whole lap of Desktops or the number of the
  Desktop the steps before it reached, it still gives that Desktop's frontmost
  window focus, unless something emptied the queue in between.
- A crossfade step that finds more steps queued when it comes to activate,
  presses that arrived while it captured, leaves the activation to the last of
  them, unless it moved a window. A held key's steps, and the first press of a
  quick burst, therefore neither activate an application nor wait for its
  focus. Should opposite presses then empty the queue, the Desktop reached
  takes focus once 150 ms have passed without a request, without a switch; a
  click or another command in that time decides the focus instead.
- A veil step captures nothing, so it never waits for the schedule: it shows the
  veil, waits two display refreshes for it to reach the screen (about 33 ms at
  60 Hz), switches and activates within one run, as a window fade does. It
  follows the rules of a crossfade step otherwise: a burst's quick steps, a
  visible or fullscreen destination and a fullscreen source show no veil.
  Neither does a destination without a window to show.
- A step with more queued behind it, and a step a held key repeats, blend in
  125 ms at most; the last of separate presses keeps the requested duration.
  The first step of a burst keeps it too: it starts before the next press is
  known, and a running blend is never shortened. Each crossfade step first
  spends about 75 ms capturing and preparing the outgoing image (see
  [effects](EFFECTS.md#desktop-crossfade)). On the MacBook display a burst
  ran one step every 380 ms on lcs.24. On two 4K displays steps ran 300–460
  ms apart on lcs.29: capturing and preparing the 20-million-pixel image took
  about 110 ms of each, Dock's switch and the activation about 100 ms, and the
  blend of the step before the rest; see [performance](PERFORMANCE.md).
- A valid, accepted `focus next`, `focus prev` or numeric `focus` request
  arriving within 400 ms of the previous accepted focus request starts a quick
  burst: every switch queued from then on, the last included, shows no effect
  and waits only for the 100 ms rhythm, and the next switch ends the blend of
  the step before. On two 4K displays a crossfade step takes 300–460 ms, more
  than such presses leave between them. The bound comes from recorded key
  presses: 150 of them came 450–600 ms apart when meant to look at each
  Desktop, and 150–350 ms apart in quick runs. A held key's repeats are quick
  presses. Numeric requests keep their absolute destinations and do not join
  relative ingress groups; numeric and relative requests share the burst clock.
  The clock uses ingress times, including the shortest gap inside a relative
  group, so event-loop delays do not hide a burst. Invalid selectors/effects and
  refused requests do not change it. A window `move` keeps its effect and breaks
  the focus burst clock.
- At most four switches wait, which keeps navigation within about 1.5 s of
  the last press on two 4K displays. No press is dropped for that: `next` or
  `prev` steps beyond the four join a jump at the end of the queue, one
  switch over several Desktops, a Desktop number takes the place of the last
  switch, and `next` or `prev` after a Desktop number jump on from that
  Desktop. Only a `move`, or a request with another effect, can still be
  refused when the queue is full.
- A click after a request, or any other command except queries, empties the
  queue. If a validated focus request arrives after a click while the old
  capture is pending, acceptance is checked first; the old capture and queued
  work are then retired and only the new request survives. It does not switch
  to or focus the abandoned destination. A refused request cannot retire the
  capture, and late callbacks cannot revive it. Mission Control, display
  animation and window moves retain their cancellation policy. A failed step
  drops the rest.

The client gets its answer when its request is queued; failures of later steps
go to the debug log. `yabai -m config space_navigation_pacing off` runs each
request at once instead, as before, with merged relative requests moving
several Desktops in one switch.

Signposts in subsystem `com.lcs.yabai`, category `navigation`, mark each
request, step, activation and the focus that confirms it.

The client of a request that joins a full queue as a jump, or replaces its
last switch, still gets success; only a refused request fails with
`navigation queue is full.`

## Relative navigation

`next` and `prev` count from the Desktop the previous navigation switched to,
for up to one second, while that Desktop is still visible. WindowServer's
active display can briefly follow an application to a window it has on
another display: a Chromium browser activated on one display makes its window
on the other display key. Counting from that display wrapped `next` back to
Desktop 1. A mouse click or any other daemon command except queries ends this,
and so does the second: the active display then decides again, so a Desktop
chosen by clicking, Command-Tab or `window --focus` is where counting starts.

When the destination's application has another window visible on a different
display, navigation activates the application on the destination window and
raises it through Accessibility, which such an application honors. The raise
completes before the next request runs, so it cannot pull a Desktop back into
view. Unlike `window --focus`, it posts no synthesized click: Edge, busy with
the switch, handled that click hundreds of milliseconds late, after rapid
navigation had moved on, and the click activated Edge again and brought its
Desktop back. An application that is already active keeps the raise of
`window --focus`, click included: activation alone does not move its key
window, and Edge kept keyboard focus on its window of the Desktop left behind.
Other applications keep the cheaper focus without a raise. A tiled window's
Desktop is the Space of its view, so only floating and unmanaged windows need
WindowServer: one query lists the application's window numbers on the Desktops
visible on other displays. The decision is made before the switch, because
afterwards WindowServer is busy showing the new Desktop and each synchronous
call waits for about a frame.

Between two windows of the application that is already active, upstream
leaves 40 ms between deactivating the old window and activating the new one,
because some applications miss a focus change whose events arrive together
([#2694](https://github.com/asmvik/yabai/pull/2694)). Until then it left 10 ms.
The destination window shows inactive until the activation lands, which reads
as a title-bar flash, so navigation uses 10 ms. It does not sleep in the event
loop: the activation runs as a later event, dropped if another navigation or
command comes first, or if focus moves to another application. Focus moving to
another window of the same application does not drop it: that is usually an
earlier navigation's activation handled late by a busy application, and
without the new activation its Desktop would come back into view.

Navigation records the window it focused, so the activation handler that
follows uses it instead of asking the application, which is busy with the
switch, for its focused window. A later focus notification from the
application still replaces it.

A navigation waits on WindowServer and the applications involved for tens of
milliseconds, while a held key repeats every 30 ms. The daemon's accept thread
lets a `focus next|prev` request join the one already waiting in the event
queue and answers it at once, so each uninterrupted group has one waiting
request. A repeat, arriving
less than 75 ms after the previous relative request, keeps the pending step; a
separate key press adds one, and the other direction takes one back. Each
request reaches the daemon through skhd, a shell and the yabai client, whose
start varies with load: with a 50 ms bound, repeats sent 30 ms apart during a
switch counted as presses and a held key went on for two Desktops after its
release. Presses a person makes in a row are at least about 100 ms apart, but
load compresses those gaps too: two presses 100 ms apart arrived 70 ms apart
and the second was lost as a repeat. A held key is not released between its
repeats, so a request after a key release, up to 150 ms before the previous
request since that one can reach the daemon late, is a press however soon it
came. Requests sent without a key, as by a script, only have the timing. A group
that a repeat starts is marked, and the pacing queue keeps at most one such
step pending, so a held key moves one Desktop per step and stops within one
step of its release, while three quick presses still move three Desktops.
Any other request closes the group,
so requests keep their order. Later repeats form a new group even while an
earlier closed group still waits. This keeps queries from disabling merging
for all subsequent repeats, without moving navigation across a query or
another command. Absolute selectors and `move` are not merged. A merged
client's successful return acknowledges queuing, not completed navigation.

## Integration

A key binding sends the action and selector only, and leaves the effect to
the settings:

```sh
exec "$yabai_msg" space --navigate "$action" "$selector"
```

Other bindings change the settings while the daemon runs, for instance:

```sh
yabai -m config navigation_effect off
yabai -m config navigation_effect_type veil
yabai -m config navigation_fade_curve ease_out
```

A release before 8.0.0 refuses a request without effect and the four effect
settings, so update the package and the bindings together. The current `crossfade` renderer runs
in the daemon and uses the ordinary payload Space-focus operation. It requires
macOS 15.2+ and existing Screen Recording permission; unavailable capture falls
back to an ordinary switch. The `veil` needs neither: it works wherever the
Space-focus operation does. The payload's own Space-alpha crossfade was removed
in `2.1.31-lcs.13` (see [effects](EFFECTS.md#space-alpha-crossfade-removed)).

## Verification boundaries

`navigation_tests` executes the production navigation implementation with
simulated OS calls. It covers repeat bursts, opacity restoration on failure,
custom opacity, window eligibility, display focus, window moves, the starting
Desktop of relative navigation, the raise decision, effects turned off and the
three memory-pressure fallbacks of a crossfade step.
`navigation_spaces_tests` reads a constructed `SLSCopyManagedDisplaySpaces`
reply and checks its lookups and fallbacks. `navigation_queue_tests` covers request recognition and merging, and the
daemon fuzz target runs the request check. The upstream unity-test executable
also checks numeric argument validation. `navigation_ingress_tests` uses actual
socket admission, the token/selector parser, command and schedule with simulated
host services; it covers numeric and mixed bursts, wrapping, duplicate targets,
invalid input, rejected queues and cancellation, that `veil` reaches the
step, is timed and merged like `crossfade`, and no other word is accepted, and
that a request without effect takes the type and duration set when it was read. `navigation_captured_schedule_tests`
connects the real asynchronous step and schedule to check clicks, superseded
captures, late callbacks, destination and focus decisions; `navigation_tests`
and the schedule tests also run the veil step and distinguish it from the
crossfade when requests merge. None establishes
visual quality, application focus behavior or live Dock behavior.

On 2026-09-26, a local 16-switch comparison at 150 ms intervals measured the
old script at 310 ms median completion latency (926 ms maximum; one failed
request), versus 25–48 ms median for direct space-focus commands. Child CPU
was about 0.95 s versus 0.19–0.20 s. These are local workload observations,
not a Hyprland comparison or measurements of the new command.

WindowServer also consumed about 47% of one CPU core at rest in that session;
its total CPU during switching cannot all be attributed to the fade. Compare
the new command live under the same workload before claiming a CPU or latency
improvement.

Live testing of lcs.4 found 4.4–4.6 s median latency for a rapid 16-request
burst in another session (old-script control: 1.1 s, with five failures).
Stack sampling identified waits in AXRaise and repeated AX focused-window
queries. The subsequent daemon correction removes those calls from ordinary
navigation; its live latency still needs verification after activation.
WindowServer consumed about 82% of one core even at rest in that session.

Payload `2.1.31-lcs.3` adds the [shared, time-based opacity engine](EFFECTS.md).
The activated lcs.5 latency checks and remaining signal-dispatch bottleneck
are recorded in [performance investigation](PERFORMANCE.md). Frame timing
and visual acceptance remain separate from command completion.
